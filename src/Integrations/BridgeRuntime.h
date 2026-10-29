#pragma once

#include "OpenWind/BridgeAPI.h"

#include <SKSE/SKSE.h>
#include <nlohmann/json.hpp>
#include <string>

namespace OpenWind::Integrations::BridgeRuntime
{
	/** Local settings and lifecycle callbacks owned by one bridge DLL. */
	struct Adapter
	{
		const char* name;
		const char* settingsPath;
		nlohmann::json (*Settings)();
		void (*Apply)(const nlohmann::json& values, bool defaults);
		bool (*IsRegistered)();
		std::string (*Status)();
		void (*Refresh)();
		void (*OnMessage)(SKSE::MessagingInterface::Message* message);
	};

	/** Initializes logging, persistent settings, and lifecycle forwarding for a static adapter. */
	bool Load(const SKSE::LoadInterface* skse, const Adapter& adapter) noexcept;
	/** Returns the bridge settings transport regardless of the requested revision. */
	const BridgeAPI* GetAPI(uint32_t revision) noexcept;
}
