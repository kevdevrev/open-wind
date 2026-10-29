#include "Runtime/Runtime.h"

#include <algorithm>
#include <cmath>

#include "Runtime/GameContext.h"
#include "Runtime/TerrainShelter.h"
#include "Runtime/ObstacleShelter.h"

namespace
{
	bool IsFiniteSource(const WindField::TransientWindSource& a_source)
	{
		return std::isfinite(a_source.origin.x) && std::isfinite(a_source.origin.y) &&
		       std::isfinite(a_source.origin.z) && std::isfinite(a_source.direction.x) &&
		       std::isfinite(a_source.direction.y) && std::isfinite(a_source.direction.z) &&
		       std::isfinite(a_source.wavefrontDistance) && std::isfinite(a_source.strength) &&
		       std::isfinite(a_source.maxDistance) && std::isfinite(a_source.waveHalfWidth) &&
		       std::isfinite(a_source.propagationSpeed) && std::isfinite(a_source.coneSpreadTangent) &&
		       std::isfinite(a_source.decayTime) && std::isfinite(a_source.collisionRadius) &&
		       std::isfinite(a_source.sideSpreadTangent) && std::isfinite(a_source.sideStrength) &&
		       std::isfinite(a_source.verticalScale) && std::isfinite(a_source.falloffWidth) &&
		       std::isfinite(a_source.orientationRoll);
	}

	struct WindSelection
	{
		float3 direction;
		float speed;
	};

	float EvaluateWindResponse(float a_speed, const std::array<float, 3>& a_response)
	{
		const float speed = std::max(a_speed, 0.0f);
		if (speed <= WindSettingsLimits::kWindResponseSpeeds[0])
			return std::lerp(0.0f, a_response[0], speed / WindSettingsLimits::kWindResponseSpeeds[0]);
		if (speed <= WindSettingsLimits::kWindResponseSpeeds[1])
			return std::lerp(a_response[0], a_response[1],
				(speed - WindSettingsLimits::kWindResponseSpeeds[0]) /
					(WindSettingsLimits::kWindResponseSpeeds[1] - WindSettingsLimits::kWindResponseSpeeds[0]));
		if (speed <= WindSettingsLimits::kWindResponseSpeeds[2])
			return std::lerp(a_response[1], a_response[2],
				(speed - WindSettingsLimits::kWindResponseSpeeds[1]) /
					(WindSettingsLimits::kWindResponseSpeeds[2] - WindSettingsLimits::kWindResponseSpeeds[1]));
		return a_response[2] * speed;
	}

	WindSelection SelectWind(const Wind& a_wind, const float3& a_ambientVelocity,
		const float3& a_fallbackDirection)
	{
		const float ambientSpeed = std::sqrt(
			a_ambientVelocity.x * a_ambientVelocity.x +
			a_ambientVelocity.y * a_ambientVelocity.y +
			a_ambientVelocity.z * a_ambientVelocity.z);
		const float speed = !a_wind.loaded || a_wind.ShouldUseRealWindSpeed() ?
		                        (std::isfinite(ambientSpeed) ? std::max(ambientSpeed, 0.0f) : 0.0f) :
		                        std::max(a_wind.GetEffectiveWindOverrideSpeed(), 0.0f);
		const bool useRealDirection = !a_wind.loaded || a_wind.runtimeState.windFieldUseRealDirection;
		const float directionLength = std::hypot(a_ambientVelocity.x, a_ambientVelocity.y);
		float3 direction = a_fallbackDirection;
		if (useRealDirection && std::isfinite(directionLength) && directionLength > 0.0001f) {
			direction = float3{
				a_ambientVelocity.x / directionLength,
				a_ambientVelocity.y / directionLength,
				0.0f
			};
		} else if (!useRealDirection) {
			const float directionRadians = DirectX::XMConvertToRadians(a_wind.runtimeState.windFieldAppliedDirectionDegrees);
			direction = float3{ std::cos(directionRadians), std::sin(directionRadians), 0.0f };
		}
		return { direction, speed };
	}
}

