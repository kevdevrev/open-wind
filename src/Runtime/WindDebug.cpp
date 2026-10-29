#include "Runtime/Runtime.h"
#include "OpenWind/DebugWind.h"
#include "Runtime/GameContext.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace
{
	constexpr float kDebugWindSpawnRadius = 1000.0f;
	constexpr float kWorstCaseInnerRadius = 2000.0f;
	constexpr float kWorstCaseOuterRadius = 24000.0f;
	constexpr float kWorstCaseSourceDistance = 12000.0f;
	constexpr float kWorstCaseWaveHalfWidth = 600.0f;
	constexpr float kWorstCasePropagationSpeed = 1200.0f;
	constexpr float kWorstCaseDecayTime = 3.0f;
	constexpr float kWorstCaseConeCosine = 0.85f;
	constexpr float kGoldenAngleRadians = 2.39996323f;
	constexpr float kDebugPulseWaveHalfWidth = 240.0f;
	constexpr float kDebugPulsePropagationSpeed = 1200.0f;
	constexpr float kDebugPulseDecayTime = 1.0f;
}

bool Wind::SpawnDebugWindPulse(float a_strength, float a_radius)
{
	using OpenWind::DebugWindPulse;
	if (!std::isfinite(a_strength) || !std::isfinite(a_radius) ||
		a_strength < DebugWindPulse::kMinimumStrength || a_strength > DebugWindPulse::kMaximumStrength ||
		a_radius < DebugWindPulse::kMinimumRadius || a_radius > DebugWindPulse::kMaximumRadius)
		return false;

	const auto* tasks = SKSE::GetTaskInterface();
	if (!tasks || !IsReady())
		return false;
	tasks->AddTask([a_strength, a_radius] {
		try {
			auto& wind = Wind::Get();
			std::lock_guard lock(wind.stateMutex);
			auto* const player = OpenWind::Game::player;
			if (!wind.IsReady() || !player || !player->GetParentCell())
				return;
			const auto position = player->GetPosition();
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
				return;
			wind.QueueTransientWindSource(WindField::MakeRadialWave({ position.x, position.y, position.z }, {},
				a_strength, a_radius, std::min(kDebugPulseWaveHalfWidth, a_radius),
				kDebugPulsePropagationSpeed, kDebugPulseDecayTime),
				TransientWindSourceOwner::Generic, TransientWindSourcePriority::Impact, TransientWindPhysics::Wind);
		} catch (...) {
			logger::error("Open Wind could not spawn the debug pulse");
		}
	});
	return true;
}

void Wind::SpawnDebugWindEffects(int a_count, bool a_worstCase)
{
	auto* const player = OpenWind::Game::player;
	if (!player)
		return;

	static std::mt19937 randomGenerator{ std::random_device{}() };
	static std::uniform_real_distribution<float> unitDistribution(-1.0f, 1.0f);
	static std::uniform_real_distribution<float> strengthDistribution(1.25f, 2.5f);
	static std::uniform_real_distribution<float> distanceDistribution(450.0f, 800.0f);
	static std::uniform_real_distribution<float> widthDistribution(90.0f, 180.0f);
	static std::uniform_real_distribution<float> speedDistribution(650.0f, 1100.0f);
	static std::uniform_real_distribution<float> decayDistribution(0.35f, 0.75f);

	const auto playerPosition = player->GetPosition();
	const int spawnCount = std::clamp(a_count, 1,
		static_cast<int>(WindField::kTransientImpulseCapacity));
	ClearTransientWindImpulses();
	if (a_worstCase) {
		const float innerRadiusSquared = kWorstCaseInnerRadius * kWorstCaseInnerRadius;
		const float outerRadiusSquared = kWorstCaseOuterRadius * kWorstCaseOuterRadius;
		for (int index = 0; index < spawnCount; ++index) {
			const float distribution = (static_cast<float>(index) + 0.5f) / static_cast<float>(spawnCount);
			const float radius = std::sqrt(std::lerp(innerRadiusSquared, outerRadiusSquared, distribution));
			const float angle = static_cast<float>(index) * kGoldenAngleRadians;
			const float cosine = std::cos(angle);
			const float sine = std::sin(angle);
			const float heightOffset = static_cast<float>(index % 5 - 2) * 350.0f;
			const float3 origin{ playerPosition.x + cosine * radius, playerPosition.y + sine * radius,
				playerPosition.z + heightOffset };
			const float3 direction{ -cosine, -sine, -heightOffset / kWorstCaseSourceDistance };
			QueueTransientWindImpulse(WindField::MakeDirectionalWave(origin, direction, 2.5f,
				kWorstCaseSourceDistance, kWorstCaseWaveHalfWidth, kWorstCasePropagationSpeed,
				kWorstCaseConeCosine, kWorstCaseDecayTime));
		}
	} else {
		for (int index = 0; index < spawnCount; ++index) {
			float3 offset;
			do {
				offset = float3{ unitDistribution(randomGenerator), unitDistribution(randomGenerator),
					unitDistribution(randomGenerator) };
			} while (offset.x * offset.x + offset.y * offset.y + offset.z * offset.z > 1.0f);
			offset *= kDebugWindSpawnRadius;
			const float3 origin{ playerPosition.x + offset.x, playerPosition.y + offset.y,
				playerPosition.z + offset.z };
			QueueTransientWindImpulse({ origin,
				0.0f,
				{},
				strengthDistribution(randomGenerator),
				distanceDistribution(randomGenerator),
				widthDistribution(randomGenerator),
				speedDistribution(randomGenerator),
				0.0f,
				decayDistribution(randomGenerator),
				0.0f,
				0.0f,
				0.0f });
		}
	}
	if (runtimeState.windFieldDebugView != WindFieldDebugView::Shapes)
		runtimeState.windFieldDebugView = WindFieldDebugView::TransientImpulses;
	runtimeState.visualizeWindField = true;
}
