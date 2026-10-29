#include "Runtime/Runtime.h"
#include "Runtime/GameContext.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message || a_message->type != SKSE::MessagingInterface::kDataLoaded)
			return;

		try {
			OpenWind::Game::Initialize();
			auto& wind = Wind::Get();
			if (!wind.IsRendererBound()) {
				logger::error("Open Wind has no compatible renderer; wind updates and CPU sampling are unavailable");
				RE::DebugMessageBox("Open Wind: the renderer connection was not established. Check the Open Shaders log for API connection errors.");
				return;
			}

			wind.DataLoaded();
			logger::info("Open Wind initialized; the connected renderer drives wind updates");
		} catch (const std::exception& exception) {
			logger::error("Open Wind initialization failed: {}", exception.what());
			RE::DebugMessageBox("Open Wind could not initialize. See OpenWind.log for details.");
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .logName = "OpenWind" });
	try {
		const auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener("SKSE", OnSKSEMessage)) {
			logger::error("Open Wind could not register its SKSE lifecycle listener");
			return false;
		}
		logger::info("Open Wind {} loaded", SKSE::GetPluginVersion().string());
		return true;
	} catch (const std::exception& exception) {
		logger::error("Open Wind failed to load: {}", exception.what());
		return false;
	}
}