void Wind::Reset()
{
	if (!loaded || !effectsInitialized)
		return;
	previousWindFieldTuning = windFieldTuning;
	windFieldTuning.gustAmplitude = GetEffectiveWindGustAmplitude();
	windFieldTuning.gustScale = GetEffectiveWindGustScale();
	windFieldTuning.frontAspectRatio =
		settings.windFieldGustCrosswindScale / windFieldTuning.gustScale;
	windFieldTuning.gustAdvectionMultiplier = GetEffectiveWindGustAdvectionMultiplier();
	const float gustThreshold = 1.0f - settings.windFieldGustCoverage;
	const float gustHalfSoftness = settings.windFieldGustEdgeSoftness * 0.5f;
	windFieldTuning.contrastLow = std::clamp(gustThreshold - gustHalfSoftness, 0.0f, 1.0f);
	windFieldTuning.contrastHigh = std::clamp(gustThreshold + gustHalfSoftness, 0.0f, 1.0f);
	windFieldTuning.distortionStrength = settings.enableWindFieldGustDistortion ?
	                                         settings.windFieldGustDistortionStrength :
	                                         0.0f;
	windFieldTuning.distortionScale = settings.windFieldGustDistortionScale;
	windFieldTuning.distortionSpeed = settings.windFieldGustDistortionSpeed;
	windFieldTuning.microTurbulenceStrength = RendererSupports(OpenWind::RendererCapability::FineWind) ?
	                                            settings.windFieldMicroTurbulenceStrength : 0.0f;
	windFieldTuning.fineBaselineAmplitude = settings.windFieldFineBaselineAmplitude;
	windFieldTuning.fineWindCoupling = settings.windFieldFineWindCoupling;
	windFieldTuning.fineGustCoupling = settings.windFieldFineGustCoupling;
	windFieldTuning.fineFrequency = settings.windFieldFineFrequency;
	if (!windFieldHasPreviousSample)
		previousWindFieldTuning = windFieldTuning;
	const bool gamePaused = OpenWind::Game::ui && OpenWind::Game::ui->GameIsPaused();
	const float frameTime = gamePaused ? 0.0f : std::max(RE::GetSecondsSinceLastFrame(), 0.0f);
	UpdateWindEffects(frameTime);
	AdvanceWindHistory(frameTime);
	UpdateWeatherWind();
	const float3 fallbackDirection =
		windFieldHasPreviousSample ? windFieldCurrent.direction : float3{ 1.0f, 0.0f, 0.0f };
	const auto selectedWind = SelectWind(*this, ambientWindVelocity, fallbackDirection);
	UpdateWindField(selectedWind.direction, selectedWind.speed, frameTime);
	UpdateTerrainShelter(frameTime);
	PublishWindState();
}

