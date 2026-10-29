#include "Runtime/TerrainShelter.h"

#include "Runtime/GameContext.h"
#include "Runtime/TerrainHeightmap.h"
#include "OpenWind/ObstacleShelter.h"

#include <cstring>
#include <limits>
#include <numeric>
#include <optional>

namespace OpenWind::TerrainShelter
{
	namespace
	{
		constexpr uint32_t kQueriesPerTask = 64;
		constexpr uint32_t kNearQueriesPerTask = 48;
		constexpr uint32_t kMiddleQueriesPerTask = 60;
		constexpr uint32_t kMarchSteps = 12;
		constexpr float kMinimumRadius = 4096.0f;
		constexpr float kMaximumRadius = 60000.0f;
		constexpr float kRetryDelay = 0.5f;
		constexpr float kDiscoveryInterval = 0.1f;
		constexpr uint8_t kMaximumRetryExponent = 4;
		constexpr float kMinimumDistance = 64.0f;
		constexpr float kClearance = 32.0f;
		constexpr float kBoundaryCells = 2.0f;
		constexpr float kMinimumSoftness = 0.0001f;
		constexpr float kCoverageScaleTolerance = 0.00001f;

		struct Discovery
		{
			std::array<float, kTerrainSampleCount> retryAfter{};
			std::array<uint8_t, kTerrainSampleCount> retryExponent{};
			std::array<bool, kTerrainSampleCount> precise{};
			std::array<float, kTerrainSampleCount> targetHeight{};
			std::array<float, kTerrainSampleCount> blendHeight{};
			std::array<float, kTerrainSampleCount> blendTime{};
			uint32_t cursor{};
			float nextWorkTime{};
			bool heightmapApplied{};
		};

		struct Cache
		{
			std::mutex mutex;
			std::array<std::atomic<std::shared_ptr<const Heightfield>>, kCascadeCount> published;
			std::array<Discovery, kCascadeCount> discovery;
			std::shared_ptr<const TerrainHeightmap> worldHeightmap;
			uint32_t worldspaceId{};
			uint64_t generation{};
			uint64_t revision{};
			float nextUpdate{};
			bool pending{};
			bool loading{};
		};

		Cache& GetCache()
		{
			static auto* cache = new Cache;
			return *cache;
		}

		const std::array<uint32_t, kTerrainSampleCount>& QueryOrder()
		{
			static const auto order = [] {
				std::array<uint32_t, kTerrainSampleCount> indices;
				std::iota(indices.begin(), indices.end(), 0u);
				const auto distance = [](uint32_t a_index) {
					const auto x = static_cast<int>(a_index % kTerrainSize) - static_cast<int>(kTerrainSize / 2);
					const auto y = static_cast<int>(a_index / kTerrainSize) - static_cast<int>(kTerrainSize / 2);
					return x * x + y * y;
				};
				std::sort(indices.begin(), indices.end(), [&](uint32_t a_left, uint32_t a_right) {
					const auto left = distance(a_left);
					const auto right = distance(a_right);
					return left == right ? a_left < a_right : left < right;
				});
				return indices;
			}();
			return order;
		}

		float SmoothUnit(float a_value) noexcept
		{
			const float value = std::clamp(a_value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value);
		}

		float3 SampleTerrain(const Heightfield& a_field, const Parameters& a_parameters,
			float a_x, float a_y) noexcept
		{
			if (!a_field.valid || a_parameters.terrain.z <= 0.0f)
				return {};
			const float x = (a_x - a_parameters.terrain.x) / a_parameters.terrain.z;
			const float y = (a_y - a_parameters.terrain.y) / a_parameters.terrain.z;
			if (x < 0.0f || y < 0.0f || x > kTerrainSize - 1 || y > kTerrainSize - 1)
				return {};
			const auto lowerX = std::min(static_cast<uint32_t>(x), kTerrainSize - 2);
			const auto lowerY = std::min(static_cast<uint32_t>(y), kTerrainSize - 2);
			const float fractionX = x - lowerX;
			const float fractionY = y - lowerY;
			float height = 0.0f;
			float validWeight = 0.0f;
			float confidence = 0.0f;
			for (uint32_t row = 0; row < 2; ++row) {
				for (uint32_t column = 0; column < 2; ++column) {
					const auto& sample = a_field.samples[(lowerY + row) * kTerrainSize + lowerX + column];
					if (sample.y < 0.0f)
						continue;
					const float weight = (column ? fractionX : 1.0f - fractionX) *
					                     (row ? fractionY : 1.0f - fractionY);
					height += sample.x * weight;
					validWeight += weight;
					confidence += weight * SmoothUnit((a_parameters.terrain.w - sample.y) / kSampleFadeSeconds);
				}
			}
			return validWeight > 0.0f ? float3{ height / validWeight, confidence, validWeight } : float3{};
		}

