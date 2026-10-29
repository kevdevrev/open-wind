#pragma once

#include "Runtime/WindEffectSettings.h"
#include "WindEffect.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/** Routes loaded magic projectiles into launch, continuous-cast, and impact wind sources. */
class ProjectileMagicWindRouter final :
	public WindEffect,
	public RE::BSTEventSink<RE::TESSpellCastEvent>
{
public:
	/** User tuning for record-driven magic projectile wind. */
	using Settings = ProjectileMagicWindRouterSettings;

	/** Registers this router with the shared projectile dispatcher. */
	ProjectileMagicWindRouter();

	/** Unregisters this router from the shared projectile dispatcher. */
	~ProjectileMagicWindRouter() override;

	/** @copydoc WindEffect::GetId */
	[[nodiscard]] std::string_view GetId() const override { return "projectileMagic"; }

	/** @copydoc WindEffect::GetDisplayName */
	[[nodiscard]] std::string GetDisplayName() const override;

	/** @copydoc WindEffect::LoadSettings */
	void LoadSettings(const nlohmann::json& a_json) override;

	/** @copydoc WindEffect::SaveSettings */
	void SaveSettings(nlohmann::json& a_json) const override;

	/** @copydoc WindEffect::RestoreDefaultSettings */
	void RestoreDefaultSettings() override;

	/** @copydoc WindEffect::DataLoaded */
	void DataLoaded() override;

	/** @copydoc WindEffect::Update */
	void Update(float a_frameTime) override;

	/** @copydoc WindEffect::Reset */
	void Reset() override;

private:
	struct ProjectileProfile
	{
		float range{};
		float radius{};
		float strength{};
		float propagationSpeed{};
		float coneCosine{};
		bool explosive{};
		bool concentration{};
		bool selfDelivery{};
		bool nativePhysics{};
	};

	struct RecentImpact
	{
		std::uintptr_t projectile{};
		RE::NiPoint3 position{};
		std::chrono::steady_clock::time_point time{};
	};

	struct PendingImpact
	{
		RE::NiPoint3 position{};
		RE::NiPoint3 velocity{};
		ProjectileProfile profile{};
	};

	struct PendingCast
	{
		RE::ActorHandle actor;
		RE::FormID actorFormID{};
		RE::FormID spellFormID{};
	};

	struct ConcentrationObservation
	{
		RE::ObjectRefHandle shooter;
		RE::MagicSystem::CastingSource castingSource{};
		RE::FormID spellFormID{};
		RE::FormID projectileFormID{};
		float projectileAge{};
		float3 position{};
		float3 direction{};
		ProjectileProfile profile{};
	};

	struct ConcentrationState
	{
		std::mutex mutex;
		std::unordered_map<uint64_t, ConcentrationObservation> pending;
		std::unordered_map<uint64_t, ConcentrationObservation> resolved;
		uint64_t generation{};
		bool enabled{};
		bool taskPending{};
	};

	struct ConcentrationStream
	{
		ConcentrationObservation observation;
		float age{};
	};

	RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
		RE::BSTEventSource<RE::TESSpellCastEvent>*) override;

	void ObserveImpact(RE::Projectile& a_projectile, const RE::NiPoint3& a_position,
		const RE::NiPoint3& a_velocity);
	void ObserveMotion(RE::Projectile& a_projectile);
	void UpdateConcentration(float a_frameTime);
	static void ResolveConcentration(const std::shared_ptr<ConcentrationState>& a_state,
		uint64_t a_generation, std::unordered_map<uint64_t, ConcentrationObservation> a_observations);
	[[nodiscard]] bool AcceptImpactLocked(const RE::Projectile& a_projectile,
		const RE::NiPoint3& a_position);
	[[nodiscard]] static std::optional<ProjectileProfile> ClassifyProjectile(
		const RE::BGSProjectile* a_projectile, float a_area = 0.0f);
	[[nodiscard]] static std::optional<ProjectileProfile> ClassifyMagicEffect(
		const RE::EffectSetting* a_effect, float a_area = 0.0f);
	[[nodiscard]] static std::optional<ProjectileProfile> ClassifyMagicItem(
		const RE::MagicItem* a_magicItem);
	[[nodiscard]] std::optional<ProjectileProfile> FindRuntimeProfile(
		const RE::Projectile& a_projectile) const;
	void EmitCast(const PendingCast& a_cast) const;
	void EmitImpact(const PendingImpact& a_impact);
	void SanitizeSettings();

	Settings settings;
	std::unordered_map<RE::FormID, ProjectileProfile> projectileProfiles;
	std::unordered_map<RE::FormID, ProjectileProfile> magicEffectProfiles;
	std::unordered_map<RE::FormID, ProjectileProfile> magicItemProfiles;
	std::unordered_set<RE::FormID> excludedMagicItems;
	std::unordered_set<RE::FormID> excludedMagicEffects;
	std::unordered_set<RE::FormID> shoutMagicItems;
	std::vector<RecentImpact> recentImpacts;
	std::vector<PendingImpact> pendingImpacts;
	std::vector<PendingCast> pendingCasts;
	std::mutex pendingMutex;
	std::shared_ptr<ConcentrationState> concentrationState = std::make_shared<ConcentrationState>();
	std::unordered_map<uint64_t, ConcentrationStream> concentrationStreams;
	bool recordsLoaded = false;
};