void Wind::UpdateTerrainShelter(float a_frameTime)
{
	using namespace OpenWind::TerrainShelter;
	const bool terrainEnabled = settings.enableTerrainShelter && RendererSupports(OpenWind::RendererCapability::TerrainShelter);
	const bool obstaclesEnabled = settings.enableObstacleShelter && RendererSupports(OpenWind::RendererCapability::ObstacleShelter);
	terrainShelterTime += std::isfinite(a_frameTime) ? std::max(a_frameTime, 0.0f) : 0.0f;
	terrainShelterParameters = {};
	farTerrainShelterParameters = {};
	middleTerrainShelterParameters = {};
	if (!terrainEnabled && !obstaclesEnabled) {
		if (terrainShelterRequested)
			OpenWind::TerrainShelter::Reset(false);
		worldTerrain.reset();
		terrainShelter.reset();
		farTerrainShelter.reset();
		middleTerrainShelter.reset();
		terrainShelterRequested = false;
	} else {
		RequestUpdate(terrainShelterTime, std::max(settings.terrainShelterRadius,
			obstaclesEnabled ? settings.obstacleShelterRadius : 0.0f));
		terrainShelterRequested = true;
		terrainShelter = Snapshot();
		farTerrainShelter = FarSnapshot();
		middleTerrainShelter = MiddleSnapshot();
		worldTerrain = WorldSnapshot(terrainShelter ? terrainShelter->worldspaceId : 0);
		if (terrainShelter && farTerrainShelter && terrainShelter->worldspaceId != farTerrainShelter->worldspaceId)
			farTerrainShelter.reset();
		if (terrainShelter && middleTerrainShelter && terrainShelter->worldspaceId != middleTerrainShelter->worldspaceId)
			middleTerrainShelter.reset();
		const auto parameters = [&](const Heightfield& a_field, uint32_t a_cascade) {
			constexpr float kVolumeInsetCells = (kTerrainSize - kVolumeWidth) * 0.5f;
			Parameters result;
			result.terrain = { a_field.origin.x, a_field.origin.y, a_field.cellSize, terrainShelterTime };
			result.volumeOrigin = { a_field.origin.x + kVolumeInsetCells * a_field.cellSize,
				a_field.origin.y + kVolumeInsetCells * a_field.cellSize, 0.0f, static_cast<float>(a_cascade) };
			result.volumeStep = { a_field.cellSize, a_field.cellSize, a_field.cellSize / kCellSize, settings.terrainShelterRadius };
			result.directions = { windFieldCurrent.direction.x, windFieldCurrent.direction.y,
				windFieldTransition.direction.x, windFieldTransition.direction.y };
			result.response = { terrainEnabled ? settings.terrainShelterStrength : 0.0f,
				settings.terrainShelterDistance, settings.terrainShelterSoftness, windFieldTransitionBlend };
			return result;
		};
		if (terrainShelter && terrainShelter->valid)
			terrainShelterParameters = parameters(*terrainShelter, 0);
		if (farTerrainShelter && farTerrainShelter->valid)
			farTerrainShelterParameters = parameters(*farTerrainShelter, 2);
		if (middleTerrainShelter && middleTerrainShelter->valid)
			middleTerrainShelterParameters = parameters(*middleTerrainShelter, 1);
	}
	obstacleShelterResponse = {};
	if (obstaclesEnabled) {
		OpenWind::ObstacleShelter::RequestUpdate(terrainShelterTime, settings.obstacleShelterRadius,
			settings.obstacleShelterDistance, settings.obstacleShelterStrength,
			{ windFieldCurrent.direction.x, windFieldCurrent.direction.y },
			{ windFieldTransition.direction.x, windFieldTransition.direction.y }, windFieldTransitionBlend,
			terrainShelter, farTerrainShelter, middleTerrainShelter);
		obstacleShelterRequested = true;
		obstacleShelter = OpenWind::ObstacleShelter::Snapshot();
		obstacleShelterResponse = { terrainShelterTime, settings.obstacleShelterStrength, windFieldTransitionBlend, 1.0f };
	} else {
		if (obstacleShelterRequested)
			OpenWind::ObstacleShelter::Reset(false);
		obstacleShelter.reset();
		obstacleShelterRequested = false;
	}
}

void Wind::AdvanceWindHistory(float a_frameTime)
{
	windFieldFrameTime = a_frameTime;
	previousWindFieldFine = windFieldFine;
	UpdateTransientWindImpulses(a_frameTime);
	previousWindFieldSelectedVelocity = windFieldSelectedVelocity;
	previousWindFieldGustTravelDistance = windFieldGustTravelDistance;
	previousWindFieldCurrent = windFieldCurrent;
	previousWindFieldTransition = windFieldTransition;
	previousWindFieldTransitionBlend = windFieldTransitionBlend;
}

void Wind::UpdateWeatherWind()
{
	ambientWindVelocity = float3{};
	const auto* sky = OpenWind::Game::sky;
	const float activeWindIntensity = sky && std::isfinite(sky->windSpeed) ?
	                                      std::clamp(sky->windSpeed, 0.0f, 1.0f) :
	                                      0.0f;
	float2 weatherDirection{};
	if (sky && std::isfinite(sky->windAngle)) {
		weatherDirection = float2{ std::cos(sky->windAngle), std::sin(sky->windAngle) };
	}
	const float directionLength = std::hypot(weatherDirection.x, weatherDirection.y);
	if (std::isfinite(directionLength) && directionLength > 0.0001f && std::isfinite(activeWindIntensity)) {
		weatherDirection.x /= directionLength;
		weatherDirection.y /= directionLength;
		ambientWindVelocity = float3{ weatherDirection.x * activeWindIntensity, weatherDirection.y * activeWindIntensity, 0.0f };
	}
}

