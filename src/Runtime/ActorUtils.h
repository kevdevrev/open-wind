#pragma once

#include "OpenWind/Types.h"
#include <RE/Skyrim.h>

namespace Util
{
	/** Identifies dragons from authored race data. */
	bool IsDragon(const RE::Actor& actor, const RE::BGSKeyword* dragonKeyword = nullptr);
	/** Returns the source positions and aim used by the existing wind effects. */
	float3 GetVisualOrigin(RE::Actor& actor) noexcept;
	float3 GetMagicOrigin(RE::Actor& actor) noexcept;
	float3 GetAimDirection(RE::Actor& actor) noexcept;
}
