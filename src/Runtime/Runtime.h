#pragma once

#include "OpenWind/RendererAPI.h"
#include "OpenWind/TerrainShelter.h"
#include "Runtime/WindEffects/WindEffect.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <span>
#include <vector>

using json = nlohmann::json;

/** Owns CPU wind state and effect sources; a compatible renderer advances it once per frame. */
struct Wind
{
	/** @brief Wind components sampled from an immutable published simulation state. */
	struct PublishedWindSample
	{
		float3 baseVelocity{};
		float3 gustVelocity{};
		float3 fineVelocity{};
		float3 transientVelocity{};
		float3 transientVelocityExcludingHavokImpulses{};
		float3 finalVelocity{};
		float3 windExcludingHavokImpulses{};
		float ambientGust{};
		float transientIntensity{};
		float terrainExposure = 1.0f;
		float obstacleExposure = 1.0f;
		float windExposure = 1.0f;
		uint64_t frameId{};
	};

	enum class TransientWindSourceOwner : uint8_t
	{
		Generic,
		FusRoDah,
		Dragon,
		SpellShout,
		StormCall,
		ProjectileMagic,
		WeaponThrowVR,
		Explosion,
		HeavyImpact
	};

	/** @brief Marks whether a transient's triggering event already applies native Havok force. */
	enum class TransientWindPhysics : uint8_t
	{
		Wind,
		Native
	};

	/** Controls which sources survive when the shared pool reaches capacity. */
	enum class TransientWindSourcePriority : uint8_t
	{
		Wingbeat = 10,
		Flight = 20,
		Impact = 40,
		Breath = 50,
		FusRoDah = 60
	};

	struct TransientWindSourceSubmission
	{
		WindField::TransientWindSource source;
		TransientWindSourcePriority priority;
		TransientWindPhysics physics = TransientWindPhysics::Wind;
	};

	using Settings = WindSettings;
	using PublishedSample = PublishedWindSample;
	static Wind& Get();
	bool BindRenderer(const OpenWind::RendererHost& a_host);
	/** Whether the bound renderer can evaluate this field component. */
	[[nodiscard]] bool RendererSupports(OpenWind::RendererCapability a_capability) const noexcept
	{
		return (rendererHost.capabilities & static_cast<uint32_t>(a_capability)) != 0;
	}
	[[nodiscard]] bool IsRendererBound() const noexcept { return loaded; }
	[[nodiscard]] bool IsReady() const noexcept;
	void DataLoaded();
	void OnSceneTransitionReset(bool a_opening);
	void Reset();
	void CopyRendererFrame(OpenWind::RendererFrame& a_frame) const;
	void LoadSettings(json& a_settings);
	bool ImportSettings(json& a_settings);
	void MarkSettingsConfigured() noexcept { settingsConfigured = true; }
	void SaveSettings(json& a_settings);
	void RestoreDefaultSettings();
	static void SanitizeSettings(Settings& a_settings);
	void SetTreeWindTestEnabled(bool a_enabled);
	/** Handles authoring JSON through the renderer's typed mesh operations. */
	bool RendererCommand(const char* a_request, void* a_context, OpenWind::AuthoringResponse a_response) noexcept;
	[[nodiscard]] bool SamplePublishedWind(std::span<const float3> a_worldPositions,
		std::span<PublishedWindSample> a_samples) const noexcept;
	[[nodiscard]] WindField::WindSample SampleWind(const float3& a_worldPosition) const noexcept;
	[[nodiscard]] WindField::WindSample SampleWind(const float3& a_worldPosition,
		const float3& a_direction, float a_speed) const noexcept;
	void QueueTransientWindImpulse(const WindField::TransientWindSource& a_impulse);
	void QueueTransientWindSource(const WindField::TransientWindSource& a_source,
		TransientWindSourceOwner a_owner, TransientWindSourcePriority a_priority,
		TransientWindPhysics a_physics = TransientWindPhysics::Wind);
	void SetAttachedTransientWindSources(TransientWindSourceOwner a_owner,
		std::span<const TransientWindSourceSubmission> a_sources);
	void ClearTransientWindSources(TransientWindSourceOwner a_owner);
	void ClearTransientWindImpulses();
	/** Replaces transient impulses with the requested renderer debug pattern. */
	void SpawnDebugWindEffects(int a_count, bool a_worstCase);
	/** Requests a player-centered wind-only pulse on the game thread; returns whether it was queued. */
	bool SpawnDebugWindPulse(float a_strength, float a_radius);
	void UpdateWindEffects(float a_frameTime);
	[[nodiscard]] bool ShouldUseRealWindSpeed() const { return !runtimeState.treeWindTest.enabled && runtimeState.windFieldUseRealSpeed; }
	[[nodiscard]] float GetEffectiveWindOverrideSpeed() const { return runtimeState.treeWindTest.enabled ? runtimeState.treeWindTest.speed : runtimeState.windFieldOverrideSpeed; }
	[[nodiscard]] float GetEffectiveWindGustScale() const { return runtimeState.treeWindTest.enabled ? runtimeState.treeWindTest.gustScale : settings.windFieldGustScale; }
	[[nodiscard]] float GetEffectiveWindGustAmplitude() const { return runtimeState.treeWindTest.enabled ? runtimeState.treeWindTest.gustAmplitude : settings.windFieldGustAmplitude; }
	[[nodiscard]] float GetEffectiveWindGustAdvectionMultiplier() const { return runtimeState.treeWindTest.enabled ? runtimeState.treeWindTest.gustAdvectionMultiplier : settings.windFieldGustAdvectionMultiplier; }