void Wind::UpdateWindField(const float3& a_direction, float a_speed, float a_frameTime)
{
	const float gustAdvectionSpeed = EvaluateWindResponse(a_speed, settings.windFieldGustAdvectionResponse) *
	                                 windFieldTuning.gustAdvectionBaseSpeed *
	                                 windFieldTuning.gustAdvectionMultiplier;
	windFieldAdvectionSpeed = std::isfinite(gustAdvectionSpeed) ? std::max(gustAdvectionSpeed, 0.0f) : 0.0f;
	windFieldTravelDelta = std::isfinite(a_frameTime) ? windFieldAdvectionSpeed * std::max(a_frameTime, 0.0f) : 0.0f;
	if (!windFieldHasPreviousSample) {
		windFieldCurrent = WindField::CreateField(a_direction, a_speed);
		previousWindFieldCurrent = windFieldCurrent;
		windFieldTransition = windFieldCurrent;
		previousWindFieldTransition = windFieldCurrent;
		windFieldTransitionBlend = 1.0f;
		previousWindFieldTransitionBlend = 1.0f;
		windFieldHasPreviousSample = true;
	} else {
		constexpr float kOneDegreeDirectionChangeCosine = 0.9998477f;
		const float directionDot = windFieldCurrent.direction.x * a_direction.x +
		                           windFieldCurrent.direction.y * a_direction.y;
		if (!windFieldTransitionActive && directionDot < kOneDegreeDirectionChangeCosine) {
			windFieldTransition = windFieldCurrent;
			previousWindFieldTransition = previousWindFieldCurrent;
			windFieldCurrent = WindField::CreateField(a_direction, a_speed);
			windFieldTransitionElapsed = 0.0f;
			windFieldTransitionBlend = 0.0f;
			windFieldTransitionActive = true;
		}
	}
	WindField::SetFieldSpeed(windFieldCurrent, a_speed);
	WindField::AdvanceField(windFieldCurrent, windFieldTravelDelta);
	if (windFieldTransitionActive) {
		WindField::SetFieldSpeed(windFieldTransition, a_speed);
		WindField::AdvanceField(windFieldTransition, windFieldTravelDelta);
		windFieldTransitionElapsed += a_frameTime;
		const float configuredTransitionDuration = settings.windFieldDirectionTransitionDuration;
		const float transitionDuration = std::isfinite(configuredTransitionDuration) ?
		                                     std::clamp(configuredTransitionDuration, 0.0f, 30.0f) :
		                                     1.0f;
		windFieldTransitionBlend = transitionDuration > 0.0f ?
		                               std::clamp(windFieldTransitionElapsed / transitionDuration, 0.0f, 1.0f) :
		                               1.0f;
		if (windFieldTransitionBlend >= 1.0f)
			windFieldTransitionActive = false;
	} else {
		windFieldTransitionBlend = 1.0f;
	}
	windFieldSelectedSpeed = windFieldCurrent.speed;
	windFieldSelectedVelocity = windFieldCurrent.direction * windFieldCurrent.speed;
	windFieldAmbientSpeed = windFieldCurrent.speed;
	windFieldGustTravelDistance = windFieldCurrent.travelDistance;
	previousWindFieldSelectedVelocity = previousWindFieldCurrent.direction * previousWindFieldCurrent.speed;
	previousWindFieldGustTravelDistance = previousWindFieldCurrent.travelDistance;
	const float3 transitionVelocity = windFieldTransition.direction * windFieldTransition.speed;
	const float3 blendedVelocity = transitionVelocity +
	                               (windFieldSelectedVelocity - transitionVelocity) * windFieldTransitionBlend;
	WindField::AdvanceFineState(windFieldFine, blendedVelocity, a_frameTime, windFieldTuning);
	WindField::WindTuning grassFineTuning = windFieldTuning;
	grassFineTuning.fineFrequency *= std::max(settings.grassWindFlutterFrequency, 0.0f);
	WindField::AdvanceFineState(grassFineTransport, blendedVelocity, a_frameTime, grassFineTuning);
}

