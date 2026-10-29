#include "Integrations/BridgeRuntime.h"
#include "Integration.h"
#include "Settings.h"

#include <cmath>
#include <stdexcept>

namespace
{
	namespace HDT = OpenWind::Integrations::HDT;

	json Settings()
	{
		const auto settings = HDT::GetSettings();
		return {
			{ "enabled", settings.enabled },
			{ "velocityResponse", settings.velocityResponse },
			{ "flutterAmount", settings.flutterAmount },
			{ "flutterRate", settings.flutterRate },
			{ "torqueOffset", settings.torqueOffset }
		};
	}

	void Apply(const json& values, bool defaults)
	{
		if (!values.is_object())
			throw std::invalid_argument("HDT settings must be an object.");
		auto settings = defaults ? HDT::Settings{} : HDT::GetSettings();
		if (values.contains("enabled"))
			settings.enabled = values.at("enabled").get<bool>();
		const auto read = [&](const char* key, float& value) {
			if (!values.contains(key))
				return;
			value = values.at(key).get<float>();
			if (!std::isfinite(value))
				throw std::invalid_argument("HDT settings must be finite.");
		};
		read("velocityResponse", settings.velocityResponse);
		read("flutterAmount", settings.flutterAmount);
		read("flutterRate", settings.flutterRate);
		read("torqueOffset", settings.torqueOffset);
		HDT::UpdateSettings(settings);
	}

	const OpenWind::Integrations::BridgeRuntime::Adapter adapter{
		"OpenWindHDT", "Data/SKSE/Plugins/OpenWind/Bridges/HDT.json",
		Settings, Apply, HDT::IsRegistered, HDT::Status, HDT::RefreshRegistration, HDT::OnMessage
	};
}

/** Initializes the HDT bridge and its persistent settings. */
SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
	return OpenWind::Integrations::BridgeRuntime::Load(skse, adapter);
}

/** Exposes this bridge's settings transport to compatible Open Wind consumers. */
extern "C" __declspec(dllexport) const OpenWind::BridgeAPI* GetOpenWindBridgeAPI(uint32_t revision) noexcept
{
	return OpenWind::Integrations::BridgeRuntime::GetAPI(revision);
}
