#pragma once

#include <cstdint>

namespace OpenWind::UI
{
	/** Copies a section's immutable JSON controls; the required byte count includes the terminator. */
	bool GetControls(const char* section, char* buffer, uint32_t* inoutSize) noexcept;
}
