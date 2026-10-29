#include "ProjectileMagicWindRouter.h"

#include "FusRoDahWind.h"
#include "Runtime/Runtime.h"
#include "OpenWind/TransientWindImpulse.h"
#include "ProjectileHookDispatcher.h"
#include "Runtime/ActorUtils.h"
#include "Runtime/WindEffects/ImpactDeduplication.h"
#include "Runtime/WindMath.h"
#include "SpellShoutWindRouter.h"
#include "StormCallRecords.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <numbers>

namespace
{
	using namespace ProjectileMagicWindRouterLimits;

	constexpr float kMinimumRadius = 96.0f;
	constexpr float kMaximumRadius = 2400.0f;
	constexpr float kMinimumRange = 400.0f;
	constexpr float kMaximumRange = 6000.0f;
	constexpr float kDuplicateWindow = 0.25f;
	constexpr float kCrossImpactWindow = 0.04f;
	constexpr float kBurstWindow = 0.1f;
	constexpr std::size_t kMaximumSourcesPerBurst = 16;
	constexpr std::size_t kMaximumRecentImpacts = 96;
	constexpr std::size_t kMaximumPendingCasts = 96;
	constexpr std::size_t kMaximumConcentrationStreams = 32;
	constexpr float kConcentrationObservationGrace = 0.1f;
	constexpr float kMinimumDirectionLength = 1e-4f;

	float3 NormalizeDirection(const RE::NiPoint3& a_direction)
	{
		const float length = std::hypot(a_direction.x, a_direction.y, a_direction.z);
		return std::isfinite(length) && length > kMinimumDirectionLength ?
		           float3{ a_direction.x / length, a_direction.y / length, a_direction.z / length } :
		           float3{};
	}
}

using WindMath::ClampFiniteOrDefault;
using WindMath::GetHorizontalVelocityDirection;
using WindMath::SquaredDistance;

ProjectileMagicWindRouter::ProjectileMagicWindRouter()
{
	auto& dispatcher = ProjectileHookDispatcher::GetSingleton();
	dispatcher.AddImpactObserver(
		this, ProjectileHookDispatcher::ImpactObserverTrampoline<ProjectileMagicWindRouter, &ProjectileMagicWindRouter::ObserveImpact>);
	dispatcher.AddMotionObserver(this, [](void* a_owner, RE::Projectile& a_projectile, float) {
		static_cast<ProjectileMagicWindRouter*>(a_owner)->ObserveMotion(a_projectile);
	});
}

ProjectileMagicWindRouter::~ProjectileMagicWindRouter()
{
	ProjectileHookDispatcher::GetSingleton().RemoveObservers(this);
	std::lock_guard lock(concentrationState->mutex);
	concentrationState->enabled = false;
	++concentrationState->generation;
}

std::string ProjectileMagicWindRouter::GetDisplayName() const
{
	return "Projectile Magic";
}

void ProjectileMagicWindRouter::LoadSettings(const nlohmann::json& a_json)
{
	settings = a_json;
	SanitizeSettings();
}

void ProjectileMagicWindRouter::SaveSettings(nlohmann::json& a_json) const
{
	a_json = settings;
}

void ProjectileMagicWindRouter::RestoreDefaultSettings()
{
	settings = {};
}

