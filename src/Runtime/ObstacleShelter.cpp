#include "Runtime/ObstacleShelter.h"

#include "Runtime/GameContext.h"
#include "Runtime/TerrainHeightmap.h"
#include "Runtime/TerrainShelter.h"

#include <chrono>
#include <limits>
#include <numeric>

namespace OpenWind::ObstacleShelter
{
	namespace
	{
		constexpr uint32_t kRaysPerTask = 32;
		constexpr auto kTaskBudget = std::chrono::microseconds(750);
		constexpr float kRefreshSeconds = 5.0f;
		constexpr float kUnknownRetrySeconds = 1.0f;
		constexpr float kCellWorldSize = 4096.0f;
		constexpr uint32_t kMaximumRayCells = 16;
		constexpr float kCloseObstacleDistance = 32.0f;
		constexpr float kDirectionTolerance = 0.00001f;
		constexpr float kGroundChangeThreshold = 1.0f;
		constexpr std::array<uint32_t, 7> kCascadeSchedule{ 0, 0, 1, 0, 1, 0, 2 };

		struct Request
		{
			float time{}, radius{}, distance{}, strength{}, blend{};
			std::array<float2, 2> directions{};
			std::array<std::shared_ptr<const TerrainShelter::Heightfield>, kCascadeCount> terrain;
			std::shared_ptr<const TerrainShelter::TerrainHeightmap> worldTerrain;
		};

		struct Cache
		{
			std::mutex mutex;
			std::atomic<std::shared_ptr<const Frame>> published;
			Request configuration;
			std::array<uint32_t, kFieldCount> cursors{};
			uint32_t scheduleCursor{};
			uint64_t generation{}, revision{};
			bool configured{}, pending{}, loading{};
		};

		Cache& GetCache()
		{
			static auto* cache = new Cache;
			return *cache;
		}

		float SmoothUnit(float a_value) noexcept
		{
			const float value = std::clamp(a_value, 0.0f, 1.0f);
			return value * value * (3.0f - 2.0f * value);
		}

		bool SameDirection(float2 a_left, float2 a_right) noexcept
		{
			return std::abs(a_left.x - a_right.x) <= kDirectionTolerance &&
			       std::abs(a_left.y - a_right.y) <= kDirectionTolerance;
		}

		float2 NormalizeDirection(float2 a_direction) noexcept
		{
			const float length = std::hypot(a_direction.x, a_direction.y);
			return std::isfinite(length) && length > kDirectionTolerance ? a_direction / length : float2{ 1.0f, 0.0f };
		}

		float CascadeRadius(uint32_t a_cascade, float a_radius) noexcept
		{
			return a_cascade < kCascadeCellSizes.size() ?
			           std::min(a_radius, kCascadeCellSizes[a_cascade] * (kWidth - 1) * 0.5f) : a_radius;
		}

		bool CascadeEnabled(uint32_t a_cascade, float a_radius) noexcept
		{
			return a_cascade == 0 || a_radius > kCascadeCellSizes[a_cascade - 1] * (kWidth - 1) * 0.5f;
		}

		float SampleValue(const float4& a_sample, float a_time) noexcept
		{
			if (a_sample.w < 0.0f)
				return 1.0f;
			const float value = std::lerp(a_sample.x, a_sample.y, SmoothUnit((a_time - a_sample.z) / kBlendSeconds));
			return std::lerp(value, 1.0f, SmoothUnit((a_time - a_sample.w - kStaleHoldSeconds) / kStaleFadeSeconds));
		}

