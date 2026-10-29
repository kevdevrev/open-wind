#pragma once

#include <cstdint>

namespace OpenWind
{
	inline constexpr uint32_t kBridgeAPIRevision = 1;

	/** Optional bridge settings transport; JSON text is borrowed only during each callback. */
	struct BridgeAPI
	{
		uint32_t structSize = sizeof(BridgeAPI);
		uint32_t revision = kBridgeAPIRevision;
		/** Synchronously replies once; true means delivery, while JSON success reports the operation result. */
		bool (*Command)(const char* request, void* context, void (*reply)(void*, const char*) noexcept) noexcept {};
	};

	using RequestBridgeAPI = const BridgeAPI* (*)(uint32_t revision) noexcept;
}
