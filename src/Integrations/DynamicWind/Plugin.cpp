#include "Integration.h"
#include "Integrations/BridgeRuntime.h"

#include <cmath>
#include <stdexcept>

namespace
{
	namespace DynamicWind = OpenWind::Integrations::DynamicWind;
	namespace BridgeRuntime = OpenWind::Integrations::BridgeRuntime;

	nlohmann::json SettingsDocument()
	{
		return {
			{ "enabled", DynamicWind::IsEnabled() },
			{ "signSensitivity", DynamicWind::GetSignSensitivity() },
			{ "pushResponse", DynamicWind::GetPushResponse() == DynamicWind::PushResponse::Cubic ? "cubic" : "linear" }
		};
	}

	void Apply(const nlohmann::json& values, bool defaults)
	{
		if (!values.is_object())
			throw std::invalid_argument("Dynamic Wind settings must be an object.");
		auto enabled = defaults ? true : DynamicWind::IsEnabled();
		auto signSensitivity = defaults ? DynamicWind::kDefaultSignSensitivity : DynamicWind::GetSignSensitivity();
		auto pushResponse = defaults ? DynamicWind::PushResponse::Linear : DynamicWind::GetPushResponse();
		if (values.contains("enabled"))
			enabled = values.at("enabled").get<bool>();
		if (values.contains("signSensitivity")) {
			signSensitivity = values.at("signSensitivity").get<float>();
			if (!std::isfinite(signSensitivity))
				throw std::invalid_argument("Sign wind sensitivity must be finite.");
		}
		if (values.contains("pushResponse")) {
			const auto response = values.at("pushResponse").get<std::string>();
			if (response != "linear" && response != "cubic")
				throw std::invalid_argument("Push response must be linear or cubic.");
			pushResponse = response == "cubic" ? DynamicWind::PushResponse::Cubic : DynamicWind::PushResponse::Linear;
		}
		DynamicWind::SetEnabled(enabled);
		DynamicWind::SetSignSensitivity(signSensitivity);
		DynamicWind::SetPushResponse(pushResponse);
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		if (message && (message->type == SKSE::MessagingInterface::kPostPostLoad ||
					   message->type == SKSE::MessagingInterface::kDataLoaded))
			DynamicWind::Connect();
	}

	const BridgeRuntime::Adapter adapter{
		"OpenWindDynamicWind", "Data/SKSE/Plugins/OpenWind/Bridges/DynamicWind.json",
		SettingsDocument, Apply, DynamicWind::IsRegistered, DynamicWind::Status,
		DynamicWind::Connect, OnMessage
	};
}

/** Initializes the Dynamic Wind bridge and its persistent settings. */
SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
	return BridgeRuntime::Load(skse, adapter);
}

/** Returns the versioned JSON command interface owned by this bridge. */
extern "C" __declspec(dllexport) const OpenWind::BridgeAPI* GetOpenWindBridgeAPI(uint32_t revision) noexcept
{
	return BridgeRuntime::GetAPI(revision);
}
