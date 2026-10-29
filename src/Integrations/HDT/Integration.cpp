#include "Integration.h"
#include "Controller.h"

#include <exception>

namespace OpenWind::Integrations::HDT
{
	namespace
	{
		void OnFSMP(SKSE::MessagingInterface::Message* message)
		{
			if (!message || message->type != hdt::PluginInterface::MSG_STARTUP || !message->data)
				return;
			try {
				Controller::Get().SetPhysicsInterface(static_cast<hdt::PluginInterface*>(message->data));
			} catch (const std::exception& exception) {
				SKSE::log::error("Open Wind could not connect to FSMP: {}", exception.what());
			}
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		if (!message)
			return;
		if (message->type == SKSE::MessagingInterface::kPostLoad) {
			const auto messaging = SKSE::GetMessagingInterface();
			if (!messaging || !messaging->RegisterListener("hdtsmp64", OnFSMP))
				SKSE::log::info("Open Wind HDT integration: FSMP messaging is unavailable");
		} else if (message->type == SKSE::MessagingInterface::kPostPostLoad ||
				   message->type == SKSE::MessagingInterface::kDataLoaded) {
			RefreshRegistration();
		}
	}

	bool IsRegistered()
	{
		return Controller::Get().IsRegistered();
	}

	std::string Status()
	{
		return Controller::Get().Status();
	}

	void RefreshRegistration()
	{
		Controller::Get().RefreshRegistration();
	}
}