		float2 SampleColumn(const Grid& a_grid, uint32_t a_column, float a_height, float a_time) noexcept
		{
			const auto ground = a_grid.ground[a_column];
			if (ground.y < 0.0f)
				return { 1.0f, 0.0f };
			const float height = a_height - ground.x;
			if (height >= kHeights.back())
				return { 1.0f, 1.0f };
			uint32_t low = 0;
			while (low + 2 < kHeightCount && height > kHeights[low + 1])
				++low;
			const float blend = std::clamp((height - kHeights[low]) / (kHeights[low + 1] - kHeights[low]), 0.0f, 1.0f);
			const float exposure = std::lerp(SampleValue(a_grid.samples[low * kColumnCount + a_column], a_time),
				SampleValue(a_grid.samples[(low + 1) * kColumnCount + a_column], a_time), blend);
			const auto confidence = [&](uint32_t layer) {
				const auto& sample = a_grid.samples[layer * kColumnCount + a_column];
				return sample.w < 0.0f ? 0.0f : 1.0f - SmoothUnit((a_time - sample.w - kStaleHoldSeconds) / kStaleFadeSeconds);
			};
			const float topFade = SmoothUnit((height - kHeights[kHeightCount - 2]) /
			                                (kHeights.back() - kHeights[kHeightCount - 2]));
			const float groundConfidence = SmoothUnit((a_time - ground.y) / kBlendSeconds);
			return { std::lerp(1.0f, std::lerp(exposure, 1.0f, topFade), groundConfidence),
				std::lerp(confidence(low), confidence(low + 1), blend) * groundConfidence };
		}

		float4 SampleGrid(const Grid& a_grid, const float3& a_position, float a_time, uint32_t a_worldspaceId) noexcept
		{
			if (!a_grid.valid || a_grid.cellSize <= 0.0f ||
				(a_worldspaceId && a_grid.worldspaceId != a_worldspaceId))
				return { 1.0f, 0.0f, 0.0f, 0.0f };
			const float2 coordinate{ (a_position.x - a_grid.origin.x) / a_grid.cellSize,
				(a_position.y - a_grid.origin.y) / a_grid.cellSize };
			const float edge = std::min({ coordinate.x, coordinate.y, kWidth - 1 - coordinate.x, kWidth - 1 - coordinate.y });
			if (!std::isfinite(edge) || edge <= 0.0f)
				return { 1.0f, 0.0f, 0.0f, 0.0f };
			const uint32_t lowX = static_cast<uint32_t>(coordinate.x);
			const uint32_t lowY = static_cast<uint32_t>(coordinate.y);
			const float2 blend{ coordinate.x - lowX, coordinate.y - lowY };
			float2 exposure{};
			for (uint32_t y = 0; y < 2; ++y) {
				for (uint32_t x = 0; x < 2; ++x) {
					const float weight = (x ? blend.x : 1.0f - blend.x) * (y ? blend.y : 1.0f - blend.y);
					exposure += weight * SampleColumn(a_grid, (lowY + y) * kWidth + lowX + x, a_position.z, a_time);
				}
			}
			return { exposure.x, exposure.y, 1.0f, SmoothUnit(edge / kBoundaryCells) };
		}

		float SampleCascades(const Frame& a_frame, uint32_t a_direction, const float3& a_position,
			float a_time, uint32_t a_worldspaceId) noexcept
		{
			float exposure = 1.0f;
			for (uint32_t cascade = kCascadeCount; cascade-- > 0;) {
				const auto sample = SampleGrid(a_frame.fields[cascade * kDirectionCount + a_direction], a_position, a_time, a_worldspaceId);
				const float weight = std::clamp(sample.y, 0.0f, 1.0f) * sample.w;
				exposure = exposure * (1.0f - weight) + weight - (1.0f - sample.x) * sample.w;
			}
			return exposure;
		}

		const std::array<uint32_t, kColumnCount>& ColumnOrder()
		{
			static const auto order = [] {
				std::array<uint32_t, kColumnCount> result{};
				std::iota(result.begin(), result.end(), 0u);
				const auto distance = [](uint32_t index) {
					const float x = static_cast<float>(index % kWidth) - (kWidth - 1) * 0.5f;
					const float y = static_cast<float>(index / kWidth) - (kWidth - 1) * 0.5f;
					return x * x + y * y;
				};
				std::sort(result.begin(), result.end(), [&](uint32_t left, uint32_t right) {
					const float a = distance(left), b = distance(right);
					return a == b ? left < right : a < b;
				});
				return result;
			}();
			return order;
		}

		const Grid* FindReusableGrid(const Frame* a_frame, float2 a_direction, float a_cellSize, float a_range)
		{
			if (a_frame) {
				for (const auto& field : a_frame->fields) {
					if (field.valid && field.cellSize == a_cellSize && field.range == a_range && SameDirection(field.direction, a_direction))
						return &field;
				}
			}
			return nullptr;
		}