		float DirectionalExposure(const Heightfield& a_field, const Parameters& a_parameters,
			const float3& a_position, float2 a_direction, const Heightfield* a_far = nullptr,
			const Parameters* a_farParameters = nullptr, const TerrainHeightmap* a_world = nullptr) noexcept
		{
			const float directionLength = std::hypot(a_direction.x, a_direction.y);
			if (directionLength <= kMinimumSoftness)
				return 1.0f;
			a_direction /= directionLength;
			const float range = std::max(a_parameters.response.y, kMinimumDistance);
			const float softness = std::max(a_parameters.response.z, kMinimumSoftness);
			float blocker = 0.0f;
			for (uint32_t index = 0; index < kMarchSteps; ++index) {
				const float fraction = static_cast<float>(index + 1) / kMarchSteps;
				const float distance = range * fraction * fraction;
				auto terrain = SampleTerrain(a_field, a_parameters,
					a_position.x - a_direction.x * distance, a_position.y - a_direction.y * distance);
				if (a_far && a_far->valid && a_farParameters && terrain.z < 1.0f) {
					const auto farSample = SampleTerrain(*a_far, *a_farParameters,
						a_position.x - a_direction.x * distance, a_position.y - a_direction.y * distance);
					const float farWeight = farSample.z * (1.0f - terrain.z);
					const float validWeight = terrain.z + farWeight;
					terrain = validWeight > 0.0f ? float3{ (terrain.x * terrain.z + farSample.x * farWeight) / validWeight,
						terrain.y + farSample.y * (1.0f - terrain.z), validWeight } : float3{};
				}
				if (a_world && terrain.z < 1.0f) {
					float worldHeight;
					if (a_world->Sample(a_position.x - a_direction.x * distance, a_position.y - a_direction.y * distance, worldHeight)) {
						terrain.x = std::lerp(worldHeight, terrain.x, terrain.z);
						terrain.y += 1.0f - terrain.z;
						terrain.z = 1.0f;
					}
				}
				const float horizon = (terrain.x - a_position.z - kClearance) / std::max(distance, kMinimumDistance);
				blocker = std::max(blocker,
					SmoothUnit(horizon / softness) * (1.0f - distance / range) * terrain.y);
			}
			return 1.0f - std::clamp(a_parameters.response.x, 0.0f, 1.0f) * blocker;
		}

		float WorldExposure(const TerrainHeightmap* a_world, const Parameters& a_parameters, const float3& a_position) noexcept
		{
			float height;
			if (!a_world || a_parameters.response.x <= 0.0f || !a_world->Sample(a_position.x, a_position.y, height))
				return 1.0f;
			static const Heightfield empty{};
			float exposure = DirectionalExposure(empty, a_parameters, a_position,
				{ a_parameters.directions.x, a_parameters.directions.y }, nullptr, nullptr, a_world);
			if (a_parameters.response.w < 1.0f)
				exposure = std::lerp(DirectionalExposure(empty, a_parameters, a_position,
					{ a_parameters.directions.z, a_parameters.directions.w }, nullptr, nullptr, a_world),
					exposure, std::clamp(a_parameters.response.w, 0.0f, 1.0f));
			return exposure;
		}