void ProjectileMagicWindRouter::DataLoaded()
{
	if (recordsLoaded)
		return;
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
	if (!dataHandler || !eventSourceHolder) {
		logger::warn("Unable to register projectile magic wind: game data is unavailable");
		return;
	}

	projectileProfiles.clear();
	magicEffectProfiles.clear();
	magicItemProfiles.clear();
	excludedMagicItems.clear();
	excludedMagicEffects.clear();
	shoutMagicItems.clear();
	for (const auto* shout : dataHandler->GetFormArray<RE::TESShout>()) {
		if (!shout)
			continue;
		for (const auto& variation : shout->variations) {
			if (variation.spell)
				shoutMagicItems.emplace(variation.spell->GetFormID());
		}
	}
	for (const auto& bolt : StormCallRecords::kBolts) {
		if (const auto* spell = StormCallRecords::ResolveBolt(*dataHandler, bolt))
			excludedMagicItems.emplace(spell->GetFormID());
	}
	SpellShoutWindRouter::CollectOwnedMagicItems(*dataHandler, excludedMagicItems);
	FusRoDahWind::CollectOwnedMagicItems(*dataHandler, excludedMagicItems);
	for (const auto formID : excludedMagicItems) {
		const auto* form = RE::TESForm::LookupByID(formID);
		const auto* magicItem = form ? form->As<RE::MagicItem>() : nullptr;
		if (!magicItem)
			continue;
		for (const auto* effect : magicItem->effects) {
			if (effect && effect->baseEffect)
				excludedMagicEffects.emplace(effect->baseEffect->GetFormID());
		}
	}
	for (const auto* effect : dataHandler->GetFormArray<RE::EffectSetting>()) {
		if (!effect)
			continue;
		if (const auto profile = ClassifyMagicEffect(effect)) {
			magicEffectProfiles.emplace(effect->GetFormID(), *profile);
			if (effect->data.projectileBase) {
				projectileProfiles.insert_or_assign(effect->data.projectileBase->GetFormID(), *profile);
				ProjectileHookDispatcher::GetSingleton().ObserveProjectileType(
					*effect->data.projectileBase, ProjectileHookDispatcher::Event::Impact);
			}
		}
	}

	const auto indexMagicItems = [&](const auto& a_items) {
		for (const auto* item : a_items) {
			if (const auto profile = ClassifyMagicItem(item)) {
				magicItemProfiles.emplace(item->GetFormID(), *profile);
				if (profile->concentration && item->GetDelivery() == RE::MagicSystem::Delivery::kAimed) {
					for (const auto* effect : item->effects) {
						if (effect && effect->baseEffect && effect->baseEffect->data.projectileBase)
							ProjectileHookDispatcher::GetSingleton().ObserveProjectileType(
								*effect->baseEffect->data.projectileBase, ProjectileHookDispatcher::Event::Motion);
					}
				}
			}
		}
	};
	indexMagicItems(dataHandler->GetFormArray<RE::SpellItem>());
	indexMagicItems(dataHandler->GetFormArray<RE::ScrollItem>());
	indexMagicItems(dataHandler->GetFormArray<RE::EnchantmentItem>());

	eventSourceHolder->AddEventSink<RE::TESSpellCastEvent>(this);
	recordsLoaded = true;
	logger::info("Registered projectile magic wind: {} magic items, {} effects, and {} projectiles",
		magicItemProfiles.size(), magicEffectProfiles.size(), projectileProfiles.size());
}

RE::BSEventNotifyControl ProjectileMagicWindRouter::ProcessEvent(const RE::TESSpellCastEvent* a_event,
	RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
	if (!a_event || !a_event->object || excludedMagicItems.contains(a_event->spell) ||
		!magicItemProfiles.contains(a_event->spell))
		return RE::BSEventNotifyControl::kContinue;
	auto* actor = a_event->object->As<RE::Actor>();
	if (!actor)
		return RE::BSEventNotifyControl::kContinue;

	const PendingCast cast{ actor->GetHandle(), actor->GetFormID(), a_event->spell };
	std::lock_guard lock(pendingMutex);
	const auto duplicate = std::ranges::find_if(pendingCasts, [&](const PendingCast& a_pending) {
		return a_pending.actorFormID == cast.actorFormID && a_pending.spellFormID == cast.spellFormID;
	});
	if (duplicate == pendingCasts.end()) {
		if (pendingCasts.size() >= kMaximumPendingCasts)
			pendingCasts.erase(pendingCasts.begin());
		pendingCasts.push_back(cast);
	}
	return RE::BSEventNotifyControl::kContinue;
}