WindField::WindSample Wind::SampleWind(const float3& a_worldPosition) const noexcept
{
	auto sample = WindField::SampleField(a_worldPosition, windFieldCurrent, windFieldTuning);
	if (windFieldTransitionBlend < 1.0f) {
		const auto previousSample = WindField::SampleField(a_worldPosition, windFieldTransition, windFieldTuning);
		sample.velocity = previousSample.velocity + (sample.velocity - previousSample.velocity) * windFieldTransitionBlend;
		sample.ambientGust = previousSample.ambientGust +
		                     (sample.ambientGust - previousSample.ambientGust) * windFieldTransitionBlend;
	}
	const float3 transitionVelocity = windFieldTransition.direction * windFieldTransition.speed;
	const float3 baseVelocity = transitionVelocity +
	                            (windFieldSelectedVelocity - transitionVelocity) * windFieldTransitionBlend;
	const auto fine = WindField::SampleFineVelocity(a_worldPosition, baseVelocity,
		sample.velocity - baseVelocity, windFieldFine, windFieldTuning);
	sample.velocity += fine.slowVelocity + fine.detailVelocity;
	sample.velocity *= OpenWind::TerrainShelter::SampleExposureComponents(terrainShelter.get(), terrainShelterParameters,
		farTerrainShelter.get(), farTerrainShelterParameters, obstacleShelter.get(), obstacleShelterResponse, a_worldPosition, worldTerrain.get(),
		middleTerrainShelter.get(), &middleTerrainShelterParameters).x;
	const auto transientSample = WindField::SampleTransientImpulses(a_worldPosition,
		std::span{ transientWindImpulses }.first(activeTransientWindImpulseCount));
	sample.velocity += transientSample.velocity;
	sample.transientImpulse = std::max(sample.transientImpulse, transientSample.intensity);
	return sample;
}

WindField::WindSample Wind::SampleWind(const float3& a_worldPosition,
	const float3& a_windDirection, float a_windSpeed) const noexcept
{
	const auto field = WindField::CreateField(a_windDirection, a_windSpeed, windFieldGustTravelDistance);
	auto sample = WindField::SampleField(a_worldPosition, field, windFieldTuning);
	const float3 baseVelocity = field.direction * field.speed;
	const auto fine = WindField::SampleFineVelocity(a_worldPosition, baseVelocity,
		sample.velocity - baseVelocity, windFieldFine, windFieldTuning);
	sample.velocity += fine.slowVelocity + fine.detailVelocity;
	if (terrainShelter || middleTerrainShelter || farTerrainShelter) {
		auto parameters = terrainShelterParameters;
		parameters.directions = { field.direction.x, field.direction.y, field.direction.x, field.direction.y };
		parameters.response.w = 1.0f;
		auto farParameters = farTerrainShelterParameters;
		farParameters.directions = parameters.directions;
		farParameters.response.w = 1.0f;
		auto middleParameters = middleTerrainShelterParameters;
		middleParameters.directions = parameters.directions;
		middleParameters.response.w = 1.0f;
		auto obstacleResponse = obstacleShelterResponse;
		const bool currentDirection = field.direction == windFieldCurrent.direction;
		const bool transitionDirection = field.direction == windFieldTransition.direction;
		obstacleResponse.z = currentDirection ? 1.0f : 0.0f;
		obstacleResponse.w *= currentDirection || transitionDirection ? 1.0f : 0.0f;
		sample.velocity *= OpenWind::TerrainShelter::SampleExposureComponents(terrainShelter.get(), parameters,
			farTerrainShelter.get(), farParameters, obstacleShelter.get(), obstacleResponse, a_worldPosition, worldTerrain.get(),
			middleTerrainShelter.get(), &middleParameters).x;
	}
	const auto transientSample = WindField::SampleTransientImpulses(a_worldPosition,
		std::span{ transientWindImpulses }.first(activeTransientWindImpulseCount));
	sample.velocity += transientSample.velocity;
	sample.transientImpulse = std::max(sample.transientImpulse, transientSample.intensity);
	return sample;
}