		void PrepareGrid(Grid& a_grid, const Grid* a_previous, uint32_t a_worldspaceId,
			float2 a_origin, float a_cellSize, float a_range, float2 a_direction)
		{
			a_grid.worldspaceId = a_worldspaceId;
			a_grid.valid = 1;
			a_grid.origin = a_origin;
			a_grid.cellSize = a_cellSize;
			a_grid.range = a_range;
			a_grid.direction = a_direction;
			a_grid.ground.fill(float2{ 0.0f, -1.0f });
			a_grid.samples.fill(float4{ 1.0f, 1.0f, 0.0f, -1.0f });
			if (!a_previous || a_previous->worldspaceId != a_worldspaceId ||
				std::abs(a_origin.x - a_previous->origin.x) >= kWidth * a_cellSize ||
				std::abs(a_origin.y - a_previous->origin.y) >= kWidth * a_cellSize)
				return;
			const int offsetX = static_cast<int>(std::round((a_origin.x - a_previous->origin.x) / a_cellSize));
			const int offsetY = static_cast<int>(std::round((a_origin.y - a_previous->origin.y) / a_cellSize));
			for (uint32_t y = 0; y < kWidth; ++y) {
				for (uint32_t x = 0; x < kWidth; ++x) {
					const int previousX = static_cast<int>(x) + offsetX;
					const int previousY = static_cast<int>(y) + offsetY;
					if (previousX < 0 || previousY < 0 || previousX >= static_cast<int>(kWidth) || previousY >= static_cast<int>(kWidth))
						continue;
					const uint32_t column = y * kWidth + x;
					const uint32_t previousColumn = previousY * kWidth + previousX;
					a_grid.ground[column] = a_previous->ground[previousColumn];
					for (uint32_t height = 0; height < kHeightCount; ++height)
						a_grid.samples[height * kColumnCount + column] = a_previous->samples[height * kColumnCount + previousColumn];
				}
			}
		}

		bool ResolveGround(const Request& a_request, uint32_t a_worldspaceId, float2 a_position, float& a_height)
		{
			for (const auto& terrain : a_request.terrain) {
				if (!terrain || !terrain->valid || terrain->worldspaceId != a_worldspaceId)
					continue;
				TerrainShelter::Parameters parameters{};
				parameters.terrain = { terrain->origin.x, terrain->origin.y, terrain->cellSize, a_request.time };
				const auto ground = TerrainShelter::SampleGround(*terrain, parameters, a_position.x, a_position.y);
				if (ground.z >= 0.999f) {
					a_height = ground.x;
					return true;
				}
			}
			return a_request.worldTerrain && a_request.worldTerrain->Sample(a_position.x, a_position.y, a_height);
		}

		class StaticRayCollector : public RE::hkpClosestRayHitCollector
		{
		public:
			void AddRayHit(const RE::hkpCdBody& a_body, const RE::hkpShapeRayCastCollectorOutput& a_hitInfo) override
			{
				const auto* body = std::addressof(a_body);
				while (body->parent)
					body = body->parent;
				const auto layer = static_cast<const RE::hkpCollidable*>(body)->GetCollisionLayer();
				if (layer == RE::COL_LAYER::kStatic || layer == RE::COL_LAYER::kAnimStatic)
					RE::hkpClosestRayHitCollector::AddRayHit(a_body, a_hitInfo);
			}
		};

		bool RayIsLoaded(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, RE::bhkWorld* a_world)
		{
			const auto delta = a_end - a_start;
			const float infinity = std::numeric_limits<float>::infinity();
			const auto nextBoundary = [infinity](float origin, float distance) {
				if (distance == 0.0f)
					return infinity;
				const float cell = std::floor(origin / kCellWorldSize);
				return ((cell + (distance > 0.0f ? 1.0f : 0.0f)) * kCellWorldSize - origin) / distance;
			};
			float nextX = nextBoundary(a_start.x, delta.x);
			float nextY = nextBoundary(a_start.y, delta.y);
			const float stepX = delta.x != 0.0f ? kCellWorldSize / std::abs(delta.x) : infinity;
			const float stepY = delta.y != 0.0f ? kCellWorldSize / std::abs(delta.y) : infinity;
			float current = 0.0f;
			for (uint32_t index = 0; index < kMaximumRayCells; ++index) {
				const float next = std::min({ nextX, nextY, 1.0f });
				if (next > current) {
					const auto* cell = Game::tes->GetCell(a_start + delta * ((current + next) * 0.5f));
					if (!cell || !cell->IsAttached() || cell->GetbhkWorld() != a_world)
						return false;
				}
				if (next >= 1.0f)
					return true;
				if (nextX <= next)
					nextX += stepX;
				if (nextY <= next)
					nextY += stepY;
				current = next;
			}
			return false;
		}