void ProjectileMagicWindRouter::Update(float a_frameTime)
{
	std::vector<PendingImpact> impacts;
	std::vector<PendingCast> casts;
	{
		std::lock_guard lock(pendingMutex);
		impacts.swap(pendingImpacts);
		casts.swap(pendingCasts);
	}
	UpdateConcentration(a_frameTime);
	if (!settings.enabled)
		return;

	for (const auto& cast : casts)
		EmitCast(cast);
	for (const auto& impact : impacts)
		EmitImpact(impact);
}

void ProjectileMagicWindRouter::Reset()
{
	{
		std::lock_guard lock(pendingMutex);
		recentImpacts.clear();
		pendingImpacts.clear();
		pendingCasts.clear();
	}
	{
		std::lock_guard lock(concentrationState->mutex);
		++concentrationState->generation;
		concentrationState->enabled = false;
		concentrationState->pending.clear();
		concentrationState->resolved.clear();
	}
	concentrationStreams.clear();
	Wind::Get().ClearTransientWindSources(Wind::TransientWindSourceOwner::ProjectileMagic);
}

void ProjectileMagicWindRouter::ObserveImpact(RE::Projectile& a_projectile,
	const RE::NiPoint3& a_position, const RE::NiPoint3& a_velocity)
{
	if (!std::isfinite(a_position.x) || !std::isfinite(a_position.y) || !std::isfinite(a_position.z))
		return;
	const auto profile = FindRuntimeProfile(a_projectile);
	if (!profile || profile->explosive)
		return;
	const auto* spell = a_projectile.GetProjectileRuntimeData().spell;
	if (profile->concentration && spell && spell->GetDelivery() == RE::MagicSystem::Delivery::kAimed)
		return;

	std::lock_guard lock(pendingMutex);
	if (!AcceptImpactLocked(a_projectile, a_position))
		return;
	if (pendingImpacts.size() >= kMaximumRecentImpacts)
		pendingImpacts.erase(pendingImpacts.begin());
	pendingImpacts.push_back({ a_position, a_velocity, *profile });
}

void ProjectileMagicWindRouter::ObserveMotion(RE::Projectile& a_projectile)
{
	const auto& runtimeData = a_projectile.GetProjectileRuntimeData();
	if (!runtimeData.spell || !runtimeData.shooter ||
		runtimeData.spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration ||
		runtimeData.spell->GetDelivery() != RE::MagicSystem::Delivery::kAimed ||
		runtimeData.castingSource < RE::MagicSystem::CastingSource::kLeftHand ||
		runtimeData.castingSource >= RE::MagicSystem::CastingSource::kNone ||
		runtimeData.flags.any(RE::Projectile::Flags::kFading, RE::Projectile::Flags::kDestroyed))
		return;
	const auto profile = FindRuntimeProfile(a_projectile);
	if (!profile)
		return;
	const auto position = a_projectile.GetPosition();
	if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
		!std::isfinite(runtimeData.livingTime))
		return;
	auto direction = NormalizeDirection(runtimeData.linearVelocity);
	if (direction.Length() <= kMinimumDirectionLength)
		direction = NormalizeDirection(runtimeData.velocity);
	if (direction.Length() <= kMinimumDirectionLength) {
		const float pitch = a_projectile.GetAngleX();
		const float heading = a_projectile.GetAngleZ();
		if (!std::isfinite(pitch) || !std::isfinite(heading))
			return;
		direction = { std::cos(pitch) * std::sin(heading), std::cos(pitch) * std::cos(heading), -std::sin(pitch) };
	}
	const uint64_t key = (static_cast<uint64_t>(runtimeData.shooter.native_handle()) << 32) |
	                     static_cast<uint32_t>(runtimeData.castingSource);
	std::lock_guard lock(concentrationState->mutex);
	if (!concentrationState->enabled)
		return;
	const auto found = concentrationState->pending.find(key);
	if (found != concentrationState->pending.end()) {
		if (found->second.projectileFormID != a_projectile.GetFormID() &&
			found->second.projectileAge < runtimeData.livingTime)
			return;
	} else if (concentrationState->pending.size() >= kMaximumConcentrationStreams) {
		return;
	}
	concentrationState->pending.insert_or_assign(key, ConcentrationObservation{
		runtimeData.shooter, runtimeData.castingSource, runtimeData.spell->GetFormID(),
		a_projectile.GetFormID(), runtimeData.livingTime,
		{ position.x, position.y, position.z }, direction, *profile });
}

