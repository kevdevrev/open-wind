#include "Integrations.h"

#include "OpenWind/BridgeAPI.h"

#include <stdexcept>

namespace OpenWind::Integrations
{
	namespace
	{
		struct Bridge
		{
			const wchar_t* module;
			const char* name;
			const char* key;
		};
		constexpr Bridge bridges[]{
			{ L"OpenWindHDT.dll", "OpenWindHDT.dll", "hdt" },
			{ L"OpenWindDynamicWind.dll", "OpenWindDynamicWind.dll", "dynamicWind" }
		};

		const BridgeAPI* FindBridge(const Bridge& bridge, std::string& status)
		{
			const auto module = GetModuleHandleW(bridge.module);
			if (!module) {
				status = std::format("{} is not installed or loaded.", bridge.name);
				return nullptr;
			}
			const auto request = reinterpret_cast<RequestBridgeAPI>(GetProcAddress(module, "GetOpenWindBridgeAPI"));
			const auto* api = request ? request(kBridgeAPIRevision) : nullptr;
			if (!api || api->structSize < sizeof(BridgeAPI) || !api->Command) {
				status = std::format("{} has an incompatible settings API.", bridge.name);
				return nullptr;
			}
			return api;
		}

		json CallBridge(const BridgeAPI& api, const json& request)
		{
			json result;
			const auto text = request.dump();
			const auto delivered = api.Command(text.c_str(), &result, [](void* context, const char* response) noexcept {
				try {
					if (response)
						*static_cast<json*>(context) = json::parse(response, nullptr, false);
				} catch (...) {
				}
			});
			if (!delivered || !result.is_object() || !result.contains("success") || !result.at("success").is_boolean())
				throw std::runtime_error("The bridge returned an invalid command response.");
			if (!result.at("success").get<bool>())
				throw std::runtime_error(result.value("error", "The bridge command failed."));
			return result;
		}
	}

	json Command(const json& request)
	{
		try {
			const auto action = request.at("action").get<std::string>();
			const auto set = action == "setIntegrations";
			const auto save = request.value("save", false);
			const char* bridgeAction = nullptr;
			if (action == "integrations")
				bridgeAction = "get";
			else if (set)
				bridgeAction = "set";
			else if (action == "saveIntegrations")
				bridgeAction = "save";
			else if (action == "reloadIntegrations")
				bridgeAction = "reload";
			else if (action == "resetIntegrations")
				bridgeAction = "reset";
			else if (action == "retryIntegrations")
				bridgeAction = "retry";
			else
				throw std::invalid_argument("Unknown integration action.");
			for (const auto& bridge : bridges) {
				if (set && request.contains(bridge.key) && !request.at(bridge.key).is_object())
					throw std::invalid_argument("Integration settings must be objects.");
			}

			json result{ { "success", true } };
			for (const auto& bridge : bridges) {
				std::string status;
				const auto* api = FindBridge(bridge, status);
				if (!api) {
					if (set && request.contains(bridge.key))
						throw std::runtime_error(status);
					result[bridge.key] = { { "installed", false }, { "registered", false }, { "status", status } };
					continue;
				}
				json command{ { "action", bridgeAction } };
				if (set) {
					if (request.contains(bridge.key)) {
						command["values"] = request.at(bridge.key);
						command["save"] = save;
					} else {
						command["action"] = save ? "save" : "get";
					}
				}
				try {
					result[bridge.key] = CallBridge(*api, command);
				} catch (const std::exception& error) {
					throw std::runtime_error(std::format("{}: {}", bridge.name, error.what()));
				}
			}
			return result;
		} catch (const std::exception& error) {
			return { { "success", false }, { "error", error.what() } };
		}
	}
}
