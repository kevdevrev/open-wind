#include "Runtime/Runtime.h"
#include "Runtime/WindMath.h"
#include <algorithm>
#include <cmath>

using namespace WindSettingsLimits;
using WindMath::ClampFiniteOrDefault;

void Wind::SanitizeSettings(Settings& a_settings)
{
	const Settings defaults{};
	a_settings.trunkWindBendSensitivity = ClampFiniteOrDefault(a_settings.trunkWindBendSensitivity,
		kTrunkWindSensitivityMin, kTrunkWindSensitivityMax, defaults.trunkWindBendSensitivity);
	a_settings.treeLeafBaseWindFlutterGain = ClampFiniteOrDefault(a_settings.treeLeafBaseWindFlutterGain,
		kTreeLeafBaseWindFlutterGainMin, kTreeLeafBaseWindFlutterGainMax, defaults.treeLeafBaseWindFlutterGain);
	a_settings.treeWindGustScale = ClampFiniteOrDefault(a_settings.treeWindGustScale,
		kTreeWindGustScaleMin, kTreeWindGustScaleMax, defaults.treeWindGustScale);
	a_settings.treeWindGustSoftLimit = ClampFiniteOrDefault(a_settings.treeWindGustSoftLimit,
		kTreeWindGustSoftLimitMin, kTreeWindGustSoftLimitMax, defaults.treeWindGustSoftLimit);
	a_settings.treeWindSpringFrequency = ClampFiniteOrDefault(a_settings.treeWindSpringFrequency,
		kTreeWindSpringFrequencyMin, kTreeWindSpringFrequencyMax, defaults.treeWindSpringFrequency);
	a_settings.treeWindSpringDamping = ClampFiniteOrDefault(a_settings.treeWindSpringDamping,
		kTreeWindSpringDampingMin, kTreeWindSpringDampingMax, defaults.treeWindSpringDamping);
	a_settings.treeTransientSpringFrequency = ClampFiniteOrDefault(a_settings.treeTransientSpringFrequency,
		kTreeTransientSpringFrequencyMin, kTreeTransientSpringFrequencyMax, defaults.treeTransientSpringFrequency);
	a_settings.treeTransientSpringDamping = ClampFiniteOrDefault(a_settings.treeTransientSpringDamping,
		kTreeTransientSpringDampingMin, kTreeTransientSpringDampingMax, defaults.treeTransientSpringDamping);
	a_settings.windFieldGustScale = ClampFiniteOrDefault(a_settings.windFieldGustScale,
		kWindFieldGustScaleMin, kWindFieldGustScaleMax, defaults.windFieldGustScale);
	a_settings.windFieldGustCrosswindScale = ClampFiniteOrDefault(a_settings.windFieldGustCrosswindScale,
		kWindFieldGustCrosswindScaleMin, kWindFieldGustCrosswindScaleMax, defaults.windFieldGustCrosswindScale);
	a_settings.windFieldGustAmplitude = ClampFiniteOrDefault(a_settings.windFieldGustAmplitude,
		kWindFieldGustAmplitudeMin, kWindFieldGustAmplitudeMax, defaults.windFieldGustAmplitude);
	a_settings.windFieldMicroTurbulenceStrength = ClampFiniteOrDefault(a_settings.windFieldMicroTurbulenceStrength,
		kWindFieldMicroTurbulenceStrengthMin, kWindFieldMicroTurbulenceStrengthMax, defaults.windFieldMicroTurbulenceStrength);
	a_settings.windFieldFineBaselineAmplitude = ClampFiniteOrDefault(a_settings.windFieldFineBaselineAmplitude,
		kWindFieldFineBaselineAmplitudeMin, kWindFieldFineBaselineAmplitudeMax, defaults.windFieldFineBaselineAmplitude);
	a_settings.windFieldFineWindCoupling = ClampFiniteOrDefault(a_settings.windFieldFineWindCoupling,
		kWindFieldFineCouplingMin, kWindFieldFineCouplingMax, defaults.windFieldFineWindCoupling);
	a_settings.windFieldFineGustCoupling = ClampFiniteOrDefault(a_settings.windFieldFineGustCoupling,
		kWindFieldFineCouplingMin, kWindFieldFineCouplingMax, defaults.windFieldFineGustCoupling);
	a_settings.windFieldFineFrequency = ClampFiniteOrDefault(a_settings.windFieldFineFrequency,
		kWindFieldFineFrequencyMin, kWindFieldFineFrequencyMax, defaults.windFieldFineFrequency);
	a_settings.windFieldGustAdvectionMultiplier = ClampFiniteOrDefault(a_settings.windFieldGustAdvectionMultiplier,
		kWindFieldGustAdvectionMultiplierMin, kWindFieldGustAdvectionMultiplierMax, defaults.windFieldGustAdvectionMultiplier);
	a_settings.windFieldGustCoverage = ClampFiniteOrDefault(a_settings.windFieldGustCoverage,
		kWindFieldGustCoverageMin, kWindFieldGustCoverageMax, defaults.windFieldGustCoverage);
	a_settings.windFieldGustEdgeSoftness = ClampFiniteOrDefault(a_settings.windFieldGustEdgeSoftness,
		kWindFieldGustEdgeSoftnessMin, kWindFieldGustEdgeSoftnessMax, defaults.windFieldGustEdgeSoftness);
	a_settings.windFieldGustDistortionStrength = ClampFiniteOrDefault(a_settings.windFieldGustDistortionStrength,
		kWindFieldGustDistortionStrengthMin, kWindFieldGustDistortionStrengthMax, defaults.windFieldGustDistortionStrength);
	a_settings.windFieldGustDistortionScale = ClampFiniteOrDefault(a_settings.windFieldGustDistortionScale,
		kWindFieldGustDistortionScaleMin, kWindFieldGustDistortionScaleMax, defaults.windFieldGustDistortionScale);
	a_settings.windFieldGustDistortionSpeed = ClampFiniteOrDefault(a_settings.windFieldGustDistortionSpeed,
		kWindFieldGustDistortionSpeedMin, kWindFieldGustDistortionSpeedMax, defaults.windFieldGustDistortionSpeed);
	for (uint32_t index = 0; index < a_settings.windFieldGustAdvectionResponse.size(); ++index)
		a_settings.windFieldGustAdvectionResponse[index] = ClampFiniteOrDefault(
			a_settings.windFieldGustAdvectionResponse[index], kWindResponseMin, kWindResponseMax,
			defaults.windFieldGustAdvectionResponse[index]);
	a_settings.windFieldDirectionTransitionDuration = ClampFiniteOrDefault(a_settings.windFieldDirectionTransitionDuration,
		kWindFieldDirectionTransitionDurationMin, kWindFieldDirectionTransitionDurationMax,
		defaults.windFieldDirectionTransitionDuration);
	a_settings.terrainShelterStrength = ClampFiniteOrDefault(a_settings.terrainShelterStrength,
		kTerrainShelterStrengthMin, kTerrainShelterStrengthMax, defaults.terrainShelterStrength);
	a_settings.terrainShelterDistance = ClampFiniteOrDefault(a_settings.terrainShelterDistance,
		kTerrainShelterDistanceMin, kTerrainShelterDistanceMax, defaults.terrainShelterDistance);
	a_settings.terrainShelterSoftness = ClampFiniteOrDefault(a_settings.terrainShelterSoftness,
		kTerrainShelterSoftnessMin, kTerrainShelterSoftnessMax, defaults.terrainShelterSoftness);
	a_settings.terrainShelterRadius = ClampFiniteOrDefault(a_settings.terrainShelterRadius,
		kTerrainShelterRadiusMin, kTerrainShelterRadiusMax, defaults.terrainShelterRadius);
	a_settings.obstacleShelterStrength = ClampFiniteOrDefault(a_settings.obstacleShelterStrength,
		kObstacleShelterStrengthMin, kObstacleShelterStrengthMax, defaults.obstacleShelterStrength);
	a_settings.obstacleShelterRadius = ClampFiniteOrDefault(a_settings.obstacleShelterRadius,
		kObstacleShelterRadiusMin, kObstacleShelterRadiusMax, defaults.obstacleShelterRadius);
	a_settings.obstacleShelterDistance = ClampFiniteOrDefault(a_settings.obstacleShelterDistance,
		kObstacleShelterDistanceMin, kObstacleShelterDistanceMax, defaults.obstacleShelterDistance);
	a_settings.grassTransientFlutterStrength = ClampFiniteOrDefault(a_settings.grassTransientFlutterStrength,
		kGrassTransientFlutterStrengthMin, kGrassTransientFlutterStrengthMax,
		defaults.grassTransientFlutterStrength);
	a_settings.grassTransientFlutterFrequency = ClampFiniteOrDefault(a_settings.grassTransientFlutterFrequency,
		kGrassTransientFlutterFrequencyMin, kGrassTransientFlutterFrequencyMax,
		defaults.grassTransientFlutterFrequency);
	SanitizeGrassWindSettings(a_settings);
}

