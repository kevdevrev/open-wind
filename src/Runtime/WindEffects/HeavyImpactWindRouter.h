#pragma once

#include "Runtime/WindEffectSettings.h"
#include "WindEffect.h"

#include <memory>

namespace RE
{
	class Actor;
}

/**
 * Routes large physical actor events into bounded one-shot transient wind sources.
 * Grounded dragon hits require wing or tail identity in current attack or weapon metadata.
 */
class HeavyImpactWindRouter final : public WindEffect
{
public:
	/** Identifies a supported impact so additional reliable event producers can share the router. */
	enum class ImpactKind
	{
		PowerAttack,
		GiantClubSmash,
		DragonGroundAttack,
		TrollPowerAttack,
		MammothHeavyAttack,
		CenturionHeavyAttack,
		BeastTransformation,
		ShieldCharge
	};

	/** Parameters for a traveling radial shockwave. */
	using RadialProfile = HeavyImpactWindRouterRadialProfile;

	/** Parameters for a traveling directional pressure wave. */
	using DirectionalProfile = HeavyImpactWindRouterDirectionalProfile;

	/** Tuning shared by event classification and each emitted source class. */
	using Settings = HeavyImpactWindRouterSettings;

	HeavyImpactWindRouter();
	~HeavyImpactWindRouter();
	HeavyImpactWindRouter(const HeavyImpactWindRouter&) = delete;
	HeavyImpactWindRouter& operator=(const HeavyImpactWindRouter&) = delete;

	/** @copydoc WindEffect::GetId */
	[[nodiscard]] std::string_view GetId() const override { return "heavyImpacts"; }

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

	/** Queues a pre-classified impact from another reliable event producer. */
	void QueueImpact(RE::Actor& a_actor, ImpactKind a_kind);

private:
	void SanitizeSettings();

	class Impl;
	std::unique_ptr<Impl> implementation;
	Settings settings;
};
