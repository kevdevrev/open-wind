#pragma once

#include "OpenWind/TransientWindImpulse.h"
#include <nlohmann/json.hpp>

struct FusRoDahWindSettings
{
	bool enabled = true;
	float intensity = 1.0f;
	float decayTime = 1.49f;
	float distanceMultiplier = 3.0f;
	float widthMultiplier = 1.61f;
	float speedMultiplier = 1.0f;
};

struct DragonWindImpactProfile
{
	float strength = 1.0f;
	float distance = 1400.0f;
	float waveHalfWidth = 240.0f;
	float propagationSpeed = 1800.0f;
};

struct DragonWindSettings
{
	bool enabled = true;
	bool wingbeatsEnabled = true;
	bool landingsEnabled = true;
	bool crashesEnabled = true;
	bool shoutsEnabled = true;
	float strength = 1.0f;
	float trackingDistance = 12000.0f;
	float wingbeatStrength = 1.6f;
	float wingbeatDistance = 2392.0f;
	float wingbeatWaveHalfWidth = 238.0f;
	float wingbeatPropagationSpeed = 2200.0f;
	float wingbeatDecayTime = 1.12f;
	float wingbeatFallbackCooldown = 0.10f;
	DragonWindImpactProfile normalImpact{ 1.5f, 1400.0f, 240.0f, 1800.0f };
	DragonWindImpactProfile forcefulImpact{ 1.8f, 2200.0f, 360.0f, 2200.0f };
	DragonWindImpactProfile crashImpact{ 3.0f, 3200.0f, 500.0f, 2600.0f };
	float impactDecayTime = 2.0f;
	float impactDeduplicationTime = 0.3f;
	float breathStrength = 1.5f;
	float breathDistance = 2100.0f;
	float breathConeHalfAngle = 32.0f;
	float breathReleaseTime = 0.5f;
	bool wakeEnabled = true;
	float wakeStrength = 5.0f;
	float wakeLength = 2999.74f;
	float wakeHalfWidth = 1965.03f;
	float wakeDownwardReach = 1875.61f;
	float wakeFullSizeSpeed = 3012.52f;
	float wakeMaximumScale = 1.5f;
	float wakeEdgeSoftness = 0.2f;
	float wakeOutwardFlow = 0.8f;
	float wakeDownwash = 1.46f;
	bool soulAbsorptionEnabled = true;
	float soulAbsorptionStrength = 2.0f;
	float soulAbsorptionRadius = 600.0f;
	float soulAbsorptionFadeTime = 0.5f;
	float wingbeatStartRadius = 0.0f;
	float wingbeatEndRadius = 2850.6746f;
};

struct SpellShoutWindRouterSettings
{
	bool enabled = true;
	float strength = 1.0f;
	bool whirlwindSprint = true;
	bool cyclone = true;
	bool elementalBreath = true;
	bool masterDestruction = true;
};

struct ProjectileMagicWindRouterSettings
{
	bool enabled = true;
	float launchStrength = 0.7f;
	float impactStrength = 0.8f;
	float concentrationStrength = 1.5f;
	float concentrationReleaseTime = 0.35f;
	float dragonStrength = 1.35f;
	float radiusMultiplier = 1.0f;
	float decayTime = 0.65f;
};

struct ExplosionWindRouterSettings
{
	bool enabled = true;
	float intensity = 1.0f;
	float radiusMultiplier = 1.0f;
	float decayTime = 0.9f;
};

struct StormCallWindRouterSettings
{
	bool enabled = true;
	float strength = 1.0f;
	float radius = 900.0f;
	float decayTime = 0.8f;
};

struct HeavyImpactWindRouterRadialProfile
{
	float strength;
	float distance;
	float waveHalfWidth;
	float propagationSpeed;
	float decayTime;
};

struct HeavyImpactWindRouterDirectionalProfile
{
	float strength;
	float distance;
	float waveHalfWidth;
	float propagationSpeed;
	float coneHalfAngle;
	float decayTime;
};