namespace OpenWind
{
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		WindSettings::GrassWindSpringQualityRange,
		textureSize,
		maxDistance)

	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
		WindSettings,
		enableTrunkBend,
		overrideTrunkWindIntensity,
		trunkWindIntensityOverride,
		trunkWindBendSensitivity,
		treeLeafBaseWindFlutterGain,
		treeWindGustScale,
		treeWindGustSoftLimit,
		treeWindSpringFrequency,
		treeWindSpringDamping,
		treeTransientSpringFrequency,
		treeTransientSpringDamping,
		windFieldGustScale,
		windFieldGustCrosswindScale,
		windFieldGustAmplitude,
		windFieldMicroTurbulenceStrength,
		windFieldFineBaselineAmplitude,
		windFieldFineWindCoupling,
		windFieldFineGustCoupling,
		windFieldFineFrequency,
		windFieldGustAdvectionMultiplier,
		windFieldGustAdvectionResponse,
		windFieldGustCoverage,
		windFieldGustEdgeSoftness,
		enableWindFieldGustDistortion,
		windFieldGustDistortionStrength,
		windFieldGustDistortionScale,
		windFieldGustDistortionSpeed,
		windFieldDirectionTransitionDuration,
		enableTerrainShelter,
		terrainShelterStrength,
		terrainShelterDistance,
		terrainShelterSoftness,
		terrainShelterRadius,
		enableObstacleShelter,
		obstacleShelterStrength,
		obstacleShelterRadius,
		obstacleShelterDistance,
		processMidRangeTransients,
		processFarRangeTransients,
		grassTransientBendStrength,
		grassTransientFlutterHalfLife,
		grassTransientFlutterStrength,
		grassTransientFlutterFrequency,
		enableAmbientGrassWind,
		enableGrassWindSpring,
		enableGrassWindSpringBend,
		grassWindResponse,
		grassWindSensitivity,
		grassWindMaximumTilt,
		grassWindCompressionToBend,
		grassWindSpringFrequency,
		grassWindSpringDamping,
		grassWindSpringQuality,
		grassWindFlutterStrength,
		grassWindFlutterFrequency,
		grassWindFlutterGustInfluence,
		grassWindFlutterWaveScale,
		grassWindFlutterAmplitudeResponse)
}

