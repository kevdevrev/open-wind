#pragma once

#include "OpenWind/TerrainShelter.h"

#include <memory>

namespace OpenWind::TerrainShelter
{
	struct TerrainHeightmap;
	/** Returns the complete heightmap only when it belongs to the requested active world. */
	[[nodiscard]] std::shared_ptr<const TerrainHeightmap> WorldSnapshot(uint32_t a_worldspaceId);
	/** Schedules at most one bounded terrain-query task on the SKSE game thread. */
	void RequestUpdate(float a_time, float a_radius);
	/** Invalidates queued work and suspends terrain discovery while a scene is loading. */
	void Reset(bool a_loading);
	/** Returns immutable terrain data without accessing game objects. */
	[[nodiscard]] std::shared_ptr<const Heightfield> Snapshot();
	/** Returns immutable middle coverage when the outer radius exceeds the fixed middle extent. */
	[[nodiscard]] std::shared_ptr<const Heightfield> MiddleSnapshot();
	/** Returns immutable coarse coverage for the configured outer radius. */
	[[nodiscard]] std::shared_ptr<const Heightfield> FarSnapshot();
}
