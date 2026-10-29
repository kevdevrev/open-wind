#pragma once

#include "OpenWind/Types.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace OpenWind::TerrainShelter
{
	/** An immutable top mip of a worldspace heightmap, retained in its original DDS format. */
	struct TerrainHeightmap
	{
		uint32_t width{};
		uint32_t height{};
		uint32_t pitch{};
		uint32_t format{};
		float3 minimum{};
		float3 maximum{};
		std::vector<std::byte> pixels;

		/** Bilinearly samples known worldspace coverage; false leaves unloaded terrain unknown. */
		[[nodiscard]] bool Sample(float a_x, float a_y, float& a_height) const noexcept;
	};
	/** Requests asynchronous DDS discovery and returns completed data for this land worldspace. */
	[[nodiscard]] std::shared_ptr<const TerrainHeightmap> RequestHeightmap(std::string_view a_worldspace);
}