void Wind::SetTreeWindTestEnabled(bool a_enabled)
{
	if (runtimeState.treeWindTest.enabled == a_enabled)
		return;

	if (a_enabled) {
		runtimeState.treeWindTest.speed = ClampFiniteOrDefault(windFieldSelectedSpeed, 0.0f, 2.0f, 1.0f);
		runtimeState.treeWindTest.gustScale = ClampFiniteOrDefault(windFieldTuning.gustScale,
			kWindFieldGustScaleMin, kWindFieldGustScaleMax, settings.windFieldGustScale);
		runtimeState.treeWindTest.gustAmplitude = ClampFiniteOrDefault(windFieldTuning.gustAmplitude,
			kWindFieldGustAmplitudeMin, kWindFieldGustAmplitudeMax, settings.windFieldGustAmplitude);
		runtimeState.treeWindTest.gustAdvectionMultiplier = ClampFiniteOrDefault(
			windFieldTuning.gustAdvectionMultiplier,
			kWindFieldGustAdvectionMultiplierMin, kWindFieldGustAdvectionMultiplierMax, settings.windFieldGustAdvectionMultiplier);
	}
	runtimeState.treeWindTest.enabled = a_enabled;
}

void Wind::LoadSettings(json& o_json)
{
	const Settings previousSettings = settings;
	std::vector<json> previousEffects(windEffects.size());
	for (size_t index = 0; index < windEffects.size(); ++index)
		windEffects[index]->SaveSettings(previousEffects[index]);
	try {
		const Settings defaults{};
		settings = o_json;
		if (!o_json.contains("windFieldGustCrosswindScale")) {
			const float legacyGustScale = ClampFiniteOrDefault(settings.windFieldGustScale,
				kWindFieldGustScaleMin, kWindFieldGustScaleMax, defaults.windFieldGustScale);
			settings.windFieldGustCrosswindScale = legacyGustScale * WindField::WindTuning{}.frontAspectRatio;
		}
		if (!o_json.contains("grassWindSpringQuality")) {
			if (o_json.contains("grassWindSpringTextureSize") && o_json["grassWindSpringTextureSize"].is_number_unsigned()) {
				const auto legacyTextureSize = o_json["grassWindSpringTextureSize"].get<uint32_t>();
				for (auto& range : settings.grassWindSpringQuality)
					range.textureSize = legacyTextureSize;
			}
			if (o_json.contains("grassWindSpringWorldSize") && o_json["grassWindSpringWorldSize"].is_number()) {
				const float legacyWorldSize = o_json["grassWindSpringWorldSize"].get<float>();
				if (std::isfinite(legacyWorldSize))
					settings.grassWindSpringQuality.back().maxDistance = legacyWorldSize * 0.5f;
			}
		}
		if (!o_json.contains("grassWindSpringFrequency") && o_json.contains("grassWindSpringLag") &&
			o_json["grassWindSpringLag"].is_number()) {
			const float legacyLag = std::max(o_json["grassWindSpringLag"].get<float>(), 0.01f);
			settings.grassWindSpringFrequency = 0.25f / legacyLag;
		}
		if (!o_json.contains("grassWindSpringDamping"))
			settings.grassWindSpringDamping = defaults.grassWindSpringDamping;
		if (!o_json.contains("treeLeafBaseWindFlutterGain")) {
			if (o_json.contains("treeLeafWindSensitivity") && o_json["treeLeafWindSensitivity"].is_number())
				settings.treeLeafBaseWindFlutterGain =
					o_json["treeLeafWindSensitivity"].get<float>() * defaults.treeLeafBaseWindFlutterGain;
			else if (o_json.contains("treeLeafAmbientSensitivity") && o_json["treeLeafAmbientSensitivity"].is_number())
				settings.treeLeafBaseWindFlutterGain =
					o_json["treeLeafAmbientSensitivity"].get<float>() * defaults.treeLeafBaseWindFlutterGain;
		}
		const auto windEffectSettings = o_json.find("windEffects");
		const bool hasWindEffectSettings = windEffectSettings != o_json.end() && windEffectSettings->is_object();
		const json emptyEffectSettings = json::object();
		for (auto& effect : windEffects) {
			const std::string effectId(effect->GetId());
			if (hasWindEffectSettings) {
				const auto effectSettings = windEffectSettings->find(effectId);
				effect->LoadSettings(effectSettings != windEffectSettings->end() && effectSettings->is_object() ?
										 *effectSettings :
										 emptyEffectSettings);
			} else {
				effect->LoadSettings(o_json);
			}
		}
		SanitizeSettings(settings);
	} catch (...) {
		settings = previousSettings;
		for (size_t index = 0; index < windEffects.size(); ++index)
			windEffects[index]->LoadSettings(previousEffects[index]);
		throw;
	}
	for (size_t index = 0; index < windEffects.size(); ++index) {
		json currentEffect;
		windEffects[index]->SaveSettings(currentEffect);
		if (previousEffects[index].value("enabled", true) && !currentEffect.value("enabled", true))
			windEffects[index]->Reset();
	}
	settingsConfigured = true;
}