	Settings settings{};
	WindRuntimeState runtimeState{};
	std::vector<std::unique_ptr<WindEffect>> windEffects;
	WindField::FineState grassFineTransport{};
	std::recursive_mutex stateMutex;
	std::atomic_bool loaded{ false };

	float windFieldFrameTime = 0.0f;
	WindField::FineState windFieldFine{};
	WindField::FineState previousWindFieldFine{};
	float windFieldAmbientSpeed = 0.0f;
	float windFieldAdvectionSpeed = 0.0f;
	float windFieldGustTravelDistance = 0.0f;
	float previousWindFieldGustTravelDistance = 0.0f;
	float windFieldTravelDelta = 0.0f;
	float3 ambientWindVelocity = {};
	float3 windFieldSelectedVelocity = {};
	float3 previousWindFieldSelectedVelocity = {};
	WindField::Field windFieldCurrent{};
	WindField::Field previousWindFieldCurrent{};
	WindField::Field windFieldTransition{};
	WindField::Field previousWindFieldTransition{};
	float windFieldTransitionElapsed = 0.0f;
	float windFieldTransitionBlend = 1.0f;
	float previousWindFieldTransitionBlend = 1.0f;
	bool windFieldTransitionActive = false;
	float windFieldSelectedSpeed = 0.0f;
	bool windFieldHasPreviousSample = false;
	WindField::WindTuning windFieldTuning{};
	WindField::WindTuning previousWindFieldTuning{};

private:
	Wind();
	void InitializeEffects();
	bool dataLoaded = false;
	bool settingsConfigured = false;
	std::atomic_bool effectsInitialized{ false };
	uint64_t resetId{};
	OpenWind::RendererHost rendererHost{};
	void AdvanceWindHistory(float a_frameTime);
	void UpdateWeatherWind();
	void UpdateWindField(const float3& a_direction, float a_speed, float a_frameTime);
	void UpdateTransientWindImpulses(float a_frameTime);
	void UpdateTerrainShelter(float a_frameTime);
	std::shared_ptr<const OpenWind::TerrainShelter::TerrainHeightmap> worldTerrain;
	std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> terrainShelter;
	OpenWind::TerrainShelter::Parameters terrainShelterParameters{};
	std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> farTerrainShelter;
	OpenWind::TerrainShelter::Parameters farTerrainShelterParameters{};
	std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> middleTerrainShelter;
	OpenWind::TerrainShelter::Parameters middleTerrainShelterParameters{};
	std::shared_ptr<const OpenWind::ObstacleShelter::Frame> obstacleShelter;
	float4 obstacleShelterResponse{};
	float terrainShelterTime{};
	bool terrainShelterRequested{};
	bool obstacleShelterRequested{};
	static void SanitizeGrassWindSettings(Settings& a_settings);
	static uint32_t SanitizeGrassWindSpringTextureSize(uint32_t a_textureSize);
	struct ManagedTransientWindSource
	{
		WindField::TransientWindSource source;
		TransientWindSourceOwner owner;
		TransientWindSourcePriority priority;
		uint64_t sequence;
		TransientWindPhysics physics;
	};
	std::array<WindField::TransientWindSource, WindField::kTransientImpulseCapacity> transientWindImpulses{};
	std::array<WindField::TransientWindSource, WindField::kTransientImpulseCapacity> previousTransientWindImpulses{};
	std::array<TransientWindSourceOwner, WindField::kTransientImpulseCapacity> transientWindImpulseOwners{};
	std::array<TransientWindSourceOwner, WindField::kTransientImpulseCapacity> previousTransientWindImpulseOwners{};
	std::array<TransientWindPhysics, WindField::kTransientImpulseCapacity> transientWindImpulsePhysics{};
	std::array<TransientWindPhysics, WindField::kTransientImpulseCapacity> previousTransientWindImpulsePhysics{};
	uint32_t activeTransientWindImpulseCount = 0;
	uint32_t previousActiveTransientWindImpulseCount = 0;
	std::vector<ManagedTransientWindSource> activeTransientWindSources;
	std::vector<ManagedTransientWindSource> pendingTransientWindSources;
	std::vector<ManagedTransientWindSource> attachedTransientWindSources;
	uint64_t transientWindSourceSequence = 0;
	std::mutex transientWindImpulseMutex;
	struct PublishedWindState
	{
		WindField::WindTuning tuning{};
		WindField::Field current{};
		WindField::Field transition{};
		float transitionBlend{ 1.0f };
		WindField::FineState fine{};
		std::shared_ptr<const OpenWind::TerrainShelter::TerrainHeightmap> worldTerrain;
		std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> terrainShelter;
		OpenWind::TerrainShelter::Parameters terrainShelterParameters{};
		std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> farTerrainShelter;
		OpenWind::TerrainShelter::Parameters farTerrainShelterParameters{};
		std::shared_ptr<const OpenWind::TerrainShelter::Heightfield> middleTerrainShelter;
		OpenWind::TerrainShelter::Parameters middleTerrainShelterParameters{};
		std::shared_ptr<const OpenWind::ObstacleShelter::Frame> obstacleShelter;
		float4 obstacleShelterResponse{};
		std::array<WindField::TransientWindSource, WindField::kTransientImpulseCapacity> transients{};
		std::array<TransientWindPhysics, WindField::kTransientImpulseCapacity> transientPhysics{};
		uint32_t transientCount{};
		uint64_t frameId{};
		bool valid{};
	};
	mutable std::shared_mutex publishedWindMutex;
	PublishedWindState publishedWindState{};
	void PublishWindState() noexcept;
};