struct HeavyImpactWindRouterSettings
{
	bool enabled = true;
	float strength = 1.0f;
	float trackingDistance = 12000.0f;
	float deduplicationTime = 0.35f;
	bool powerAttacks = true;
	HeavyImpactWindRouterDirectionalProfile powerAttack{ 0.9f, 1000.0f, 200.0f, 2800.0f, 40.0f, 0.65f };
	HeavyImpactWindRouterRadialProfile giantClub{ 2.5f, 2200.0f, 360.0f, 2200.0f, 1.4f };
	HeavyImpactWindRouterRadialProfile dragonGroundAttack{ 2.2f, 2000.0f, 340.0f, 2200.0f, 1.2f };
	HeavyImpactWindRouterRadialProfile trollPowerAttack{ 1.4f, 1300.0f, 260.0f, 1800.0f, 1.0f };
	HeavyImpactWindRouterRadialProfile mammothHeavyAttack{ 2.0f, 1900.0f, 320.0f, 2000.0f, 1.2f };
	HeavyImpactWindRouterRadialProfile centurionHeavyAttack{ 2.0f, 1800.0f, 300.0f, 2100.0f, 1.1f };
	HeavyImpactWindRouterRadialProfile beastTransformation{ 1.8f, 1700.0f, 320.0f, 1900.0f, 1.3f };
	HeavyImpactWindRouterDirectionalProfile shieldCharge{ 1.3f, 1100.0f, 220.0f, 2800.0f, 45.0f, 0.7f };
};

struct WeaponThrowVRWindSettings
{
	bool enabled = true;
	float strength = 1.0f;
	float minimumSpeed = 150.0f;
	float fullStrengthSpeed = 1800.0f;
	float radius = 220.0f;
	float bowLength = 320.0f;
	float wakeLength = 900.0f;
	float launchStrength = 0.65f;
	float impactStrength = 0.8f;
	float decayTime = 0.6f;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DragonWindImpactProfile,
	strength,
	distance,
	waveHalfWidth,
	propagationSpeed)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HeavyImpactWindRouterDirectionalProfile,
	strength,
	distance,
	waveHalfWidth,
	propagationSpeed,
	coneHalfAngle,
	decayTime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HeavyImpactWindRouterRadialProfile,
	strength,
	distance,
	waveHalfWidth,
	propagationSpeed,
	decayTime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FusRoDahWindSettings,
	enabled,
	intensity,
	decayTime,
	distanceMultiplier,
	widthMultiplier,
	speedMultiplier)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	DragonWindSettings,
	enabled,
	wingbeatsEnabled,
	landingsEnabled,
	crashesEnabled,
	shoutsEnabled,
	strength,
	trackingDistance,
	wingbeatStrength,
	wingbeatDistance,
	wingbeatWaveHalfWidth,
	wingbeatPropagationSpeed,
	wingbeatDecayTime,
	wingbeatFallbackCooldown,
	normalImpact,
	forcefulImpact,
	crashImpact,
	impactDecayTime,
	impactDeduplicationTime,
	breathStrength,
	breathDistance,
	breathConeHalfAngle,
	breathReleaseTime,
	wakeEnabled,
	wakeStrength,
	wakeLength,
	wakeHalfWidth,
	wakeDownwardReach,
	wakeFullSizeSpeed,
	wakeMaximumScale,
	wakeEdgeSoftness,
	wakeOutwardFlow,
	wakeDownwash,
	soulAbsorptionEnabled,
	soulAbsorptionStrength,
	soulAbsorptionRadius,
	soulAbsorptionFadeTime,
	wingbeatStartRadius,
	wingbeatEndRadius)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SpellShoutWindRouterSettings,
	enabled,
	strength,
	whirlwindSprint,
	cyclone,
	elementalBreath,
	masterDestruction)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ProjectileMagicWindRouterSettings,
	enabled,
	launchStrength,
	impactStrength,
	concentrationStrength,
	concentrationReleaseTime,
	dragonStrength,
	radiusMultiplier,
	decayTime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ExplosionWindRouterSettings,
	enabled,
	intensity,
	radiusMultiplier,
	decayTime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	StormCallWindRouterSettings,
	enabled,
	strength,
	radius,
	decayTime)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HeavyImpactWindRouterSettings,
	enabled,
	strength,
	trackingDistance,
	deduplicationTime,
	powerAttacks,
	powerAttack,
	giantClub,
	dragonGroundAttack,
	trollPowerAttack,
	mammothHeavyAttack,
	centurionHeavyAttack,
	beastTransformation,
	shieldCharge)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	WeaponThrowVRWindSettings,
	enabled,
	strength,
	minimumSpeed,
	fullStrengthSpeed,
	radius,
	bowLength,
	wakeLength,
	launchStrength,
	impactStrength,
	decayTime)