void ProjectileMagicWindRouter::ResolveConcentration(const std::shared_ptr<ConcentrationState>& a_state,
	uint64_t a_generation, std::unordered_map<uint64_t, ConcentrationObservation> a_observations)
{
	{
		std::lock_guard lock(a_state->mutex);
		if (a_state->generation != a_generation || !a_state->enabled) {
			a_state->taskPending = false;
			return;
		}
	}
	for (auto iterator = a_observations.begin(); iterator != a_observations.end();) {
		auto& observation = iterator->second;
		auto reference = observation.shooter.get();
		auto* actor = reference ? reference->As<RE::Actor>() : nullptr;
		if (!actor || !actor->Is3DLoaded() || actor->IsDead() || Util::IsDragon(*actor)) {
			iterator = a_observations.erase(iterator);
			continue;
		}
		auto* caster = actor->GetMagicCaster(observation.castingSource);
		if (!caster || caster->state != RE::MagicCaster::State::kCasting || !caster->currentSpell ||
			caster->currentSpell->GetFormID() != observation.spellFormID) {
			iterator = a_observations.erase(iterator);
			continue;
		}
		if (auto* node = caster->GetMagicNode()) {
			const auto& position = node->world.translate;
			observation.position = { position.x, position.y, position.z };
		}
		if (!std::isfinite(observation.position.x) || !std::isfinite(observation.position.y) ||
			!std::isfinite(observation.position.z))
			iterator = a_observations.erase(iterator);
		else
			++iterator;
	}
	std::lock_guard lock(a_state->mutex);
	if (a_state->generation == a_generation && a_state->enabled) {
		for (auto& [key, observation] : a_observations)
			a_state->resolved.insert_or_assign(key, std::move(observation));
	}
	a_state->taskPending = false;
}