void Wind::PublishWindState() noexcept
{
	std::scoped_lock lock(transientWindImpulseMutex, publishedWindMutex);
	publishedWindState.tuning = windFieldTuning;
	publishedWindState.current = windFieldCurrent;
	publishedWindState.transition = windFieldTransition;
	publishedWindState.transitionBlend = windFieldTransitionBlend;
	publishedWindState.fine = windFieldFine;
	publishedWindState.worldTerrain = worldTerrain;
	publishedWindState.terrainShelter = terrainShelter;
	publishedWindState.terrainShelterParameters = terrainShelterParameters;
	publishedWindState.farTerrainShelter = farTerrainShelter;
	publishedWindState.farTerrainShelterParameters = farTerrainShelterParameters;
	publishedWindState.middleTerrainShelter = middleTerrainShelter;
	publishedWindState.middleTerrainShelterParameters = middleTerrainShelterParameters;
	publishedWindState.obstacleShelter = obstacleShelter;
	publishedWindState.obstacleShelterResponse = obstacleShelterResponse;
	publishedWindState.transientCount = activeTransientWindImpulseCount;
	std::copy_n(transientWindImpulses.begin(), activeTransientWindImpulseCount,
		publishedWindState.transients.begin());
	std::copy_n(transientWindImpulsePhysics.begin(), activeTransientWindImpulseCount,
		publishedWindState.transientPhysics.begin());
	++publishedWindState.frameId;
	publishedWindState.valid = windFieldHasPreviousSample;
}

bool Wind::SamplePublishedWind(std::span<const float3> a_worldPositions,
	std::span<PublishedWindSample> a_samples) const noexcept
{
	if (a_worldPositions.size() != a_samples.size())
		return false;

	PublishedWindState state;
	{
		std::shared_lock lock(publishedWindMutex);
		if (!publishedWindState.valid)
			return false;
		state = publishedWindState;
	}
	const float3 transitionVelocity = state.transition.direction * state.transition.speed;
	const float3 currentVelocity = state.current.direction * state.current.speed;
	const float3 baseVelocity = transitionVelocity +
	                            (currentVelocity - transitionVelocity) * state.transitionBlend;
	for (std::size_t index = 0; index < a_worldPositions.size(); ++index) {
		auto ambientSample = WindField::SampleField(a_worldPositions[index], state.current, state.tuning);
		if (state.transitionBlend < 1.0f) {
			const auto transitionSample = WindField::SampleField(
				a_worldPositions[index], state.transition, state.tuning);
			ambientSample.velocity = transitionSample.velocity +
			                         (ambientSample.velocity - transitionSample.velocity) * state.transitionBlend;
			ambientSample.ambientGust = transitionSample.ambientGust +
			                            (ambientSample.ambientGust - transitionSample.ambientGust) * state.transitionBlend;
		}
		const float3 gustVelocity = ambientSample.velocity - baseVelocity;
		const auto fine = WindField::SampleFineVelocity(a_worldPositions[index], baseVelocity,
			gustVelocity, state.fine, state.tuning);
		const float3 fineVelocity = fine.slowVelocity + fine.detailVelocity;
		ambientSample.velocity += fineVelocity;
		const auto exposure = OpenWind::TerrainShelter::SampleExposureComponents(
			state.terrainShelter.get(), state.terrainShelterParameters, state.farTerrainShelter.get(),
			state.farTerrainShelterParameters, state.obstacleShelter.get(), state.obstacleShelterResponse, a_worldPositions[index], state.worldTerrain.get(),
			state.middleTerrainShelter.get(), &state.middleTerrainShelterParameters);
		ambientSample.velocity *= exposure.x;
		WindField::TransientImpulseSample transientSample{};
		float3 transientVelocityExcludingHavokImpulses{};
		for (uint32_t sourceIndex = 0; sourceIndex < state.transientCount; ++sourceIndex) {
			const auto sourceSample = WindField::SampleTransientImpulse(
				a_worldPositions[index], state.transients[sourceIndex]);
			transientSample.velocity += sourceSample.velocity;
			transientSample.intensity = std::max(transientSample.intensity, sourceSample.intensity);
			if (state.transientPhysics[sourceIndex] == TransientWindPhysics::Wind)
				transientVelocityExcludingHavokImpulses += sourceSample.velocity;
		}
		a_samples[index] = {
			baseVelocity * exposure.x,
			gustVelocity * exposure.x,
			fineVelocity * exposure.x,
			transientSample.velocity,
			transientVelocityExcludingHavokImpulses,
			ambientSample.velocity + transientSample.velocity,
			ambientSample.velocity + transientVelocityExcludingHavokImpulses,
			ambientSample.ambientGust,
			transientSample.intensity,
			exposure.y,
			exposure.z,
			exposure.x,
			state.frameId
		};
	}
	return true;
}

