#pragma once

namespace OpenWind::Integrations::HDT
{
	struct Settings
	{
		bool enabled = true;
		float velocityResponse = 0.08f;
		float flutterAmount = 1.f;
		float flutterRate = 1.f;
		float torqueOffset = 0.8f;
	};

	/** Copies the current response settings for one physics update. */
	Settings GetSettings();
	/** Validates response settings without changing registration or writing files. */
	void UpdateSettings(Settings settings);
}