		float EvaluateExposure(const Heightfield& a_field, const Parameters& a_parameters, const float3& a_position,
			const Heightfield* a_far = nullptr, const Parameters* a_farParameters = nullptr, const TerrainHeightmap* a_world = nullptr) noexcept
		{
			if (a_parameters.response.x <= 0.0f || a_parameters.volumeStep.w <= 0.0f)
				return 1.0f;
			float exposure = DirectionalExposure(a_field, a_parameters, a_position,
				{ a_parameters.directions.x, a_parameters.directions.y }, a_far, a_farParameters, a_world);
			const float blend = std::clamp(a_parameters.response.w, 0.0f, 1.0f);
			if (blend < 1.0f) {
				const float transition = DirectionalExposure(a_field, a_parameters, a_position,
					{ a_parameters.directions.z, a_parameters.directions.w }, a_far, a_farParameters, a_world);
				exposure = std::lerp(transition, exposure, blend);
			}
			const float radius = a_parameters.volumeStep.w;
			if (radius > 0.0f) {
				const auto& terrain = a_far && a_far->valid && a_farParameters ? a_farParameters->terrain : a_parameters.terrain;
				if (radius / ((kVolumeWidth - 1) * 0.5f) >= terrain.z * (1.0f - kCoverageScaleTolerance))
					return exposure;
				const float2 center{ terrain.x + (kTerrainSize - 1) * 0.5f * terrain.z,
					terrain.y + (kTerrainSize - 1) * 0.5f * terrain.z };
				const float edge = radius - std::max(std::abs(a_position.x - center.x), std::abs(a_position.y - center.y));
				const float fade = SmoothUnit(edge / (kBoundaryCells * radius / ((kVolumeWidth - 1) * 0.5f)));
				if (fade < 1.0f)
					exposure = std::lerp(WorldExposure(a_world, a_parameters, a_position), exposure, fade);
			}
			return exposure;
		}

		void Recenter(Heightfield& a_field, Discovery& a_discovery, const Heightfield* a_previous,
			const Discovery& a_previousDiscovery, float2 a_origin)
		{
			a_field.origin = a_origin;
			a_field.samples.fill(float2{ 0.0f, -1.0f });
			a_discovery = {};
			if (!a_previous)
				return;
			const int offsetX = static_cast<int>(std::round((a_origin.x - a_previous->origin.x) / a_field.cellSize));
			const int offsetY = static_cast<int>(std::round((a_origin.y - a_previous->origin.y) / a_field.cellSize));
			for (uint32_t y = 0; y < kTerrainSize; ++y) {
				for (uint32_t x = 0; x < kTerrainSize; ++x) {
					const int previousX = static_cast<int>(x) + offsetX;
					const int previousY = static_cast<int>(y) + offsetY;
					if (previousX >= 0 && previousY >= 0 && previousX < static_cast<int>(kTerrainSize) &&
						previousY < static_cast<int>(kTerrainSize)) {
						const auto index = y * kTerrainSize + x;
						const auto previousIndex = previousY * kTerrainSize + previousX;
						a_field.samples[index] = a_previous->samples[previousIndex];
						a_discovery.retryAfter[index] = a_previousDiscovery.retryAfter[previousIndex];
						a_discovery.retryExponent[index] = a_previousDiscovery.retryExponent[previousIndex];
						a_discovery.precise[index] = a_previousDiscovery.precise[previousIndex];
						a_discovery.targetHeight[index] = a_previousDiscovery.targetHeight[previousIndex];
						a_discovery.blendHeight[index] = a_previousDiscovery.blendHeight[previousIndex];
						a_discovery.blendTime[index] = a_previousDiscovery.blendTime[previousIndex];
					}
				}
			}
		}