void ProjectileMagicWindRouter::UpdateConcentration(float a_frameTime)
{
	if (!settings.enabled || settings.concentrationStrength <= 0.0f) {
		{
			std::lock_guard lock(concentrationState->mutex);
			if (concentrationState->enabled)
				++concentrationState->generation;
			concentrationState->enabled = false;
			concentrationState->pending.clear();
			concentrationState->resolved.clear();
		}
		concentrationStreams.clear();
		Wind::Get().SetAttachedTransientWindSources(Wind::TransientWindSourceOwner::ProjectileMagic, {});
		return;
	}
	std::unordered_map<uint64_t, ConcentrationObservation> resolved;
	std::unordered_map<uint64_t, ConcentrationObservation> pending;
	uint64_t generation = 0;
	const auto* tasks = SKSE::GetTaskInterface();
	{
		std::lock_guard lock(concentrationState->mutex);
		concentrationState->enabled = true;
		resolved.swap(concentrationState->resolved);
		if (tasks && !concentrationState->taskPending && !concentrationState->pending.empty()) {
			pending.swap(concentrationState->pending);
			concentrationState->taskPending = true;
			generation = concentrationState->generation;
		}
	}
	if (!pending.empty()) {
		try {
			tasks->AddTask([state = concentrationState, generation, observations = std::move(pending)]() mutable {
				try {
					ResolveConcentration(state, generation, std::move(observations));
				} catch (...) {
					std::lock_guard lock(state->mutex);
					state->taskPending = false;
				}
			});
		} catch (...) {
			std::lock_guard lock(concentrationState->mutex);
			concentrationState->taskPending = false;
		}
	}
	const float frameTime = std::isfinite(a_frameTime) ? std::max(a_frameTime, 0.0f) : 0.0f;
	for (auto& [key, stream] : concentrationStreams)
		stream.age += frameTime;
	std::erase_if(concentrationStreams, [&](const auto& a_entry) {
		return a_entry.second.age > kConcentrationObservationGrace + settings.concentrationReleaseTime;
	});
	for (auto& [key, observation] : resolved) {
		if (concentrationStreams.size() < kMaximumConcentrationStreams || concentrationStreams.contains(key))
			concentrationStreams.insert_or_assign(key, ConcentrationStream{ std::move(observation), 0.0f });
	}
	std::vector<Wind::TransientWindSourceSubmission> sources;
	sources.reserve(concentrationStreams.size());
	for (const auto& [key, stream] : concentrationStreams) {
		const float release = settings.concentrationReleaseTime > 0.0f ?
		                          std::clamp((stream.age - kConcentrationObservationGrace) / settings.concentrationReleaseTime, 0.0f, 1.0f) :
		                          0.0f;
		const float amplitude = 1.0f - release * release * (3.0f - 2.0f * release);
		const auto& observation = stream.observation;
		auto source = WindField::MakeDirectionalCone(observation.position, observation.direction,
			settings.concentrationStrength * observation.profile.strength * amplitude,
			observation.profile.range, observation.profile.coneCosine);
		source.collisionRadius = observation.profile.radius * settings.radiusMultiplier;
		sources.push_back({ source, Wind::TransientWindSourcePriority::Breath, Wind::TransientWindPhysics::Wind });
	}
	Wind::Get().SetAttachedTransientWindSources(Wind::TransientWindSourceOwner::ProjectileMagic, sources);
}

std::optional<ProjectileMagicWindRouter::ProjectileProfile> ProjectileMagicWindRouter::ClassifyProjectile(
	const RE::BGSProjectile* a_projectile, float a_area)
{
	if (!a_projectile)
		return std::nullopt;
	const float range = ClampFiniteOrDefault(a_projectile->data.range, kMinimumRange, kMaximumRange, 2000.0f);
	const float collisionRadius = ClampFiniteOrDefault(
		a_projectile->data.collisionRadius * 4.0f, kMinimumRadius, kMaximumRadius, kMinimumRadius);
	const float radius = std::clamp(std::max(collisionRadius, a_area), kMinimumRadius, kMaximumRadius);
	const float force = std::isfinite(a_projectile->data.force) ? std::max(a_projectile->data.force, 0.0f) : 0.0f;
	const float strength = std::clamp(0.45f + radius / 1200.0f + std::sqrt(std::min(force, 5000.0f) / 5000.0f),
		0.5f, 2.5f);
	const float speed = ClampFiniteOrDefault(a_projectile->data.speed, 400.0f, 5000.0f, 1800.0f);
	const float coneHalfAngle = std::clamp(10.0f + radius / std::max(range, 1.0f) * 180.0f, 10.0f, 40.0f);
	const bool explosive = a_projectile->data.explosionType ||
	                       a_projectile->data.flags.all(RE::BGSProjectileData::BGSProjectileFlags::kExplosion);
	return ProjectileProfile{
		range, radius, strength, speed,
		std::cos(coneHalfAngle * std::numbers::pi_v<float> / 180.0f), explosive, false, false,
		force > 0.0f || (a_projectile->data.explosionType &&
							std::isfinite(a_projectile->data.explosionType->data.force) &&
							a_projectile->data.explosionType->data.force > 0.0f)
	};
}