namespace FusRoDahWindLimits
{
	inline constexpr float kDecayTimeMax = WindField::kTransientImpulseMaximumDecayTime;
	inline constexpr float kDecayTimeMin = 0.0f;
	inline constexpr float kDistanceMultiplierMax = 3.0f;
	inline constexpr float kDistanceMultiplierMin = 0.25f;
	inline constexpr float kIntensityMax = 5.0f;
	inline constexpr float kIntensityMin = 0.0f;
	inline constexpr float kSpeedMultiplierMax = 3.0f;
	inline constexpr float kSpeedMultiplierMin = 0.25f;
	inline constexpr float kWidthMultiplierMax = 3.0f;
	inline constexpr float kWidthMultiplierMin = 0.25f;
}

namespace DragonWindLimits
{
	inline constexpr float kBreathAngleMin = 1.0f;
	inline constexpr float kBreathAngleMax = 89.0f;
	inline constexpr float kWingbeatRadiusMin = 0.0f;
	inline constexpr float kWakeDimensionMin = 100.0f;
	inline constexpr float kWakeDimensionMax = 10000.0f;
	inline constexpr float kWakeScaleMin = 1.0f;
	inline constexpr float kWakeScaleMax = 4.0f;
	inline constexpr float kWakeEdgeMin = 0.01f;
	inline constexpr float kWakeEdgeMax = 0.49f;
	inline constexpr float kWakeFlowMin = 0.0f;
	inline constexpr float kWakeFlowMax = 3.0f;
	inline constexpr float kDistanceMax = 30000.0f;
	inline constexpr float kDistanceMin = 100.0f;
	inline constexpr float kSpeedMax = 10000.0f;
	inline constexpr float kSpeedMin = 0.0f;
	inline constexpr float kStrengthMax = 5.0f;
	inline constexpr float kStrengthMin = 0.0f;
	inline constexpr float kTimeMax = WindField::kTransientImpulseMaximumDecayTime;
	inline constexpr float kTimeMin = 0.0f;
}

namespace SpellShoutWindRouterLimits
{
	inline constexpr float kStrengthMaximum = 5.0f;
	inline constexpr float kStrengthMinimum = 0.0f;
}

namespace ProjectileMagicWindRouterLimits
{
	inline constexpr float kMaximumRadiusMultiplier = 3.0f;
	inline constexpr float kMaximumStrength = 5.0f;
	inline constexpr float kMinimumRadiusMultiplier = 0.25f;
	inline constexpr float kMinimumStrength = 0.0f;
}

namespace ExplosionWindRouterLimits
{
	inline constexpr float kIntensityMaximum = 5.0f;
	inline constexpr float kIntensityMinimum = 0.0f;
	inline constexpr float kRadiusMultiplierMaximum = 3.0f;
	inline constexpr float kRadiusMultiplierMinimum = 0.25f;
}

namespace StormCallWindRouterLimits
{
	inline constexpr float kMaximumRadius = 2400.0f;
	inline constexpr float kMaximumStrength = 5.0f;
	inline constexpr float kMinimumRadius = 256.0f;
	inline constexpr float kMinimumStrength = 0.0f;
}

namespace HeavyImpactWindRouterLimits
{
	inline constexpr float kMaximumDistance = 30000.0f;
	inline constexpr float kMaximumStrength = 10.0f;
	inline constexpr float kMaximumTime = WindField::kTransientImpulseMaximumDecayTime;
	inline constexpr float kMinimumDistance = 50.0f;
}

namespace WeaponThrowVRWindLimits
{
	inline constexpr float kMaximumDistance = 3000.0f;
	inline constexpr float kMaximumSpeed = 6000.0f;
	inline constexpr float kMaximumStrength = 5.0f;
	inline constexpr float kMinimumDistance = 32.0f;
	inline constexpr float kMinimumSpeed = 0.0f;
	inline constexpr float kMinimumStrength = 0.0f;
}