		void Discover(Heightfield& a_field, Discovery& a_discovery, const TerrainHeightmap* a_heightmap,
			float a_time, float a_playerHeight, uint32_t a_queryLimit, uint32_t& a_queries, bool& a_changed)
		{
			for (uint32_t index = 0; index < kTerrainSampleCount; ++index) {
				if (!a_discovery.precise[index] || a_field.samples[index].x == a_discovery.targetHeight[index])
					continue;
				a_field.samples[index].x = std::lerp(a_discovery.blendHeight[index], a_discovery.targetHeight[index],
					SmoothUnit((a_time - a_discovery.blendTime[index]) / kSampleFadeSeconds));
				a_changed = true;
			}
			if (a_heightmap && !a_discovery.heightmapApplied) {
				for (uint32_t index = 0; index < kTerrainSampleCount; ++index) {
					if (a_field.samples[index].y >= 0.0f)
						continue;
					float height;
					if (a_heightmap->Sample(a_field.origin.x + (index % kTerrainSize) * a_field.cellSize,
							a_field.origin.y + (index / kTerrainSize) * a_field.cellSize, height)) {
						a_field.samples[index] = { height, a_time };
						a_changed = true;
					}
				}
				a_discovery.heightmapApplied = true;
			}
			const auto& order = QueryOrder();
			for (uint32_t visited = 0; visited < kTerrainSampleCount && a_queries < a_queryLimit; ++visited) {
				const uint32_t index = order[a_discovery.cursor];
				a_discovery.cursor = (a_discovery.cursor + 1) % kTerrainSampleCount;
				if (a_discovery.precise[index] || a_time < a_discovery.retryAfter[index])
					continue;
				++a_queries;
				const RE::NiPoint3 query{ a_field.origin.x + (index % kTerrainSize) * a_field.cellSize,
					a_field.origin.y + (index / kTerrainSize) * a_field.cellSize, a_playerHeight };
				float height;
				if (Game::tes->GetLandHeight(query, height) && std::isfinite(height)) {
					const auto previous = a_field.samples[index];
					a_field.samples[index] = previous.y < 0.0f ? float2{ height, a_time } : previous;
					a_discovery.precise[index] = true;
					a_discovery.targetHeight[index] = height;
					a_discovery.blendHeight[index] = a_field.samples[index].x;
					a_discovery.blendTime[index] = a_time;
					a_changed = true;
				} else {
					a_discovery.retryAfter[index] = a_time + kRetryDelay * (1u << a_discovery.retryExponent[index]);
					a_discovery.retryExponent[index] = std::min<uint8_t>(a_discovery.retryExponent[index] + 1, kMaximumRetryExponent);
				}
			}
			a_discovery.nextWorkTime = std::numeric_limits<float>::infinity();
			for (uint32_t index = 0; index < kTerrainSampleCount; ++index) {
				if (!a_discovery.precise[index])
					a_discovery.nextWorkTime = std::min(a_discovery.nextWorkTime, a_discovery.retryAfter[index]);
				else if (a_field.samples[index].x != a_discovery.targetHeight[index])
					a_discovery.nextWorkTime = 0.0f;
			}
		}