		bool TraceExposure(RE::bhkWorld* a_world, const RE::NiPoint3& a_start,
			float2 a_direction, float a_range, float& a_exposure, uint32_t& a_rays)
		{
			const RE::NiPoint3 end{ a_start.x - a_direction.x * a_range, a_start.y - a_direction.y * a_range, a_start.z };
			const auto* cell = Game::tes->GetCell(a_start);
			if (!cell || !cell->IsAttached() || cell->GetbhkWorld() != a_world)
				return false;
			const float scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = a_start * scale;
			pick.rayInput.to = end * scale;
			pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);
			StaticRayCollector collector;
			collector.Reset();
			pick.closestRayHitCollector = &collector;
			{
				RE::BSReadLockGuard worldGuard(a_world->worldLock);
				++a_rays;
				a_world->PickObject(pick);
			}
			if (pick.pickFailed)
				return false;
			if (!collector.HasHit()) {
				if (!RayIsLoaded(a_start, end, a_world))
					return false;
				a_exposure = 1.0f;
				return true;
			}
			const float distance = collector.rayHit.hitFraction * a_range;
			if (!std::isfinite(distance) || distance < 0.0f || distance > a_range ||
				!RayIsLoaded(a_start, a_start + (end - a_start) * collector.rayHit.hitFraction, a_world))
				return false;
			a_exposure = SmoothUnit((distance - kCloseObstacleDistance) / std::max(a_range - kCloseObstacleDistance, 1.0f));
			return true;
		}

		bool UpdateSample(Grid& a_grid, uint32_t a_column, uint32_t a_height,
			const Request& a_request, RE::bhkWorld* a_world, uint32_t& a_rays)
		{
			auto& sample = a_grid.samples[a_height * kColumnCount + a_column];
			const float lastAttempt = sample.w < -1.0f ? -sample.w - 2.0f : sample.w;
			if ((sample.w >= 0.0f && a_request.time - sample.w < kRefreshSeconds) ||
				(sample.w < -1.0f && a_request.time - lastAttempt < kUnknownRetrySeconds) ||
				(sample.w >= 0.0f && sample.z > sample.w && a_request.time - sample.z < kUnknownRetrySeconds))
				return false;
			const float2 position{ a_grid.origin.x + (a_column % kWidth) * a_grid.cellSize,
				a_grid.origin.y + (a_column / kWidth) * a_grid.cellSize };
			const auto* cell = Game::tes->GetCell(RE::NiPoint3{ position.x, position.y, 0.0f });
			if (!cell || !cell->IsAttached() || cell->GetbhkWorld() != a_world) {
				if (sample.w >= 0.0f) {
					sample.x = sample.y;
					sample.z = a_request.time;
				} else {
					sample.w = -a_request.time - 2.0f;
				}
				return true;
			}
			float groundHeight;
			if (ResolveGround(a_request, a_grid.worldspaceId, position, groundHeight)) {
				auto& ground = a_grid.ground[a_column];
				if (ground.y < 0.0f || std::abs(groundHeight - ground.x) > kGroundChangeThreshold) {
					ground = { groundHeight, a_request.time };
					for (uint32_t height = 0; height < kHeightCount; ++height)
						a_grid.samples[height * kColumnCount + a_column] = { 1.0f, 1.0f, a_request.time, -1.0f };
				}
			}
			if (a_grid.ground[a_column].y < 0.0f) {
				sample.w = -a_request.time - 2.0f;
				return true;
			}
			const RE::NiPoint3 start{ position.x, position.y, a_grid.ground[a_column].x + kHeights[a_height] };
			float exposure;
			if (TraceExposure(a_world, start, a_grid.direction, a_grid.range, exposure, a_rays)) {
				const float previous = SampleValue(sample, a_request.time);
				sample = { previous, exposure, a_request.time, a_request.time };
			} else if (sample.w >= 0.0f) {
				sample.x = sample.y;
				sample.z = a_request.time;
			} else {
				sample.w = -a_request.time - 2.0f;
			}
			return true;
		}

