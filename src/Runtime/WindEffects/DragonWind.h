#pragma once

#include "Runtime/WindEffectSettings.h"
#include "WindEffect.h"

/** Adds dragon wingbeats, flight wakes, impacts, breath, and soul absorption to shared wind. */
class DragonWind final : public WindEffect
{
public:
	/** Tunable parameters for one dragon landing shockwave class. */
	using ImpactProfile = DragonWindImpactProfile;

	/** Advanced tuning for all dragon-owned visual wind sources. */
	using Settings = DragonWindSettings;

	/** @copydoc WindEffect::GetId */
	[[nodiscard]] std::string_view GetId() const override { return "dragon"; }
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

	/** Returns a thread-safe snapshot of dragon wake states and CPU force probes. */
	[[nodiscard]] static nlohmann::json GetWakeDiagnostics(const nlohmann::json& a_args);

private:
	void SanitizeSettings();

	Settings settings;
};
