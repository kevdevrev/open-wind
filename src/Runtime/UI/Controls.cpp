#include "Controls.h"

#include "Runtime/WindEffectSettings.h"

#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace
{
	using json = nlohmann::json;

	struct EffectControls
	{
		std::string prefix;

		json Label(const char* key, const char* text) const
		{
			return { { "key", prefix + key }, { "text", text } };
		}

		json Toggle(const char* path, const char* key, const char* text) const
		{
			return { { "type", "checkbox" }, { "path", path }, { "label", Label(key, text) } };
		}

		json Slider(const char* path, const char* key, const char* text, float minimum, float maximum,
			const char* format = "%.2fx", bool logarithmic = false) const
		{
			return { { "type", "float" }, { "path", path }, { "label", Label(key, text) },
				{ "min", minimum }, { "max", maximum }, { "format", format }, { "logarithmic", logarithmic } };
		}

		json Group(const char* key, const char* text, json controls, bool defaultOpen = false, bool tree = false) const
		{
			return { { "type", "group" }, { "label", Label(key, text) }, { "controls", std::move(controls) },
				{ "collapsible", true }, { "defaultOpen", defaultOpen }, { "tree", tree } };
		}

		json Text(const char* key, const char* text, bool disabled = false) const
		{
			return { { "type", "text" }, { "label", Label(key, text) }, { "disabled", disabled } };
		}

		json Tip(json control, const char* key, const char* text) const
		{
			control["tooltip"] = Label(key, text);
			return control;
		}

		json Tab(const char* id, const char* name, json controls) const
		{
			return { { "id", id }, { "label", Label("name", name) }, { "sceneEditable", false },
				{ "controls", std::move(controls) } };
		}
	};

	json Unchecked(const char* path)
	{
		return { { "path", path }, { "equals", false } };
	}

	json Disabled(json condition, json controls)
	{
		return { { "type", "group" }, { "disabledWhen", std::move(condition) }, { "controls", std::move(controls) } };
	}

	json FusRoDahControls()
	{
		using namespace FusRoDahWindLimits;
		const EffectControls ui{ "feature.wind.wind_effect.fus_ro_dah." };
		return ui.Tab("fusRoDah", "Fus Ro Dah", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Wind Impulse"), "enabled_tooltip",
				"Adds Unrelenting Force as a directional wave traveling through the shared wind field."),
			Disabled(Unchecked("/enabled"), {
				ui.Tip(ui.Slider("/intensity", "intensity", "Intensity", kIntensityMin, kIntensityMax), "intensity_tooltip",
					"Scales the rank-specific wind velocity added at the moving pressure wave."),
				ui.Tip(ui.Slider("/decayTime", "decay_time", "Trailing Falloff", kDecayTimeMin, kDecayTimeMax, "%.2f s"), "decay_time_tooltip",
					"Controls how quickly pressure fades behind the moving front. Zero removes the extra trailing falloff."),
				ui.Tip(ui.Slider("/distanceMultiplier", "distance_multiplier", "Propagation Distance", kDistanceMultiplierMin, kDistanceMultiplierMax), "distance_multiplier_tooltip",
					"Scales the rank-specific distance the pressure wave can travel."),
				ui.Tip(ui.Slider("/widthMultiplier", "width_multiplier", "Wave Width", kWidthMultiplierMin, kWidthMultiplierMax), "width_multiplier_tooltip",
					"Scales the moving pressure-front thickness and the soft falloff surrounding its oval core."),
				ui.Tip(ui.Slider("/speedMultiplier", "speed_multiplier", "Propagation Speed", kSpeedMultiplierMin, kSpeedMultiplierMax), "speed_multiplier_tooltip",
					"Scales how quickly the wavefront travels through the shared wind field.") }) });
	}

	json DragonImpactControls(const EffectControls& ui, const char* path, const char* key, const char* name, const char* enabled)
	{
		using namespace DragonWindLimits;
		const std::string base(path);
		return ui.Group(key, name, { Disabled(Unchecked(enabled), {
			ui.Slider((base + "/strength").c_str(), "impact_strength", "Strength", kStrengthMin, kStrengthMax),
			ui.Slider((base + "/distance").c_str(), "impact_distance", "Travel Distance", kDistanceMin, kDistanceMax, "%.0f units", true),
			ui.Slider((base + "/waveHalfWidth").c_str(), "impact_width", "Wave Half-Width", kDistanceMin, kDistanceMax, "%.0f units", true),
			ui.Slider((base + "/propagationSpeed").c_str(), "impact_speed", "Propagation Speed", kSpeedMin, kSpeedMax, "%.0f units/s") }) }, false, true);
	}

	json DragonControls()
	{
		using namespace DragonWindLimits;
		const EffectControls ui{ "feature.wind.wind_effect.dragon." };
		auto startRadius = ui.Tip(ui.Slider("/wingbeatStartRadius", "wingbeat_start_radius", "Start Radius", kWingbeatRadiusMin, kDistanceMax, "%.0f units"),
			"wingbeat_start_radius_tooltip", "Pulse radius at the dragon. Zero starts at a point.");
		startRadius["onChangeClampMinimum"] = { "/wingbeatEndRadius" };
		auto endRadius = ui.Tip(ui.Slider("/wingbeatEndRadius", "wingbeat_end_radius", "End Radius", kWingbeatRadiusMin, kDistanceMax, "%.0f units"),
			"wingbeat_end_radius_tooltip", "Pulse radius at Travel Distance. Radius grows linearly from Start Radius; equal radii form a cylinder. Wave Half-Width controls the pulse thickness along its travel direction.");
		endRadius["minimumPath"] = "/wingbeatStartRadius";
		return ui.Tab("dragon", "Dragons", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Dragon Wind"), "enabled_tooltip",
				"Adds dragon wingbeats, flight wakes, impacts, breath, and soul absorption to the shared wind field for vegetation and connected physics mods."),
			Disabled(Unchecked("/enabled"), {
				ui.Slider("/strength", "strength", "Master Strength", kStrengthMin, kStrengthMax),
				ui.Slider("/trackingDistance", "tracking_distance", "Tracking Distance", kDistanceMin, kDistanceMax, "%.0f units", true),
				ui.Group("wake_header", "Flight Wake", {
					ui.Toggle("/wakeEnabled", "wake_enabled", "Enable Moving Flight and Crash Wake"),
					Disabled(Unchecked("/wakeEnabled"), {
						ui.Slider("/wakeStrength", "wake_strength", "Strength", kStrengthMin, kStrengthMax),
						ui.Slider("/wakeLength", "wake_length", "Length", kWakeDimensionMin, kWakeDimensionMax, "%.0f units"),
						ui.Slider("/wakeHalfWidth", "wake_width", "Maximum Half-Width", kWakeDimensionMin, kWakeDimensionMax, "%.0f units"),
						ui.Slider("/wakeDownwardReach", "wake_reach", "Downward Reach", kWakeDimensionMin, kWakeDimensionMax, "%.0f units"),
						ui.Slider("/wakeFullSizeSpeed", "wake_full_size_speed", "Full Size Speed", kWakeDimensionMin, kWakeDimensionMax, "%.0f units/s"),
						ui.Slider("/wakeMaximumScale", "wake_maximum_scale", "Maximum Size Multiplier", kWakeScaleMin, kWakeScaleMax),
						ui.Slider("/wakeEdgeSoftness", "wake_edge", "Soft Edge Fraction", kWakeEdgeMin, kWakeEdgeMax, "%.2f"),
						ui.Slider("/wakeOutwardFlow", "wake_outward", "Outward Flow", kWakeFlowMin, kWakeFlowMax, "%.2f"),
						ui.Slider("/wakeDownwash", "wake_downwash", "Downwash", kWakeFlowMin, kWakeFlowMax, "%.2f") }),
					ui.Text("wake_hint", "A rounded wake trails movement, banks with the wings, and weakens downstream. Dimensions grow with speed and force grows with speed squared, reaching their maximum at Full Size Speed. Downward Reach slopes the wake toward foliage.") }),
				ui.Group("wingbeats", "Wingbeat Pulses", {
					ui.Toggle("/wingbeatsEnabled", "wingbeats_enabled", "Enable Wingbeat Pulses"),
					Disabled(Unchecked("/wingbeatsEnabled"), {
						ui.Slider("/wingbeatStrength", "wingbeat_strength", "Strength", kStrengthMin, kStrengthMax),
						ui.Slider("/wingbeatDistance", "wingbeat_distance", "Travel Distance", kDistanceMin, kDistanceMax, "%.0f units", true),
						std::move(startRadius), std::move(endRadius),
						ui.Slider("/wingbeatWaveHalfWidth", "wingbeat_width", "Wave Half-Width", kDistanceMin, kDistanceMax, "%.0f units", true),
						ui.Slider("/wingbeatPropagationSpeed", "wingbeat_speed", "Propagation Speed", kSpeedMin, kSpeedMax, "%.0f units/s"),
						ui.Slider("/wingbeatDecayTime", "wingbeat_decay", "Trailing Falloff", kTimeMin, kTimeMax, "%.2f s"),
						ui.Slider("/wingbeatFallbackCooldown", "wingbeat_fallback_cooldown", "Sound Fallback Cooldown", kTimeMin, kTimeMax, "%.2f s") }) }, true),
				ui.Group("impacts", "Landing and Crash Shockwaves", {
					ui.Toggle("/landingsEnabled", "landings_enabled", "Enable Landing Shockwaves"),
					ui.Toggle("/crashesEnabled", "crashes_enabled", "Enable Crash Shockwaves"),
					DragonImpactControls(ui, "/normalImpact", "normal_landing", "Normal Landing", "/landingsEnabled"),
					DragonImpactControls(ui, "/forcefulImpact", "forceful_landing", "Forceful Landing", "/landingsEnabled"),
					DragonImpactControls(ui, "/crashImpact", "crash_landing", "Crash Landing", "/crashesEnabled"),
					Disabled({ { "all", { Unchecked("/landingsEnabled"), Unchecked("/crashesEnabled") } } }, {
						ui.Slider("/impactDecayTime", "impact_decay", "Trailing Falloff", kTimeMin, kTimeMax, "%.2f s"),
						ui.Slider("/impactDeduplicationTime", "impact_deduplication", "Event Deduplication Window", kTimeMin, kTimeMax, "%.2f s") }) }, true),
				ui.Toggle("/shoutsEnabled", "shouts_enabled", "Enable Shout and Breath Forces"),
				Disabled(Unchecked("/shoutsEnabled"), {
					ui.Tip(ui.Slider("/breathStrength", "breath_strength", "Breath Strength", kStrengthMin, kStrengthMax), "breath_strength_tooltip",
						"Continuous wind follows the dragon's breath while casting. Multiplied by Master Strength and shared with vegetation and connected physics mods."),
					ui.Slider("/breathDistance", "breath_distance", "Breath Reach", kDistanceMin, kDistanceMax, "%.0f units", true),
					ui.Slider("/breathConeHalfAngle", "breath_angle", "Breath Cone Half-Angle", kBreathAngleMin, kBreathAngleMax, "%.0f degrees"),
					ui.Tip(ui.Slider("/breathReleaseTime", "breath_release", "Breath Release Time", kTimeMin, kTimeMax, "%.2f s"), "breath_release_tooltip",
						"Fades the remaining wind at its last position after the dragon stops breathing. Zero stops immediately.") }),
				ui.Group("soul_header", "Soul Absorption", {
					ui.Toggle("/soulAbsorptionEnabled", "soul_enabled", "Enable Soul Absorption Wind"),
					Disabled(Unchecked("/soulAbsorptionEnabled"), {
						ui.Slider("/soulAbsorptionStrength", "soul_strength", "Soul Wind Strength", kStrengthMin, kStrengthMax),
						ui.Slider("/soulAbsorptionRadius", "soul_radius", "Soul Wind Radius", kDistanceMin, kDistanceMax, "%.0f units"),
						ui.Slider("/soulAbsorptionFadeTime", "soul_fade", "Soul Wind Fade Time", kTimeMin, kTimeMax, "%.2f s") }),
					ui.Text("soul_hint", "Wind flows from the dragon toward the player while its directed soul absorption effect is active. Radius controls the width of the flow.") }) }) });
	}

	json SpellShoutControls()
	{
		using namespace SpellShoutWindRouterLimits;
		const EffectControls ui{ "feature.wind.wind_effect.spell_shout." };
		return ui.Tab("spellShout", "Spells and Shouts", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Spell and Shout Wind"), "enabled_tooltip",
				"Adds focused transient wind to major shouts and master Destruction spell activations."),
			Disabled(Unchecked("/enabled"), {
				ui.Slider("/strength", "strength", "Master Strength", kStrengthMinimum, kStrengthMaximum),
				ui.Toggle("/whirlwindSprint", "whirlwind_sprint", "Whirlwind Sprint"),
				ui.Toggle("/cyclone", "cyclone", "Cyclone"),
				ui.Toggle("/elementalBreath", "elemental_breath", "Fire and Frost Breath"),
				ui.Toggle("/masterDestruction", "master_destruction", "Master Destruction Activations") }) });
	}

	json ProjectileControls()
	{
		using namespace ProjectileMagicWindRouterLimits;
		const EffectControls ui{ "feature.wind.wind_effect.projectile_magic." };
		return ui.Tab("projectileMagic", "Projectile Magic", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Projectile Wind"), "enabled_tooltip",
				"Adds launch and impact wind to projectile spells, plus continuous wind for aimed concentration spells such as Flames. Includes mod-added magic."),
			Disabled(Unchecked("/enabled"), {
				ui.Slider("/launchStrength", "launch", "Launch Strength", kMinimumStrength, kMaximumStrength),
				ui.Slider("/impactStrength", "impact", "Impact Strength", kMinimumStrength, kMaximumStrength),
				ui.Tip(ui.Slider("/concentrationStrength", "concentration_strength", "Concentration Strength", kMinimumStrength, kMaximumStrength), "concentration_strength_tooltip",
					"Scales continuous wind from each casting hand. Follows the spell's aim and range; zero disables the stream. Dragon breath has separate controls."),
				ui.Tip(ui.Slider("/concentrationReleaseTime", "concentration_release", "Concentration Release Time", 0.0f, WindField::kTransientImpulseMaximumDecayTime, "%.2f s"), "concentration_release_tooltip",
					"Fades the stream at its last position after a 0.1-second grace period without casting updates. Zero stops after that grace period."),
				ui.Slider("/dragonStrength", "dragon", "Dragon Strength", kMinimumStrength, kMaximumStrength),
				ui.Tip(ui.Slider("/radiusMultiplier", "radius", "Radius Multiplier", kMinimumRadiusMultiplier, kMaximumRadiusMultiplier), "radius_tooltip",
					"Scales impact radius and the concentration stream's initial radius. Stream reach and cone opening remain unchanged."),
				ui.Slider("/decayTime", "decay", "Trailing Falloff", 0.0f, WindField::kTransientImpulseMaximumDecayTime, "%.2f s") }) });
	}

	json ExplosionControls()
	{
		using namespace ExplosionWindRouterLimits;
		const EffectControls ui{ "feature.wind.wind_effect.explosions." };
		return ui.Tab("explosions", "Explosive Magic", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Explosion Wind"), "enabled_tooltip",
				"Adds radial pressure waves for record-backed magic explosions that hit a reference."),
			Disabled(Unchecked("/enabled"), {
				ui.Slider("/intensity", "intensity", "Intensity", kIntensityMinimum, kIntensityMaximum),
				ui.Slider("/radiusMultiplier", "radius", "Radius", kRadiusMultiplierMinimum, kRadiusMultiplierMaximum),
				ui.Slider("/decayTime", "decay", "Trailing Falloff", 0.0f, WindField::kTransientImpulseMaximumDecayTime, "%.2f s") }) });
	}

	json StormCallControls()
	{
		using namespace StormCallWindRouterLimits;
		const EffectControls ui{ "feature.wind.wind_effect.storm_call." };
		return ui.Tab("stormCall", "Storm Call Strikes", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Storm Call Wind"), "enabled_tooltip",
				"Adds a radial pressure wave at each exact Storm Call lightning impact."),
			Disabled(Unchecked("/enabled"), {
				ui.Slider("/strength", "strength", "Strength", kMinimumStrength, kMaximumStrength),
				ui.Slider("/radius", "radius", "Radius", kMinimumRadius, kMaximumRadius, "%.0f"),
				ui.Slider("/decayTime", "decay", "Trailing Falloff", 0.0f, WindField::kTransientImpulseMaximumDecayTime, "%.2f s") }) });
	}

	json HeavyImpactControls()
	{
		using namespace HeavyImpactWindRouterLimits;
		const EffectControls ui{ "feature.wind.wind_effect.heavy_impacts." };
		return ui.Tab("heavyImpacts", "Heavy Impacts", {
			ui.Tip(ui.Toggle("/enabled", "enabled", "Enable Heavy Impacts"), "enabled_tooltip",
				"Adds pressure waves for power attacks, giant, dragon, troll, mammoth, Centurion, transformation, and Shield Charge events."),
			Disabled(Unchecked("/enabled"), {
				ui.Toggle("/powerAttacks", "power_attacks", "Power Attack Impacts"),
				Disabled(Unchecked("/powerAttacks"), {
					ui.Slider("/powerAttack/strength", "power_attack_strength", "Power Attack Strength", 0.0f, kMaximumStrength) }),
				ui.Slider("/strength", "strength", "Master Strength", 0.0f, kMaximumStrength),
				ui.Slider("/trackingDistance", "tracking_distance", "Tracking Distance", kMinimumDistance, kMaximumDistance, "%.0f units", true),
				ui.Slider("/deduplicationTime", "deduplication", "Deduplication Window", 0.0f, kMaximumTime, "%.2f s") }) });
	}

	json WeaponThrowControls()
	{
		using namespace WeaponThrowVRWindLimits;
		const EffectControls ui{ "feature.wind.wind_effect.weapon_throw_vr." };
		auto status = ui.Text("not_detected", "WeaponThrowVR.esp not detected", true);
		status["visibleWhen"] = { { "available", false } };
		auto minimumSpeed = ui.Slider("/minimumSpeed", "minimum_speed", "Minimum Speed", kMinimumSpeed, kMaximumSpeed, "%.0f units/s");
		minimumSpeed["onChangeClampMinimum"] = { "/fullStrengthSpeed" };
		auto fullSpeed = ui.Slider("/fullStrengthSpeed", "full_speed", "Full-Strength Speed", kMinimumSpeed, kMaximumSpeed, "%.0f units/s");
		fullSpeed["minimumPath"] = "/minimumSpeed";
		auto tab = ui.Tab("weaponThrowVR", "Weapon Throw VR", {
			std::move(status), ui.Toggle("/enabled", "enabled", "Enable Thrown Weapon Wind"),
			Disabled({ { "any", { Unchecked("/enabled"), json{ { "available", false } } } } }, {
				ui.Slider("/strength", "strength", "Master Strength", kMinimumStrength, kMaximumStrength),
				std::move(minimumSpeed), std::move(fullSpeed),
				ui.Slider("/radius", "radius", "Crosswind Radius", kMinimumDistance, kMaximumDistance, "%.0f units"),
				ui.Slider("/bowLength", "bow", "Bow Length", kMinimumDistance, kMaximumDistance, "%.0f units"),
				ui.Slider("/wakeLength", "wake", "Wake Length", kMinimumDistance, kMaximumDistance, "%.0f units"),
				ui.Slider("/launchStrength", "launch", "Launch Pulse", kMinimumStrength, kMaximumStrength),
				ui.Slider("/impactStrength", "impact", "Impact Pulse", kMinimumStrength, kMaximumStrength),
				ui.Slider("/decayTime", "decay", "Pulse Falloff", 0.0f, WindField::kTransientImpulseMaximumDecayTime, "%.2f s") }) });
		tab["availabilityAction"] = { { "action", "effectAvailable" }, { "id", "weaponThrowVR" } };
		return tab;
	}

	const std::string& WindEffectControls()
	{
		static const auto controls = json{ { "tabs", { FusRoDahControls(), DragonControls(), SpellShoutControls(),
			ProjectileControls(), ExplosionControls(), StormCallControls(), WeaponThrowControls(), HeavyImpactControls() } } }.dump();
		return controls;
	}
}

bool OpenWind::UI::GetControls(const char* section, char* buffer, uint32_t* inoutSize) noexcept
{
	if (!section || !inoutSize || std::string_view(section) != "windEffects")
		return false;
	try {
		const auto& controls = WindEffectControls();
		if (controls.size() >= std::numeric_limits<uint32_t>::max())
			return false;
		const auto capacity = *inoutSize;
		*inoutSize = static_cast<uint32_t>(controls.size() + 1);
		if (!buffer)
			return true;
		if (capacity < *inoutSize)
			return false;
		std::memcpy(buffer, controls.c_str(), *inoutSize);
		return true;
	} catch (...) {
		return false;
	}
}
