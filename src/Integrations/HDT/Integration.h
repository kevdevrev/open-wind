#pragma once

#include <SKSE/SKSE.h>
#include <string>

namespace OpenWind::Integrations::HDT
{
	/** Detects FSMP and registers its callbacks at SKSE lifecycle events on the game thread. */
	void OnMessage(SKSE::MessagingInterface::Message* message);
	/** Reports the reserved provider slot without calling FSMP from the menu thread. */
	bool IsRegistered();
	/** Reports compatibility, registration, and source readiness. */
	std::string Status();
	/** Applies the enable setting to FSMP ownership; game thread only, outside callbacks. */
	void RefreshRegistration();
}