		void RunUpdate(uint64_t a_generation, float a_time, float a_radius) noexcept
		{
			auto& cache = GetCache();
			try {
				{
					std::lock_guard lock(cache.mutex);
					if (a_generation != cache.generation || cache.loading)
						return;
				}
				const auto* player = Game::player;
				const auto* cell = player ? player->GetParentCell() : nullptr;
				const auto* worldspace = player && cell && !cell->IsInteriorCell() ? player->GetWorldspace() : nullptr;
				const auto position = player ? player->GetPosition() : RE::NiPoint3{};
				std::unique_lock lock(cache.mutex);
				if (a_generation != cache.generation || cache.loading)
					return;
				if (!Game::tes || !worldspace || !std::isfinite(position.x) || !std::isfinite(position.y) ||
					!std::isfinite(position.z)) {
					for (auto& field : cache.published)
						field.store(nullptr);
					cache.discovery = {};
					cache.worldHeightmap.reset();
					cache.worldspaceId = 0;
					cache.pending = false;
					return;
				}
				auto landWorldspace = worldspace;
				while (landWorldspace->parentWorld &&
					landWorldspace->parentUseFlags.any(RE::TESWorldSpace::ParentUseFlag::kUseLandData))
					landWorldspace = landWorldspace->parentWorld;
				const auto* worldspaceName = landWorldspace->GetFormEditorID();
				const auto heightmap = worldspaceName && *worldspaceName ? RequestHeightmap(worldspaceName) : nullptr;
				const auto worldspaceId = worldspace->GetFormID();
				cache.worldspaceId = worldspaceId;
				cache.worldHeightmap = heightmap;
				std::array<std::shared_ptr<Heightfield>, kCascadeCount> next;
				std::array<std::optional<Discovery>, kCascadeCount> discovery;
				std::array<bool, kCascadeCount> changed{};
				for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
					if (cascade == 1 && a_radius <= kMiddleRadius) {
						cache.published[cascade].store(nullptr);
						cache.discovery[cascade] = {};
						continue;
					}
					const auto previous = cache.published[cascade].load();
					const float spacing = cascade == 0 ? kCellSize : cascade == 1 ? kMiddleCellSize :
						std::clamp(a_radius, kMinimumRadius, kMaximumRadius) / ((kVolumeWidth - 1) * 0.5f);
					const float extent = (kTerrainSize - 1) * spacing * 0.5f;
					const float2 origin{ std::floor(position.x / spacing) * spacing - extent,
						std::floor(position.y / spacing) * spacing - extent };
					const bool reuse = previous && previous->worldspaceId == worldspaceId && previous->cellSize == spacing &&
						std::abs(origin.x - previous->origin.x) < kTerrainSize * spacing &&
						std::abs(origin.y - previous->origin.y) < kTerrainSize * spacing;
					changed[cascade] = !reuse || origin != previous->origin;
					if (!changed[cascade] && a_time < cache.discovery[cascade].nextWorkTime &&
						(!heightmap || cache.discovery[cascade].heightmapApplied))
						continue;
					next[cascade] = std::make_shared<Heightfield>();
					next[cascade]->cellSize = spacing;
					if (changed[cascade]) {
						discovery[cascade].emplace();
						Recenter(*next[cascade], *discovery[cascade], reuse ? previous.get() : nullptr, cache.discovery[cascade], origin);
					} else {
						discovery[cascade].emplace(cache.discovery[cascade]);
						*next[cascade] = *previous;
					}
					next[cascade]->worldspaceId = worldspaceId;
					next[cascade]->valid = 1;
				}
				lock.unlock();
				uint32_t queries = 0;
				for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
					if (next[cascade])
						Discover(*next[cascade], *discovery[cascade], heightmap.get(), a_time, position.z,
							cascade == 0 ? kNearQueriesPerTask : cascade == 1 ? kMiddleQueriesPerTask : kQueriesPerTask, queries, changed[cascade]);
				lock.lock();
				if (a_generation != cache.generation || cache.loading)
					return;
				for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade) {
					if (discovery[cascade])
						cache.discovery[cascade] = std::move(*discovery[cascade]);
					if (changed[cascade]) {
						next[cascade]->revision = ++cache.revision;
						cache.published[cascade].store(std::move(next[cascade]));
					}
				}
				cache.pending = false;
			} catch (...) {
				std::lock_guard lock(cache.mutex);
				if (a_generation == cache.generation)
					cache.pending = false;
			}
		}
	}

	void RequestUpdate(float a_time, float a_radius)
	{
		if (!std::isfinite(a_time) || !std::isfinite(a_radius))
			return;
		const auto* tasks = SKSE::GetTaskInterface();
		if (!tasks)
			return;
		auto& cache = GetCache();
		std::unique_lock lock(cache.mutex);
		if (cache.pending || cache.loading || a_time < cache.nextUpdate)
			return;
		cache.nextUpdate = a_time + kDiscoveryInterval;
		cache.pending = true;
		const uint64_t generation = cache.generation;
		lock.unlock();
		try {
			tasks->AddTask([generation, a_time, a_radius] { RunUpdate(generation, a_time, a_radius); });
		} catch (...) {
			lock.lock();
			if (generation == cache.generation)
				cache.pending = false;
		}
	}

	void Reset(bool a_loading)
	{
		auto& cache = GetCache();
		std::lock_guard lock(cache.mutex);
		++cache.generation;
		cache.pending = false;
		cache.loading = a_loading;
		cache.nextUpdate = 0.0f;
		cache.discovery = {};
		cache.worldHeightmap.reset();
		cache.worldspaceId = 0;
		for (auto& field : cache.published)
			field.store(nullptr);
	}

	std::shared_ptr<const TerrainHeightmap> WorldSnapshot(uint32_t a_worldspaceId)
	{
		auto& cache = GetCache();
		std::lock_guard lock(cache.mutex);
		return cache.worldspaceId == a_worldspaceId ? cache.worldHeightmap : nullptr;
	}

	std::shared_ptr<const Heightfield> Snapshot()
	{
		return GetCache().published[0].load();
	}

	std::shared_ptr<const Heightfield> MiddleSnapshot()
	{
		return GetCache().published[1].load();
	}

	std::shared_ptr<const Heightfield> FarSnapshot()
	{
		return GetCache().published[2].load();
	}

	float3 SampleGround(const Heightfield& a_field, const Parameters& a_parameters, float a_x, float a_y) noexcept
	{
		if (!std::isfinite(a_x) || !std::isfinite(a_y) || !std::isfinite(a_parameters.terrain.x) ||
			!std::isfinite(a_parameters.terrain.y) || !std::isfinite(a_parameters.terrain.z) ||
			!std::isfinite(a_parameters.terrain.w) || a_parameters.terrain.z <= 0.0f)
			return {};
		return a_field.valid ? SampleTerrain(a_field, a_parameters, a_x, a_y) : float3{};
	}

	float EvaluateNode(const Heightfield& a_field, const Parameters& a_parameters,
		uint32_t a_x, uint32_t a_y, uint32_t a_z) noexcept
	{
		if (!a_field.valid || a_parameters.response.x <= 0.0f)
			return 1.0f;
		float3 position{ a_parameters.volumeOrigin.x + a_x * a_parameters.volumeStep.x,
			a_parameters.volumeOrigin.y + a_y * a_parameters.volumeStep.y,
			0.0f };
		const auto ground = SampleTerrain(a_field, a_parameters, position.x, position.y);
		if (ground.z <= 0.0f || a_z >= kVolumeDepth)
			return 1.0f;
		position.z = ground.x + kHeightLevels[a_z] * a_parameters.volumeStep.z;
		return EvaluateExposure(a_field, a_parameters, position);
	}

	namespace
	{
		float4 SampleCascade(const Heightfield& a_field, const Parameters& a_parameters,
			const ObstacleShelter::Frame* a_obstacles, const float4& a_obstacleResponse,
			const float3& a_position, uint32_t a_cascade, const Heightfield* a_far = nullptr,
			const Parameters* a_farParameters = nullptr, const TerrainHeightmap* a_world = nullptr) noexcept
		{
			if (!a_field.valid || a_parameters.volumeStep.x <= 0.0f || a_parameters.volumeStep.z <= 0.0f)
				return { 1.0f, 1.0f, 1.0f, 0.0f };
			float3 coordinate{ (a_position.x - a_parameters.volumeOrigin.x) / a_parameters.volumeStep.x,
				(a_position.y - a_parameters.volumeOrigin.y) / a_parameters.volumeStep.y, 0.0f };
			if (!std::isfinite(coordinate.x) || !std::isfinite(coordinate.y) || !std::isfinite(a_position.z))
				return { 1.0f, 1.0f, 1.0f, 0.0f };
			const float edge = std::min({ coordinate.x, coordinate.y,
				kVolumeWidth - 1 - coordinate.x, kVolumeWidth - 1 - coordinate.y });
			if (edge <= 0.0f)
				return { 1.0f, 1.0f, 1.0f, 0.0f };
			const auto lowerX = static_cast<uint32_t>(coordinate.x);
			const auto lowerY = static_cast<uint32_t>(coordinate.y);
			float3 fraction{ coordinate.x - lowerX, coordinate.y - lowerY, 0.0f };
			float groundHeight = 0.0f;
			float validWeight = 0.0f;
			std::array<float3, 4> ground;
			for (uint32_t y = 0; y < 2; ++y) {
				for (uint32_t x = 0; x < 2; ++x) {
					auto& node = ground[y * 2 + x];
					node = SampleTerrain(a_field, a_parameters,
						a_parameters.volumeOrigin.x + (lowerX + x) * a_parameters.volumeStep.x,
						a_parameters.volumeOrigin.y + (lowerY + y) * a_parameters.volumeStep.y);
					if (a_world && node.z < 1.0f) {
						float worldHeight;
						if (a_world->Sample(a_parameters.volumeOrigin.x + (lowerX + x) * a_parameters.volumeStep.x,
							a_parameters.volumeOrigin.y + (lowerY + y) * a_parameters.volumeStep.y, worldHeight))
							node = { std::lerp(worldHeight, node.x, node.z), node.y + 1.0f - node.z, 1.0f };
					}
					const float weight = (x ? fraction.x : 1.0f - fraction.x) * (y ? fraction.y : 1.0f - fraction.y);
					groundHeight += node.x * node.z * weight;
					validWeight += node.z * weight;
				}
			}
			if (validWeight <= 0.0f)
				return { 1.0f, 1.0f, 1.0f, 0.0f };
			const float height = std::max((a_position.z - groundHeight / validWeight) / a_parameters.volumeStep.z, 0.0f);
			if (height >= kHeightLevels.back())
				return { 1.0f, 1.0f, 1.0f, 0.0f };
			uint32_t lowerZ = 0;
			while (lowerZ < kVolumeDepth - 2 && height >= kHeightLevels[lowerZ + 1])
				++lowerZ;
			fraction.z = (height - kHeightLevels[lowerZ]) / (kHeightLevels[lowerZ + 1] - kHeightLevels[lowerZ]);
			coordinate.z = lowerZ + fraction.z;
			struct Memo
			{
				const TerrainHeightmap* world{};
				const Heightfield* field{};
				const Heightfield* farField{};
				const ObstacleShelter::Frame* obstacles{};
				uint64_t revision = std::numeric_limits<uint64_t>::max();
				std::array<uint64_t, ObstacleShelter::kFieldCount> obstacleRevisions{};
				Parameters parameters{};
				Parameters farParameters{};
				uint64_t farRevision{};
				float4 obstacleResponse{};
				uint32_t generation{};
				std::array<uint32_t, kVolumeSampleCount> stamps{};
				std::array<float3, kVolumeSampleCount> values{};
			};
			thread_local std::array<Memo, kCascadeCount> memos;
			auto& memo = memos[a_cascade];
			std::array<uint64_t, ObstacleShelter::kFieldCount> obstacleRevisions{};
			if (a_obstacles)
				for (uint32_t index = 0; index < obstacleRevisions.size(); ++index)
					obstacleRevisions[index] = a_obstacles->fields[index].revision;
			if (memo.world != a_world || memo.field != &a_field || memo.revision != a_field.revision || memo.obstacles != a_obstacles ||
				memo.farField != a_far || memo.farRevision != (a_far ? a_far->revision : 0) ||
				(a_farParameters && std::memcmp(&memo.farParameters, a_farParameters, sizeof(Parameters)) != 0) ||
				memo.obstacleRevisions != obstacleRevisions || memo.obstacleResponse != a_obstacleResponse ||
				std::memcmp(&memo.parameters, &a_parameters, sizeof(Parameters)) != 0) {
				memo.world = a_world;
				memo.field = &a_field;
				memo.farField = a_far;
				memo.revision = a_field.revision;
				memo.obstacles = a_obstacles;
				memo.obstacleRevisions = obstacleRevisions;
				memo.obstacleResponse = a_obstacleResponse;
				memo.parameters = a_parameters;
				memo.farRevision = a_far ? a_far->revision : 0;
				memo.farParameters = a_farParameters ? *a_farParameters : Parameters{};
				if (++memo.generation == 0) {
					memo.stamps.fill(0);
					++memo.generation;
				}
			}
			float3 exposure{};
			for (uint32_t z = 0; z < 2; ++z) {
				for (uint32_t y = 0; y < 2; ++y) {
					for (uint32_t x = 0; x < 2; ++x) {
						const uint32_t index = ((lowerZ + z) * kVolumeWidth + lowerY + y) * kVolumeWidth + lowerX + x;
						if (memo.stamps[index] != memo.generation) {
							float terrain = 1.0f;
							float obstacle = 1.0f;
							if (ground[y * 2 + x].z > 0.0f) {
								const float3 node{ a_parameters.volumeOrigin.x + (lowerX + x) * a_parameters.volumeStep.x,
									a_parameters.volumeOrigin.y + (lowerY + y) * a_parameters.volumeStep.y,
									ground[y * 2 + x].x + kHeightLevels[lowerZ + z] * a_parameters.volumeStep.z };
								terrain = EvaluateExposure(a_field, a_parameters, node, a_far, a_farParameters, a_world);
								if (a_obstacles)
									obstacle = ObstacleShelter::SampleExposure(*a_obstacles, a_obstacleResponse, node, a_field.worldspaceId);
							}
							memo.values[index] = { std::min(terrain, obstacle), terrain, obstacle };
							memo.stamps[index] = memo.generation;
						}
						const float weight = (x ? fraction.x : 1.0f - fraction.x) *
						                     (y ? fraction.y : 1.0f - fraction.y) * (z ? fraction.z : 1.0f - fraction.z);
						exposure += memo.values[index] * weight;
					}
				}
			}
			return { exposure.x, exposure.y, exposure.z,
				SmoothUnit(edge / kBoundaryCells) * validWeight * SmoothUnit(kVolumeDepth - 1 - coordinate.z) };
		}
	}

	float SampleExposure(const Heightfield& a_field, const Parameters& a_parameters,
		const float3& a_position) noexcept
	{
		const auto exposure = SampleCascade(a_field, a_parameters, nullptr, {}, a_position, 0);
		return std::lerp(1.0f, exposure.x, exposure.w);
	}

	float3 SampleExposureComponents(const Heightfield* a_near, const Parameters& a_nearParameters,
		const Heightfield* a_far, const Parameters& a_farParameters, const ObstacleShelter::Frame* a_obstacles,
		const float4& a_obstacleResponse, const float3& a_position, const TerrainHeightmap* a_world,
		const Heightfield* a_middle, const Parameters* a_middleParameters) noexcept
	{
		float4 nearSample{ 1.0f, 1.0f, 1.0f, 0.0f };
		if (a_near) {
			nearSample = SampleCascade(*a_near, a_nearParameters, a_obstacles, a_obstacleResponse, a_position, 0,
				a_far, &a_farParameters, a_world);
			if (nearSample.w >= 1.0f)
				return { nearSample.x, nearSample.y, nearSample.z };
		}
		const auto middleSample = a_middle && a_middleParameters ? SampleCascade(*a_middle, *a_middleParameters, a_obstacles, a_obstacleResponse,
			a_position, 1, a_far, &a_farParameters, a_world) : float4{ 1.0f, 1.0f, 1.0f, 0.0f };
		if (middleSample.w >= 1.0f) {
			float3 exposure{ middleSample.x, middleSample.y, middleSample.z };
			return exposure + (float3{ nearSample.x, nearSample.y, nearSample.z } - exposure) * nearSample.w;
		}
		const auto farSample = a_far ? SampleCascade(*a_far, a_farParameters, a_obstacles, a_obstacleResponse,
			a_position, 2, nullptr, nullptr, a_world) : float4{ 1.0f, 1.0f, 1.0f, 0.0f };
		const float worldExposure = farSample.w < 1.0f ? WorldExposure(a_world, a_near && a_near->valid ? a_nearParameters : a_farParameters, a_position) : 1.0f;
		float3 exposure{ worldExposure, worldExposure, 1.0f };
		exposure += (float3{ farSample.x, farSample.y, farSample.z } - exposure) * farSample.w;
		exposure += (float3{ middleSample.x, middleSample.y, middleSample.z } - exposure) * middleSample.w;
		exposure += (float3{ nearSample.x, nearSample.y, nearSample.z } - exposure) * nearSample.w;
		return exposure;
	}
}