void Wind::QueueTransientWindImpulse(const WindField::TransientWindSource& a_impulse)
{
	QueueTransientWindSource(a_impulse, TransientWindSourceOwner::Generic,
		TransientWindSourcePriority::Wingbeat);
}

void Wind::QueueTransientWindSource(const WindField::TransientWindSource& a_source,
	TransientWindSourceOwner a_owner, TransientWindSourcePriority a_priority,
	TransientWindPhysics a_physics)
{
	std::lock_guard lock(transientWindImpulseMutex);
	pendingTransientWindSources.push_back({ a_source, a_owner, a_priority, ++transientWindSourceSequence, a_physics });
	if (pendingTransientWindSources.size() > WindField::kTransientImpulseCapacity) {
		const auto leastImportant = std::ranges::min_element(
			pendingTransientWindSources, [](const auto& left, const auto& right) {
				if (left.priority != right.priority)
					return left.priority < right.priority;
				return left.sequence < right.sequence;
			});
		pendingTransientWindSources.erase(leastImportant);
	}
}

void Wind::SetAttachedTransientWindSources(TransientWindSourceOwner a_owner,
	std::span<const TransientWindSourceSubmission> a_sources)
{
	std::lock_guard lock(transientWindImpulseMutex);
	std::erase_if(attachedTransientWindSources,
		[a_owner](const auto& source) { return source.owner == a_owner; });
	for (const auto& submission : a_sources) {
		attachedTransientWindSources.push_back(
			{ submission.source, a_owner, submission.priority, ++transientWindSourceSequence, submission.physics });
	}
}

void Wind::ClearTransientWindSources(TransientWindSourceOwner a_owner)
{
	std::lock_guard lock(transientWindImpulseMutex);
	auto removeOwned = [a_owner](auto& sources) {
		std::erase_if(sources, [a_owner](const auto& source) { return source.owner == a_owner; });
	};
	removeOwned(activeTransientWindSources);
	removeOwned(pendingTransientWindSources);
	removeOwned(attachedTransientWindSources);

	auto compactSnapshot = [a_owner](auto& sources, auto& owners, auto& physics, uint32_t& count) {
		uint32_t outputIndex = 0;
		for (uint32_t index = 0; index < count; ++index) {
			if (owners[index] == a_owner)
				continue;
			sources[outputIndex] = sources[index];
			owners[outputIndex] = owners[index];
			physics[outputIndex] = physics[index];
			++outputIndex;
		}
		for (uint32_t index = outputIndex; index < count; ++index) {
			sources[index] = {};
			owners[index] = {};
			physics[index] = {};
		}
		count = outputIndex;
	};
	compactSnapshot(transientWindImpulses, transientWindImpulseOwners, transientWindImpulsePhysics,
		activeTransientWindImpulseCount);
	compactSnapshot(previousTransientWindImpulses, previousTransientWindImpulseOwners,
		previousTransientWindImpulsePhysics,
		previousActiveTransientWindImpulseCount);
}

