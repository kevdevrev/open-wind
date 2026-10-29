#include "StormCallWindRouter.h"

#include "Runtime/Runtime.h"
#include "OpenWind/TransientWindImpulse.h"
#include "ProjectileHookDispatcher.h"
#include "Runtime/WindEffects/ImpactDeduplication.h"
#include "Runtime/WindMath.h"
#include "StormCallRecords.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace
{
	using namespace StormCallWindRouterLimits;

	constexpr float kDuplicateWindow = 0.25f;
	constexpr float kCrossImpactWindow = 0.05f;
	constexpr float kBurstWindow = 0.1f;
	constexpr std::size_t kMaximumSourcesPerBurst = 16;
	constexpr std::size_t kMaximumRecentImpacts = 96;

}

using WindMath::ClampFiniteOrDefault;
using WindMath::GetHorizontalVelocityDirection;
using WindMath::SquaredDistance;

StormCallWindRouter::StormCallWindRouter()
{
	ProjectileHookDispatcher::GetSingleton().AddImpactObserver(
		this, ProjectileHookDispatcher::ImpactObserverTrampoline<StormCallWindRouter, &StormCallWindRouter::ObserveImpact>);
}

StormCallWindRouter::~StormCallWindRouter()
{
	ProjectileHookDispatcher::GetSingleton().RemoveObservers(this);
}

std::string StormCallWindRouter::GetDisplayName() const
{
	return "Storm Call Strikes";
}

void StormCallWindRouter::LoadSettings(const nlohmann::json& a_json)
{
	settings = a_json;
	SanitizeSettings();
}

void StormCallWindRouter::SaveSettings(nlohmann::json& a_json) const
{
	a_json = settings;
}

void StormCallWindRouter::RestoreDefaultSettings()
{
	settings = {};
}

void StormCallWindRouter::DataLoaded()
{
	if (recordsLoaded)
		return;
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler) {
		logger::warn("Unable to register Storm Call strike wind: game data is unavailable");
		return;
	}

	boltStrengthBySpell.clear();
	boltStrengthByMagicEffect.clear();
	for (const auto& bolt : StormCallRecords::kBolts) {
		auto* spell = StormCallRecords::ResolveBolt(*dataHandler, bolt);
		if (!spell)
			continue;

		boltStrengthBySpell.emplace(spell->GetFormID(), bolt.strength);
		for (const auto* effectItem : spell->effects) {
			if (!effectItem || !effectItem->baseEffect)
				continue;
			boltStrengthByMagicEffect.insert_or_assign(effectItem->baseEffect->GetFormID(), bolt.strength);
			if (effectItem->baseEffect->data.projectileBase)
				ProjectileHookDispatcher::GetSingleton().ObserveProjectileType(
					*effectItem->baseEffect->data.projectileBase, ProjectileHookDispatcher::Event::Impact);
		}
	}

	if (auto* effect = dataHandler->LookupForm<RE::EffectSetting>(
			StormCallRecords::kEffectFormID, StormCallRecords::kPlugin);
		effect && effect->data.projectileBase)
		ProjectileHookDispatcher::GetSingleton().ObserveProjectileType(
			*effect->data.projectileBase, ProjectileHookDispatcher::Event::Impact);

	recordsLoaded = true;
	logger::info("Registered Storm Call strike wind for {} bolt spells and {} magic effects",
		boltStrengthBySpell.size(), boltStrengthByMagicEffect.size());
}

void StormCallWindRouter::Update(float)
{
	std::vector<PendingImpact> impacts;
	{
		std::lock_guard lock(pendingMutex);
		impacts.swap(pendingImpacts);
	}
	if (!settings.enabled || settings.strength <= 0.0f)
		return;

	for (const auto& impact : impacts) {
		const float waveHalfWidth = std::clamp(settings.radius * 0.3f, 96.0f, 500.0f);
		const float propagationSpeed = std::clamp(2200.0f + settings.radius, 2400.0f, 4500.0f);
		const auto source = WindField::MakeRadialWave(
			{ impact.position.x, impact.position.y, impact.position.z }, GetHorizontalVelocityDirection(impact.velocity),
			settings.strength * impact.strength, settings.radius, waveHalfWidth, propagationSpeed,
			settings.decayTime);
		Wind::Get().QueueTransientWindSource(source,
			Wind::TransientWindSourceOwner::StormCall,
			Wind::TransientWindSourcePriority::Impact,
			impact.nativePhysics ? Wind::TransientWindPhysics::Native : Wind::TransientWindPhysics::Wind);
	}
}

void StormCallWindRouter::Reset()
{
	{
		std::lock_guard lock(pendingMutex);
		recentImpacts.clear();
		pendingImpacts.clear();
	}
	Wind::Get().ClearTransientWindSources(Wind::TransientWindSourceOwner::StormCall);
}

void StormCallWindRouter::ObserveImpact(RE::Projectile& a_projectile,
	const RE::NiPoint3& a_position, const RE::NiPoint3& a_velocity)
{
	if (!std::isfinite(a_position.x) || !std::isfinite(a_position.y) || !std::isfinite(a_position.z))
		return;
	const auto& runtimeData = a_projectile.GetProjectileRuntimeData();
	float strength = 0.0f;
	if (runtimeData.spell) {
		const auto route = boltStrengthBySpell.find(runtimeData.spell->GetFormID());
		if (route != boltStrengthBySpell.end())
			strength = route->second;
	}
	if (strength <= 0.0f && runtimeData.avEffect) {
		const auto route = boltStrengthByMagicEffect.find(runtimeData.avEffect->GetFormID());
		if (route != boltStrengthByMagicEffect.end())
			strength = route->second;
	}
	if (strength <= 0.0f)
		return;

	std::lock_guard lock(pendingMutex);
	if (!AcceptImpactLocked(a_projectile, a_position))
		return;
	if (pendingImpacts.size() >= kMaximumRecentImpacts)
		pendingImpacts.erase(pendingImpacts.begin());
	const auto* projectile = a_projectile.GetProjectileBase();
	const bool nativePhysics = (projectile && std::isfinite(projectile->data.force) && projectile->data.force > 0.0f) ||
	                           (runtimeData.explosion && std::isfinite(runtimeData.explosion->data.force) && runtimeData.explosion->data.force > 0.0f);
	pendingImpacts.push_back({ a_position, a_velocity, strength, nativePhysics });
}

bool StormCallWindRouter::AcceptImpactLocked(const RE::Projectile& a_projectile,
	const RE::NiPoint3& a_position)
{
	const auto projectileIdentity = reinterpret_cast<std::uintptr_t>(std::addressof(a_projectile));
	return WindImpactDedup::AcceptImpact(recentImpacts, projectileIdentity, a_position,
		kDuplicateWindow, kCrossImpactWindow, kBurstWindow, kMaximumSourcesPerBurst, kMaximumRecentImpacts);
}

void StormCallWindRouter::SanitizeSettings()
{
	const Settings defaults{};
	settings.strength = ClampFiniteOrDefault(
		settings.strength, kMinimumStrength, kMaximumStrength, defaults.strength);
	settings.radius = ClampFiniteOrDefault(
		settings.radius, kMinimumRadius, kMaximumRadius, defaults.radius);
	settings.decayTime = ClampFiniteOrDefault(settings.decayTime,
		0.0f, WindField::kTransientImpulseMaximumDecayTime, defaults.decayTime);
}
