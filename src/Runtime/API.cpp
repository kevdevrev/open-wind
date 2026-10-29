#include "OpenWind/API.h"
#include "Runtime/Runtime.h"
#include "OpenWind/UIAPI.h"
#include "Runtime/TerrainShelter.h"
#include "Runtime/TerrainHeightmap.h"
#include "Runtime/UI/Controls.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace
{
	OpenWind::Vector3 ToVector(const float3& a_value)
	{
		return { a_value.x, a_value.y, a_value.z };
	}

	bool SampleWind(const OpenWind::Vector3* a_positions, OpenWind::WindSample* a_samples,
		uint32_t a_count) noexcept
	{
		if (a_count > OpenWind::kMaximumBatchSize || (a_count && (!a_positions || !a_samples)))
			return false;
		if (!a_count)
			return true;
		try {
			auto& wind = Wind::Get();
			if (!wind.IsReady())
				return false;
			thread_local std::vector<float3> positions;
			thread_local std::vector<Wind::PublishedWindSample> samples;
			positions.resize(a_count);
			samples.resize(a_count);
			for (uint32_t index = 0; index < a_count; ++index) {
				const auto& position = a_positions[index];
				if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
					return false;
				positions[index] = { position.x, position.y, position.z };
			}
			if (!wind.SamplePublishedWind(positions, samples))
				return false;
			for (uint32_t index = 0; index < a_count; ++index) {
				const auto& sample = samples[index];
				a_samples[index] = {
					ToVector(sample.baseVelocity), ToVector(sample.gustVelocity), ToVector(sample.fineVelocity),
					ToVector(sample.transientVelocity), ToVector(sample.transientVelocityExcludingHavokImpulses),
					ToVector(sample.finalVelocity), ToVector(sample.windExcludingHavokImpulses),
					sample.ambientGust, sample.transientIntensity, sample.terrainExposure,
					sample.obstacleExposure, sample.windExposure, sample.frameId
				};
			}
			return true;
		} catch (...) {
			return false;
		}
	}

	bool Bind(const OpenWind::RendererHost* a_host) noexcept
	{
		try {
			return a_host && Wind::Get().BindRenderer(*a_host);
		} catch (...) {
			return false;
		}
	}

	bool CopyFrame(OpenWind::RendererFrame* a_frame) noexcept
	{
		if (!a_frame || a_frame->structSize != sizeof(*a_frame))
			return false;
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			if (!wind.IsRendererBound())
				return false;
			wind.CopyRendererFrame(*a_frame);
			return true;
		} catch (...) {
			return false;
		}
	}

	bool Update(OpenWind::RendererFrame* a_frame) noexcept
	{
		if (!a_frame || a_frame->structSize != sizeof(*a_frame))
			return false;
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			if (!wind.IsRendererBound())
				return false;
			wind.Reset();
			wind.CopyRendererFrame(*a_frame);
			return true;
		} catch (...) {
			return false;
		}
	}

	void DataLoaded() noexcept
	{
		try {
			Wind::Get().DataLoaded();
		} catch (...) {
			logger::error("Open Wind could not initialize effect sources");
		}
	}

	void ResetScene(bool a_opening) noexcept
	{
		try {
			Wind::Get().OnSceneTransitionReset(a_opening);
		} catch (...) {
			logger::error("Open Wind could not reset effect sources");
		}
	}

	bool SetSettings(const WindSettings* a_settings, uint32_t a_size) noexcept
	{
		if (!a_settings || a_size != sizeof(*a_settings))
			return false;
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			wind.settings = *a_settings;
			Wind::SanitizeSettings(wind.settings);
			wind.MarkSettingsConfigured();
			return true;
		} catch (...) {
			return false;
		}
	}

	bool GetSettings(char* a_buffer, uint32_t* a_size) noexcept
	{
		if (!a_size)
			return false;
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			json settings;
			wind.SaveSettings(settings);
			const auto serialized = settings.dump();
			if (serialized.size() >= std::numeric_limits<uint32_t>::max())
				return false;
			const auto required = static_cast<uint32_t>(serialized.size() + 1);
			const auto capacity = *a_size;
			*a_size = required;
			if (!a_buffer)
				return true;
			if (capacity < required)
				return false;
			std::memcpy(a_buffer, serialized.c_str(), required);
			return true;
		} catch (...) {
			return false;
		}
	}

	template <bool Import>
	bool LoadSettings(const char* a_json) noexcept
	{
		if (!a_json)
			return false;
		try {
			auto settings = json::parse(a_json);
			if (!settings.is_object())
				return false;
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			if constexpr (Import)
				return wind.ImportSettings(settings);
			wind.LoadSettings(settings);
			return true;
		} catch (...) {
			return false;
		}
	}

	bool SetControls(const WindRuntimeState* a_controls, uint32_t a_size) noexcept
	{
		if (!a_controls || a_size != sizeof(*a_controls))
			return false;
		constexpr float kMaximumTestSpeed = 2.0f;
		constexpr float kMaximumDirectionDegrees = 180.0f;
		const auto inRange = [](float value, float minimum, float maximum) {
			return std::isfinite(value) && value >= minimum && value <= maximum;
		};
		using namespace WindSettingsLimits;
		const auto& test = a_controls->treeWindTest;
		if (!inRange(a_controls->windFieldOverrideSpeed, 0.0f, kMaximumTestSpeed) ||
			!inRange(a_controls->windFieldPendingDirectionDegrees, -kMaximumDirectionDegrees, kMaximumDirectionDegrees) ||
			!inRange(a_controls->windFieldAppliedDirectionDegrees, -kMaximumDirectionDegrees, kMaximumDirectionDegrees) ||
			!inRange(test.speed, 0.0f, kMaximumTestSpeed) ||
			!inRange(test.gustScale, kWindFieldGustScaleMin, kWindFieldGustScaleMax) ||
			!inRange(test.gustAmplitude, kWindFieldGustAmplitudeMin, kWindFieldGustAmplitudeMax) ||
			!inRange(test.gustAdvectionMultiplier, kWindFieldGustAdvectionMultiplierMin, kWindFieldGustAdvectionMultiplierMax) ||
			a_controls->windFieldDebugView > WindFieldDebugView::TreeSpringHeight)
			return false;
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			wind.runtimeState = *a_controls;
			return true;
		} catch (...) {
			return false;
		}
	}

	void RestoreDefaults() noexcept
	{
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			wind.RestoreDefaultSettings();
			wind.MarkSettingsConfigured();
		} catch (...) {
			logger::error("Open Wind could not restore defaults");
		}
	}

	bool RunAction(const char* a_json) noexcept
	{
		if (!a_json)
			return false;
		try {
			const auto request = json::parse(a_json);
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			const auto action = request.at("action").get<std::string>();
			if (action == "spawnDebugPulse") {
				if (!request.at("strength").is_number() || !request.at("radius").is_number())
					return false;
				return wind.SpawnDebugWindPulse(request.at("strength").get<float>(), request.at("radius").get<float>());
			}
			if (action == "spawnDebugEffects") {
				wind.SpawnDebugWindEffects(request.at("count").get<int>(), request.at("worstCase").get<bool>());
				return true;
			}
			if (action == "resetEffect" || action == "effectAvailable") {
				const auto id = request.at("id").get<std::string>();
				for (auto& effect : wind.windEffects) {
					if (effect->GetId() == id) {
						if (action == "effectAvailable")
							return effect->IsAvailable();
						effect->RestoreDefaultSettings();
						wind.MarkSettingsConfigured();
						return true;
					}
				}
			}
		} catch (...) {
			return false;
		}
		return false;
	}

	const OpenWind::API publicAPI{
		sizeof(OpenWind::API), OpenWind::kAPIRevision, sizeof(OpenWind::WindSample), OpenWind::kMaximumBatchSize,
		[]() noexcept {
			try {
				return Wind::Get().IsReady();
			} catch (...) {
				return false;
			}
		},
		SampleWind
	};
	bool VisitTerrainHeightmap(uint32_t a_worldspaceId, void* a_context,
		bool (*a_visitor)(void*, const OpenWind::TerrainHeightmapView*) noexcept) noexcept
	{
		try {
			const auto map = OpenWind::TerrainShelter::WorldSnapshot(a_worldspaceId);
			if (!map || !a_visitor)
				return false;
			const OpenWind::TerrainHeightmapView view{ map->width, map->height, map->pitch, map->format,
				map->minimum, map->maximum, map->pixels.data() };
			return a_visitor(a_context, &view);
		} catch (...) {
			return false;
		}
	}

	const OpenWind::RendererAPI rendererAPI{
		sizeof(OpenWind::RendererAPI), OpenWind::kRendererRevision, sizeof(OpenWind::RendererFrame),
		Bind, Update, CopyFrame, DataLoaded, ResetScene, SetSettings, SetControls, VisitTerrainHeightmap
	};

	const OpenWind::UIAPI uiAPI{
		sizeof(OpenWind::UIAPI), OpenWind::kUIRevision,
		GetSettings, LoadSettings<false>, LoadSettings<true>, RestoreDefaults, RunAction,
		[](const char* a_request, void* a_context, OpenWind::UIResponse a_response) noexcept {
			try {
				return Wind::Get().RendererCommand(a_request, a_context, a_response);
			} catch (...) {
				return false;
			}
		},
		OpenWind::UI::GetControls
	};
}

extern "C" __declspec(dllexport) const OpenWind::API* GetOpenWindAPI(uint32_t) noexcept
{
	return &publicAPI;
}

extern "C" __declspec(dllexport) const OpenWind::RendererAPI* GetOpenWindRendererAPI(uint32_t) noexcept
{
	return &rendererAPI;
}

extern "C" __declspec(dllexport) const OpenWind::UIAPI* GetOpenWindUIAPI(uint32_t) noexcept
{
	return &uiAPI;
}
