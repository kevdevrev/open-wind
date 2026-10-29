#include "Runtime/Runtime.h"
#include "Runtime/GameContext.h"
#include "Runtime/TerrainShelter.h"
#include "Runtime/ObstacleShelter.h"
#include "Runtime/WindEffects/DragonWind.h"
#include "Runtime/WindEffects/ExplosionWindRouter.h"
#include "Runtime/WindEffects/FusRoDahWind.h"
#include "Runtime/WindEffects/HeavyImpactWindRouter.h"
#include "Runtime/WindEffects/ProjectileMagicWindRouter.h"
#include "Runtime/WindEffects/SpellShoutWindRouter.h"
#include "Runtime/WindEffects/StormCallWindRouter.h"
#include "Runtime/WindEffects/WeaponThrowVRWind.h"

Wind::Wind()
{
	windEffects.emplace_back(std::make_unique<FusRoDahWind>());
	windEffects.emplace_back(std::make_unique<DragonWind>());
	windEffects.emplace_back(std::make_unique<SpellShoutWindRouter>());
	windEffects.emplace_back(std::make_unique<ProjectileMagicWindRouter>());
	windEffects.emplace_back(std::make_unique<ExplosionWindRouter>());
	windEffects.emplace_back(std::make_unique<StormCallWindRouter>());
	windEffects.emplace_back(std::make_unique<WeaponThrowVRWind>());
	windEffects.emplace_back(std::make_unique<HeavyImpactWindRouter>());
}

Wind& Wind::Get()
{
	static auto* wind = new Wind;
	return *wind;
}

bool Wind::BindRenderer(const OpenWind::RendererHost& a_host)
{
	std::lock_guard lock(stateMutex);
	if (a_host.structSize != sizeof(OpenWind::RendererHost) ||
		a_host.frameSize != sizeof(OpenWind::RendererFrame))
		return false;
	if (loaded)
		return rendererHost.context == a_host.context && rendererHost.authoring == a_host.authoring;
	rendererHost = a_host;
	loaded = true;
	InitializeEffects();
	return true;
}

bool Wind::IsReady() const noexcept
{
	std::shared_lock lock(publishedWindMutex);
	return loaded && effectsInitialized && publishedWindState.valid;
}

void Wind::DataLoaded()
{
	std::lock_guard lock(stateMutex);
	OpenWind::Game::Initialize();
	dataLoaded = true;
	InitializeEffects();
}

void Wind::InitializeEffects()
{
	if (!loaded || !dataLoaded || effectsInitialized)
		return;
	for (auto& effect : windEffects)
		effect->DataLoaded();
	effectsInitialized = true;
}

void Wind::UpdateWindEffects(float a_frameTime)
{
	for (auto& effect : windEffects)
		effect->Update(a_frameTime);
}

void Wind::OnSceneTransitionReset(bool a_opening)
{
	std::lock_guard lock(stateMutex);
	OpenWind::TerrainShelter::Reset(a_opening);
	OpenWind::ObstacleShelter::Reset(a_opening);
	worldTerrain.reset();
	terrainShelter.reset();
	farTerrainShelter.reset();
	middleTerrainShelter.reset();
	obstacleShelter.reset();
	terrainShelterRequested = false;
	obstacleShelterRequested = false;
	terrainShelterParameters = {};
	farTerrainShelterParameters = {};
	middleTerrainShelterParameters = {};
	obstacleShelterResponse = {};
	{
		std::unique_lock publishedLock(publishedWindMutex);
		publishedWindState.worldTerrain.reset();
		publishedWindState.terrainShelter.reset();
		publishedWindState.terrainShelterParameters = {};
		publishedWindState.farTerrainShelter.reset();
		publishedWindState.farTerrainShelterParameters = {};
		publishedWindState.middleTerrainShelter.reset();
		publishedWindState.middleTerrainShelterParameters = {};
		publishedWindState.obstacleShelter.reset();
		publishedWindState.obstacleShelterResponse = {};
	}
	for (auto& effect : windEffects)
		effect->Reset();
	++resetId;
}