void Wind::SaveSettings(json& o_json)
{
	SanitizeSettings(settings);
	o_json = settings;
	json windEffectSettings = json::object();
	for (const auto& effect : windEffects) {
		json effectSettings;
		effect->SaveSettings(effectSettings);
		windEffectSettings[std::string(effect->GetId())] = std::move(effectSettings);
	}
	o_json["windEffects"] = std::move(windEffectSettings);
}

void Wind::RestoreDefaultSettings()
{
	settings = {};
	for (auto& effect : windEffects)
		effect->RestoreDefaultSettings();
}

uint32_t Wind::SanitizeGrassWindSpringTextureSize(uint32_t a_textureSize)
{
	return *std::min_element(kGrassWindSpringTextureSizes.begin(), kGrassWindSpringTextureSizes.end(),
		[a_textureSize](uint32_t a_left, uint32_t a_right) {
			return std::abs(static_cast<int64_t>(a_left) - a_textureSize) <
		           std::abs(static_cast<int64_t>(a_right) - a_textureSize);
		});
}

void Wind::SanitizeGrassWindSettings(Settings& a_settings)
{
	const Settings defaults{};
	a_settings.trunkWindIntensityOverride = ClampFiniteOrDefault(a_settings.trunkWindIntensityOverride, kTrunkWindIntensityMin, kTrunkWindIntensityMax, defaults.trunkWindIntensityOverride);
	a_settings.grassTransientBendStrength = ClampFiniteOrDefault(a_settings.grassTransientBendStrength, kGrassWindResponseMin, kGrassWindResponseMax, defaults.grassTransientBendStrength);
	a_settings.grassTransientFlutterHalfLife = ClampFiniteOrDefault(a_settings.grassTransientFlutterHalfLife, kGrassTransientFlutterHalfLifeMin, kGrassTransientFlutterHalfLifeMax, defaults.grassTransientFlutterHalfLife);
	a_settings.grassWindResponse = ClampFiniteOrDefault(a_settings.grassWindResponse, kGrassWindResponseMin, kGrassWindResponseMax, defaults.grassWindResponse);
	a_settings.grassWindSensitivity = ClampFiniteOrDefault(a_settings.grassWindSensitivity, kGrassWindSensitivityMin, kGrassWindSensitivityMax, defaults.grassWindSensitivity);
	a_settings.grassWindMaximumTilt = ClampFiniteOrDefault(a_settings.grassWindMaximumTilt, kGrassWindMaximumTiltMin, kGrassWindMaximumTiltMax, defaults.grassWindMaximumTilt);
	a_settings.grassWindCompressionToBend = ClampFiniteOrDefault(a_settings.grassWindCompressionToBend, kGrassWindCompressionToBendMin, kGrassWindCompressionToBendMax, defaults.grassWindCompressionToBend);
	a_settings.grassWindSpringFrequency = ClampFiniteOrDefault(a_settings.grassWindSpringFrequency, kGrassWindSpringFrequencyMin, kGrassWindSpringFrequencyMax, defaults.grassWindSpringFrequency);
	a_settings.grassWindSpringDamping = ClampFiniteOrDefault(a_settings.grassWindSpringDamping, kGrassWindSpringDampingMin, kGrassWindSpringDampingMax, defaults.grassWindSpringDamping);
	for (uint32_t index = 0; index < kGrassWindSpringQualityRangeCount; ++index) {
		a_settings.grassWindSpringQuality[index].textureSize =
			SanitizeGrassWindSpringTextureSize(a_settings.grassWindSpringQuality[index].textureSize);
		a_settings.grassWindSpringQuality[index].maxDistance = ClampFiniteOrDefault(
			a_settings.grassWindSpringQuality[index].maxDistance,
			kGrassWindSpringDistanceMin,
			kGrassWindSpringDistanceMax,
			defaults.grassWindSpringQuality[index].maxDistance);
		if (index > 0)
			a_settings.grassWindSpringQuality[index].maxDistance =
				std::max(a_settings.grassWindSpringQuality[index].maxDistance,
					a_settings.grassWindSpringQuality[index - 1].maxDistance);
	}
	a_settings.grassWindFlutterStrength = ClampFiniteOrDefault(a_settings.grassWindFlutterStrength, kGrassWindFlutterStrengthMin, kGrassWindFlutterStrengthMax, defaults.grassWindFlutterStrength);
	a_settings.grassWindFlutterFrequency = ClampFiniteOrDefault(a_settings.grassWindFlutterFrequency, kGrassWindFlutterFrequencyMin, kGrassWindFlutterFrequencyMax, defaults.grassWindFlutterFrequency);
	a_settings.grassWindFlutterGustInfluence = ClampFiniteOrDefault(a_settings.grassWindFlutterGustInfluence, kGrassWindFlutterGustInfluenceMin, kGrassWindFlutterGustInfluenceMax, defaults.grassWindFlutterGustInfluence);
	a_settings.grassWindFlutterWaveScale = ClampFiniteOrDefault(a_settings.grassWindFlutterWaveScale, kGrassWindFlutterWaveScaleMin, kGrassWindFlutterWaveScaleMax, defaults.grassWindFlutterWaveScale);
	for (uint32_t index = 0; index < a_settings.grassWindFlutterAmplitudeResponse.size(); ++index)
		a_settings.grassWindFlutterAmplitudeResponse[index] = ClampFiniteOrDefault(
			a_settings.grassWindFlutterAmplitudeResponse[index], kGrassWindFlutterAmplitudeResponseMin, kGrassWindFlutterAmplitudeResponseMax,
			defaults.grassWindFlutterAmplitudeResponse[index]);
}

bool Wind::ImportSettings(json& a_settings)
{
	if (settingsConfigured)
		return false;
	LoadSettings(a_settings);
	return true;
}