std::optional<ProjectileMagicWindRouter::ProjectileProfile> ProjectileMagicWindRouter::ClassifyMagicEffect(
	const RE::EffectSetting* a_effect, float a_area)
{
	if (!a_effect || !a_effect->data.projectileBase)
		return std::nullopt;
	auto profile = ClassifyProjectile(a_effect->data.projectileBase, a_area);
	if (profile && a_effect->data.explosion) {
		profile->explosive = true;
		profile->nativePhysics = profile->nativePhysics ||
		                         (std::isfinite(a_effect->data.explosion->data.force) &&
									 a_effect->data.explosion->data.force > 0.0f);
	}
	return profile;
}

std::optional<ProjectileMagicWindRouter::ProjectileProfile> ProjectileMagicWindRouter::ClassifyMagicItem(
	const RE::MagicItem* a_magicItem)
{
	if (!a_magicItem)
		return std::nullopt;
	std::optional<ProjectileProfile> result;
	for (const auto* effect : a_magicItem->effects) {
		if (!effect)
			continue;
		auto profile = ClassifyMagicEffect(effect->baseEffect, static_cast<float>(effect->effectItem.area));
		if (!profile)
			continue;
		const bool nativePhysics = (result && result->nativePhysics) || profile->nativePhysics;
		if (!result || profile->radius > result->radius)
			result = profile;
		result->nativePhysics = nativePhysics;
	}
	if (result) {
		result->range = std::clamp(std::max(result->range, a_magicItem->GetRange()), kMinimumRange, kMaximumRange);
		result->concentration = a_magicItem->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
		result->selfDelivery = a_magicItem->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
	}
	return result;
}

std::optional<ProjectileMagicWindRouter::ProjectileProfile> ProjectileMagicWindRouter::FindRuntimeProfile(
	const RE::Projectile& a_projectile) const
{
	const auto& runtimeData = a_projectile.GetProjectileRuntimeData();
	if (!runtimeData.spell && !runtimeData.avEffect)
		return std::nullopt;
	if (runtimeData.spell && excludedMagicItems.contains(runtimeData.spell->GetFormID()))
		return std::nullopt;
	if (runtimeData.avEffect && excludedMagicEffects.contains(runtimeData.avEffect->GetFormID()))
		return std::nullopt;
	std::optional<ProjectileProfile> result;
	if (runtimeData.avEffect) {
		const auto found = magicEffectProfiles.find(runtimeData.avEffect->GetFormID());
		if (found != magicEffectProfiles.end())
			result = found->second;
	}
	const auto* base = a_projectile.GetProjectileBase();
	if (!result && runtimeData.spell) {
		const auto found = magicItemProfiles.find(runtimeData.spell->GetFormID());
		if (found != magicItemProfiles.end())
			result = found->second;
	}
	if (!result && base) {
		const auto found = projectileProfiles.find(base->GetFormID());
		if (found != projectileProfiles.end())
			result = found->second;
	}
	if (result && (runtimeData.explosion || (base && (base->data.explosionType ||
														 base->data.flags.all(RE::BGSProjectileData::BGSProjectileFlags::kExplosion)))))
		result->explosive = true;
	if (result && base) {
		const auto* explosion = runtimeData.explosion;
		if (!explosion && runtimeData.avEffect)
			explosion = runtimeData.avEffect->data.explosion;
		if (!explosion)
			explosion = base->data.explosionType;
		result->nativePhysics = (std::isfinite(base->data.force) && base->data.force > 0.0f) ||
		                        (explosion && std::isfinite(explosion->data.force) && explosion->data.force > 0.0f);
	}
	if (result && runtimeData.spell) {
		result->concentration = runtimeData.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
		result->selfDelivery = runtimeData.spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
	}
	return result;
}

