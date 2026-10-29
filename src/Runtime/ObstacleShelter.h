#pragma once

#include "OpenWind/ObstacleShelter.h"
#include "OpenWind/TerrainShelter.h"

#include <memory>

namespace OpenWind::ObstacleShelter
{
	/** Queues bounded game-thread collision discovery; no physics access occurs in sampling callbacks. */
	void RequestUpdate(float a_time, float a_radius, float a_distance, float a_strength,
		float2 a_currentDirection, float2 a_transitionDirection, float a_transitionBlend,
		std::shared_ptr<const TerrainShelter::Heightfield> a_nearTerrain,
		std::shared_ptr<const TerrainShelter::Heightfield> a_farTerrain,
		std::shared_ptr<const TerrainShelter::Heightfield> a_middleTerrain = nullptr);
	/** Invalidates pending work and suspends collision discovery during scene loading. */
	void Reset(bool a_loading);
	/** Acquires the latest immutable collision cache without accessing game objects. */
	[[nodiscard]] std::shared_ptr<const Frame> Snapshot();
}
