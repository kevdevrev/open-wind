#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace OpenWind::Integrations::HDT
{
	namespace
	{
		struct SettingsState
		{
			std::mutex lock;
			Settings settings;
		};

		SettingsState& State()
		{
			static auto* state = new SettingsState;
			return *state;
		}

		float Clamp(float value, float fallback, float minimum, float maximum)
		{
			return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
		}
	}

	Settings GetSettings()
	{
		auto& state = State();
		std::lock_guard lock(state.lock);
		return state.settings;
	}

	void UpdateSettings(Settings settings)
	{
		const Settings defaults;
		settings.velocityResponse = Clamp(settings.velocityResponse, defaults.velocityResponse, 0.f, 0.5f);
		settings.flutterAmount = Clamp(settings.flutterAmount, defaults.flutterAmount, 0.f, 1.f);
		settings.flutterRate = Clamp(settings.flutterRate, defaults.flutterRate, 0.1f, 4.f);
		settings.torqueOffset = Clamp(settings.torqueOffset, defaults.torqueOffset, 0.f, 2.f);
		auto& state = State();
		std::lock_guard lock(state.lock);
		state.settings = settings;
	}
}