		void RunUpdate(uint64_t a_generation, Request a_request) noexcept
		{
			auto& cache = GetCache();
			try {
				std::shared_ptr<const Frame> previous;
				std::array<uint32_t, kFieldCount> cursors;
				uint32_t scheduleCursor;
				{
					std::lock_guard lock(cache.mutex);
					if (a_generation != cache.generation || cache.loading)
						return;
					previous = cache.published.load();
					cursors = cache.cursors;
					scheduleCursor = cache.scheduleCursor;
				}
				const auto* player = Game::player;
				const auto* cell = player ? player->GetParentCell() : nullptr;
				const auto* worldspace = cell && !cell->IsInteriorCell() ? player->GetWorldspace() : nullptr;
				auto* world = cell && cell->IsAttached() ? cell->GetbhkWorld() : nullptr;
				const auto position = player ? player->GetPosition() : RE::NiPoint3{};
				if (!Game::tes || !worldspace || !world || !std::isfinite(position.x) || !std::isfinite(position.y)) {
					std::lock_guard lock(cache.mutex);
					if (a_generation == cache.generation) {
						cache.published.store(nullptr);
						cache.pending = false;
					}
					return;
				}
				const auto worldspaceId = worldspace->GetFormID();
				auto next = std::make_shared<Frame>();
				std::array<bool, kFieldCount> changed{};
				for (uint32_t index = 0; index < next->fields.size(); ++index) {
					const uint32_t cascade = index / kDirectionCount;
					if (!CascadeEnabled(cascade, a_request.radius))
						continue;
					const float radius = CascadeRadius(cascade, a_request.radius);
					const float cellSize = radius * 2.0f / (kWidth - 1);
					const float2 origin{ std::floor(position.x / cellSize) * cellSize - radius,
						std::floor(position.y / cellSize) * cellSize - radius };
					const auto direction = a_request.directions[index % kDirectionCount];
					const auto* reusable = FindReusableGrid(previous.get(), direction, cellSize, a_request.distance);
					if (reusable && reusable->worldspaceId == worldspaceId && reusable->origin == origin) {
						next->fields[index] = *reusable;
					} else {
						PrepareGrid(next->fields[index], reusable, worldspaceId, origin, cellSize, a_request.distance, direction);
						cursors[index] = 0;
						changed[index] = true;
					}
				}
				const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
				const auto& columns = ColumnOrder();
				const uint32_t directionCount = a_request.blend < 1.0f && !SameDirection(a_request.directions[0], a_request.directions[1]) ? kDirectionCount : 1u;
				std::array<uint32_t, kFieldCount> scanned{};
				const uint32_t scheduleSize = static_cast<uint32_t>(kCascadeSchedule.size()) * directionCount;
				uint32_t rays = 0;
				for (uint32_t visited = 0; visited < kSampleCount * scheduleSize && rays < kRaysPerTask; ++visited) {
					if (std::chrono::steady_clock::now() >= deadline)
						break;
					const uint32_t slot = scheduleCursor++ % scheduleSize;
					const uint32_t field = kCascadeSchedule[slot / directionCount] * kDirectionCount + slot % directionCount;
					if (!next->fields[field].valid || scanned[field] >= kSampleCount)
						continue;
					++scanned[field];
					const uint32_t cursor = cursors[field];
					cursors[field] = (cursor + 1) % kSampleCount;
					changed[field] |= UpdateSample(next->fields[field], columns[cursor / kHeightCount],
						cursor % kHeightCount, a_request, world, rays);
				}
				next->response = { a_request.time, a_request.strength, a_request.blend, 1.0f };
				std::lock_guard lock(cache.mutex);
				if (a_generation != cache.generation || cache.loading)
					return;
				for (uint32_t index = 0; index < changed.size(); ++index) {
					if (changed[index])
						next->fields[index].revision = ++cache.revision;
				}
				if (SameDirection(a_request.directions[0], a_request.directions[1]))
					for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
						next->fields[cascade * kDirectionCount + 1] = next->fields[cascade * kDirectionCount];
				cache.cursors = cursors;
				cache.scheduleCursor = scheduleCursor;
				if (std::any_of(changed.begin(), changed.end(), [](bool value) { return value; }) || !previous)
					cache.published.store(std::move(next));
				cache.pending = false;
			} catch (...) {
				std::lock_guard lock(cache.mutex);
				if (a_generation == cache.generation)
					cache.pending = false;
			}
		}
	}

	void RequestUpdate(float a_time, float a_radius, float a_distance, float a_strength,
		float2 a_currentDirection, float2 a_transitionDirection, float a_transitionBlend,
		std::shared_ptr<const TerrainShelter::Heightfield> a_nearTerrain,
		std::shared_ptr<const TerrainShelter::Heightfield> a_farTerrain,
		std::shared_ptr<const TerrainShelter::Heightfield> a_middleTerrain)
	{
		const auto* tasks = SKSE::GetTaskInterface();
		if (!tasks || !std::isfinite(a_time) || !std::isfinite(a_radius) || a_radius <= 0.0f ||
			!std::isfinite(a_distance) || a_distance <= 0.0f)
			return;
		const auto worldTerrain = TerrainShelter::WorldSnapshot(a_nearTerrain ? a_nearTerrain->worldspaceId : 0);
		Request request{ a_time, a_radius, a_distance, a_strength, a_transitionBlend,
			{ NormalizeDirection(a_currentDirection), NormalizeDirection(a_transitionDirection) },
			{ std::move(a_nearTerrain), std::move(a_middleTerrain), std::move(a_farTerrain) }, worldTerrain };
		auto& cache = GetCache();
		std::unique_lock lock(cache.mutex);
		if (cache.loading)
			return;
		const auto& old = cache.configuration;
		if (!cache.configured || old.radius != request.radius || old.distance != request.distance ||
			!SameDirection(old.directions[0], request.directions[0]) || !SameDirection(old.directions[1], request.directions[1])) {
			++cache.generation;
			cache.pending = false;
			cache.cursors = {};
			cache.scheduleCursor = 0;
			cache.configured = true;
			cache.configuration = request;
			const auto previous = cache.published.load();
			auto remapped = std::make_shared<Frame>();
			for (uint32_t index = 0; index < remapped->fields.size(); ++index) {
				const uint32_t cascade = index / kDirectionCount;
				if (!CascadeEnabled(cascade, a_radius))
					continue;
				const float cellSize = CascadeRadius(cascade, a_radius) * 2.0f / (kWidth - 1);
				if (const auto* reuse = FindReusableGrid(previous.get(), request.directions[index % kDirectionCount], cellSize, a_distance))
					remapped->fields[index] = *reuse;
			}
			remapped->response = { a_time, a_strength, a_transitionBlend, 1.0f };
			cache.published.store(std::move(remapped));
		}
		if (cache.pending)
			return;
		cache.pending = true;
		const uint64_t generation = cache.generation;
		lock.unlock();
		try {
			tasks->AddTask([generation, request = std::move(request)] { RunUpdate(generation, request); });
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
		cache.configured = false;
		cache.cursors = {};
		cache.scheduleCursor = 0;
		cache.configuration = {};
		cache.published.store(nullptr);
	}

	std::shared_ptr<const Frame> Snapshot()
	{
		return GetCache().published.load();
	}

	float SampleExposure(const Frame& a_frame, const float4& a_response, const float3& a_position,
		uint32_t a_worldspaceId) noexcept
	{
		if (a_response.w <= 0.0f || a_response.y <= 0.0f || !std::isfinite(a_position.x) ||
			!std::isfinite(a_position.y) || !std::isfinite(a_position.z))
			return 1.0f;
		const float current = SampleCascades(a_frame, 0, a_position, a_response.x, a_worldspaceId);
		const float blend = std::clamp(a_response.z, 0.0f, 1.0f);
		const float exposure = blend >= 1.0f ? current :
		                                       std::lerp(SampleCascades(a_frame, 1, a_position, a_response.x, a_worldspaceId), current, blend);
		return 1.0f - std::clamp(a_response.y, 0.0f, 1.0f) * (1.0f - exposure);
	}

	float SampleExposure(const Frame& a_frame, const float3& a_position) noexcept
	{
		return SampleExposure(a_frame, a_frame.response, a_position);
	}
}