void Wind::CopyRendererFrame(OpenWind::RendererFrame& a_frame) const
{
	a_frame.structSize = sizeof(a_frame);
	a_frame.revision = OpenWind::kRendererRevision;
	a_frame.frameId = publishedWindState.frameId;
	a_frame.resetId = resetId;
	a_frame.settings = settings;
	a_frame.controls = runtimeState;
	a_frame.tuning = windFieldTuning;
	a_frame.previousTuning = previousWindFieldTuning;
	a_frame.fine = windFieldFine;
	a_frame.previousFine = previousWindFieldFine;
	a_frame.grassFineTransport = grassFineTransport;
	a_frame.current = windFieldCurrent;
	a_frame.previous = previousWindFieldCurrent;
	a_frame.transition = windFieldTransition;
	a_frame.previousTransition = previousWindFieldTransition;
	a_frame.ambientVelocity = ambientWindVelocity;
	a_frame.selectedVelocity = windFieldSelectedVelocity;
	a_frame.previousSelectedVelocity = previousWindFieldSelectedVelocity;
	a_frame.frameTime = windFieldFrameTime;
	a_frame.ambientSpeed = windFieldAmbientSpeed;
	a_frame.advectionSpeed = windFieldAdvectionSpeed;
	a_frame.gustTravelDistance = windFieldGustTravelDistance;
	a_frame.previousGustTravelDistance = previousWindFieldGustTravelDistance;
	a_frame.travelDelta = windFieldTravelDelta;
	a_frame.transitionBlend = windFieldTransitionBlend;
	a_frame.previousTransitionBlend = previousWindFieldTransitionBlend;
	a_frame.selectedSpeed = windFieldSelectedSpeed;
	a_frame.initialized = windFieldHasPreviousSample;
	a_frame.transients = transientWindImpulses;
	a_frame.previousTransients = previousTransientWindImpulses;
	a_frame.transientCount = activeTransientWindImpulseCount;
	a_frame.previousTransientCount = previousActiveTransientWindImpulseCount;
	if (terrainShelter) {
		if (a_frame.shelter.heightfield.revision != terrainShelter->revision || !a_frame.shelter.heightfield.valid)
			a_frame.shelter.heightfield = *terrainShelter;
	} else {
		a_frame.shelter.heightfield.valid = 0;
		a_frame.shelter.heightfield.worldspaceId = 0;
		a_frame.shelter.heightfield.revision = 0;
	}
	a_frame.shelter.parameters = terrainShelterParameters;
	if (farTerrainShelter) {
		if (a_frame.shelter.farHeightfield.revision != farTerrainShelter->revision || !a_frame.shelter.farHeightfield.valid)
			a_frame.shelter.farHeightfield = *farTerrainShelter;
	} else {
		a_frame.shelter.farHeightfield.valid = 0;
		a_frame.shelter.farHeightfield.worldspaceId = 0;
		a_frame.shelter.farHeightfield.revision = 0;
	}
	a_frame.shelter.farParameters = farTerrainShelterParameters;
	if (middleTerrainShelter) {
		if (a_frame.shelter.middleHeightfield.revision != middleTerrainShelter->revision || !a_frame.shelter.middleHeightfield.valid)
			a_frame.shelter.middleHeightfield = *middleTerrainShelter;
	} else {
		a_frame.shelter.middleHeightfield.valid = 0;
		a_frame.shelter.middleHeightfield.worldspaceId = 0;
		a_frame.shelter.middleHeightfield.revision = 0;
	}
	a_frame.shelter.middleParameters = middleTerrainShelterParameters;
	for (uint32_t index = 0; index < a_frame.obstacles.fields.size(); ++index) {
		auto& field = a_frame.obstacles.fields[index];
		if (obstacleShelter) {
			if (field.revision != obstacleShelter->fields[index].revision || field.valid != obstacleShelter->fields[index].valid)
				field = obstacleShelter->fields[index];
		} else {
			field.valid = 0;
			field.worldspaceId = 0;
			field.revision = 0;
		}
	}
	a_frame.obstacles.response = obstacleShelterResponse;
}
