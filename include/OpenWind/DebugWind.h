// SPDX-License-Identifier: LGPL-3.0-or-later
// See COPYING.LESSER and NOTICE.md for licensing.

#pragma once

namespace OpenWind
{
	/** Runtime-only controls for a player-centered radial test pulse. */
	struct DebugWindPulse
	{
		static constexpr float kMinimumStrength = 0.1f;
		static constexpr float kMaximumStrength = 20.0f;
		static constexpr float kMinimumRadius = 128.0f;
		static constexpr float kMaximumRadius = 8192.0f;

		float strength = 3.0f;
		float radius = 1500.0f;
	};
}