void ProjectileMagicWindRouter::EmitCast(const PendingCast& a_cast) const
{
	const auto route = magicItemProfiles.find(a_cast.spellFormID);
	if (route == magicItemProfiles.end() || route->second.concentration || route->second.selfDelivery ||
		settings.launchStrength <= 0.0f)
		return;
	auto actor = a_cast.actor.get();
	if (!actor)
		return;
	if (shoutMagicItems.contains(a_cast.spellFormID) && Util::IsDragon(*actor))
		return;
	const float dragonScale = Util::IsDragon(*actor) ? settings.dragonStrength : 1.0f;
	const auto& profile = route->second;
	const auto source = WindField::MakeDirectionalWave(
		Util::GetMagicOrigin(*actor), Util::GetAimDirection(*actor),
		settings.launchStrength * dragonScale * profile.strength, profile.range,
		std::clamp(profile.radius * 1.5f, 120.0f, 600.0f), profile.propagationSpeed,
		profile.coneCosine, settings.decayTime);
	Wind::Get().QueueTransientWindSource(source,
		Wind::TransientWindSourceOwner::ProjectileMagic,
		Wind::TransientWindSourcePriority::Flight);
}

void ProjectileMagicWindRouter::EmitImpact(const PendingImpact& a_impact)
{
	if (settings.impactStrength <= 0.0f)
		return;
	const float radius = std::clamp(a_impact.profile.radius * settings.radiusMultiplier,
		kMinimumRadius, kMaximumRadius);
	const auto source = WindField::MakeRadialWave(
		{ a_impact.position.x, a_impact.position.y, a_impact.position.z },
		GetHorizontalVelocityDirection(a_impact.velocity), settings.impactStrength * a_impact.profile.strength,
		radius, std::clamp(radius * 0.35f, 64.0f, 600.0f),
		std::clamp(a_impact.profile.propagationSpeed, 1400.0f, 4500.0f), settings.decayTime);
	Wind::Get().QueueTransientWindSource(source,
		Wind::TransientWindSourceOwner::ProjectileMagic,
		Wind::TransientWindSourcePriority::Impact,
		a_impact.profile.nativePhysics ? Wind::TransientWindPhysics::Native : Wind::TransientWindPhysics::Wind);
}

bool ProjectileMagicWindRouter::AcceptImpactLocked(const RE::Projectile& a_projectile,
	const RE::NiPoint3& a_position)
{
	const auto projectileIdentity = reinterpret_cast<std::uintptr_t>(std::addressof(a_projectile));
	return WindImpactDedup::AcceptImpact(recentImpacts, projectileIdentity, a_position,
		kDuplicateWindow, kCrossImpactWindow, kBurstWindow, kMaximumSourcesPerBurst, kMaximumRecentImpacts);
}

void ProjectileMagicWindRouter::SanitizeSettings()
{
	const Settings defaults{};
	settings.launchStrength = ClampFiniteOrDefault(
		settings.launchStrength, kMinimumStrength, kMaximumStrength, defaults.launchStrength);
	settings.concentrationStrength = ClampFiniteOrDefault(
		settings.concentrationStrength, kMinimumStrength, kMaximumStrength, defaults.concentrationStrength);
	settings.concentrationReleaseTime = ClampFiniteOrDefault(settings.concentrationReleaseTime,
		0.0f, WindField::kTransientImpulseMaximumDecayTime, defaults.concentrationReleaseTime);
	settings.impactStrength = ClampFiniteOrDefault(
		settings.impactStrength, kMinimumStrength, kMaximumStrength, defaults.impactStrength);
	settings.dragonStrength = ClampFiniteOrDefault(
		settings.dragonStrength, kMinimumStrength, kMaximumStrength, defaults.dragonStrength);
	settings.radiusMultiplier = ClampFiniteOrDefault(settings.radiusMultiplier,
		kMinimumRadiusMultiplier, kMaximumRadiusMultiplier, defaults.radiusMultiplier);
	settings.decayTime = ClampFiniteOrDefault(settings.decayTime,
		0.0f, WindField::kTransientImpulseMaximumDecayTime, defaults.decayTime);
}
