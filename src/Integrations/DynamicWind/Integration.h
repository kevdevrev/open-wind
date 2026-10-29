#pragma once

#include "DynamicWind/DynamicWindAPI.h"
#include <string>

namespace OpenWind::Integrations::DynamicWind
{
	using PushResponse = DynamicWindAPI::PushResponse;
	inline constexpr float kDefaultSignSensitivity = 1.0f;
	inline constexpr float kMinimumSignSensitivity = 0.0f;
	inline constexpr float kMaximumSignSensitivity = 100.0f;

	/** Detects the compatible host and registers once on the game thread. */
	void Connect();
	/** Reports whether the host accepted this module's provider. */
	bool IsRegistered();
	/** Returns the user's integration enable setting. */
	bool IsEnabled();
	/** Changes enablement; the host performs the next native/external handoff. */
	void SetEnabled(bool enabled);
	/** Returns the global multiplier for Dynamic Wind physics responses. */
	float GetSignSensitivity() noexcept;
	/** Clamps finite input to the supported range; nonfinite input leaves the value unchanged. */
	void SetSignSensitivity(float sensitivity) noexcept;
	/** Returns the strength curve applied before sign sensitivity. */
	PushResponse GetPushResponse() noexcept;
	/** Selects linear wind response or the original capped cubic strength curve. */
	void SetPushResponse(PushResponse response) noexcept;
	/** Returns a thread-safe description of discovery and source readiness. */
	std::string Status();
}