void Wind::ClearTransientWindImpulses()
{
	std::lock_guard lock(transientWindImpulseMutex);
	transientWindImpulses = {};
	previousTransientWindImpulses = {};
	transientWindImpulseOwners = {};
	previousTransientWindImpulseOwners = {};
	transientWindImpulsePhysics = {};
	previousTransientWindImpulsePhysics = {};
	activeTransientWindImpulseCount = 0;
	previousActiveTransientWindImpulseCount = 0;
	activeTransientWindSources.clear();
	pendingTransientWindSources.clear();
	attachedTransientWindSources.clear();
}

void Wind::UpdateTransientWindImpulses(float a_frameTime)
{
	std::lock_guard lock(transientWindImpulseMutex);

	previousTransientWindImpulses = transientWindImpulses;
	previousTransientWindImpulseOwners = transientWindImpulseOwners;
	previousTransientWindImpulsePhysics = transientWindImpulsePhysics;
	previousActiveTransientWindImpulseCount = activeTransientWindImpulseCount;

	const float frameTime = std::isfinite(a_frameTime) ? std::max(a_frameTime, 0.0f) : 0.0f;
	for (std::size_t index = 0; index < activeTransientWindSources.size();) {
		auto& impulse = activeTransientWindSources[index].source;
		const float travelDelta = std::isfinite(impulse.propagationSpeed) ?
		                              std::max(impulse.propagationSpeed, 0.0f) * frameTime :
		                              0.0f;
		impulse.wavefrontDistance += travelDelta;
		const float decayTime = std::clamp(
			std::isfinite(impulse.decayTime) ? impulse.decayTime : 0.0f, 0.0f,
			WindField::kTransientImpulseMaximumDecayTime);
		const float retentionDistance = impulse.maxDistance + std::abs(impulse.waveHalfWidth) +
		                                std::max(impulse.propagationSpeed, 0.0f) * decayTime;
		if (!std::isfinite(impulse.wavefrontDistance) || impulse.wavefrontDistance >= retentionDistance) {
			activeTransientWindSources.erase(activeTransientWindSources.begin() + index);
		} else {
			++index;
		}
	}

	std::vector<ManagedTransientWindSource> pendingSources;
	pendingSources.swap(pendingTransientWindSources);
	const std::vector<ManagedTransientWindSource>& attachedSources = attachedTransientWindSources;
	for (auto& pendingSource : pendingSources) {
		pendingSource.source.wavefrontDistance = 0.0f;
		activeTransientWindSources.push_back(pendingSource);
	}
	std::ranges::stable_sort(activeTransientWindSources, [](const auto& left, const auto& right) {
		if (left.priority != right.priority)
			return left.priority > right.priority;
		return left.sequence > right.sequence;
	});
	if (activeTransientWindSources.size() > WindField::kTransientImpulseCapacity)
		activeTransientWindSources.resize(WindField::kTransientImpulseCapacity);

	std::vector<ManagedTransientWindSource> composedSources = activeTransientWindSources;
	composedSources.insert(composedSources.end(), attachedSources.begin(), attachedSources.end());
	std::erase_if(composedSources, [](const auto& a_managed) {
		return !IsFiniteSource(a_managed.source);
	});
	std::ranges::stable_sort(composedSources, [](const auto& left, const auto& right) {
		if (left.priority != right.priority)
			return left.priority > right.priority;
		return left.sequence > right.sequence;
	});
	if (composedSources.size() > WindField::kTransientImpulseCapacity)
		composedSources.resize(WindField::kTransientImpulseCapacity);

	transientWindImpulses = {};
	transientWindImpulseOwners = {};
	transientWindImpulsePhysics = {};
	activeTransientWindImpulseCount = static_cast<uint32_t>(composedSources.size());
	for (uint32_t index = 0; index < activeTransientWindImpulseCount; ++index) {
		transientWindImpulses[index] = composedSources[index].source;
		transientWindImpulseOwners[index] = composedSources[index].owner;
		transientWindImpulsePhysics[index] = composedSources[index].physics;
	}
}
