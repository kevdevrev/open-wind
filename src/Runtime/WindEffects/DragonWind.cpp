#include "DragonWind.h"

#include "FusRoDahWind.h"
#include "Runtime/Runtime.h"
#include "OpenWind/TransientWindImpulse.h"
#include "Runtime/ActorUtils.h"
#include "Runtime/GameContext.h"
#include "Runtime/WindMath.h"
#include "ShoutWindProfiles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DragonWindRuntime
{
	using namespace DragonWindLimits;
	namespace
	{
		constexpr std::size_t kFallbackActorsPerScan = 64;
		constexpr float kFallbackScanInterval = 0.5f;
		constexpr float kTrackingRefreshInterval = 0.25f;
		constexpr float kMaximumTrackedVelocity = 10000.0f;
		constexpr float kTeleportDistance = 4096.0f;
		constexpr float kVelocitySmoothingTime = 0.15f;
		constexpr float kHoverWingbeatInterval = 1.0f;
		constexpr float kCrashImpactFallbackTime = 0.15f;

		constexpr float kWakeMinimumSpeed = 50.0f;
		constexpr float kWakeMovementGraceTime = 0.15f;
		constexpr float kCrashMotionTimeout = 30.0f;
		constexpr float kCrashStoppedTime = 0.3f;
		constexpr float kWingbeatHorizontalDrift = 0.35f;

		float LengthSquared(const float3& a_value) noexcept
		{
			return a_value.x * a_value.x + a_value.y * a_value.y + a_value.z * a_value.z;
		}

		float Length(const float3& a_value) noexcept
		{
			return std::sqrt(std::max(LengthSquared(a_value), 0.0f));
		}

		float3 Normalize(const float3& a_value) noexcept
		{
			const float length = Length(a_value);
			return std::isfinite(length) && length > 1e-4f ? a_value / length : float3{};
		}

		bool IsFinite(const float3& a_value) noexcept
		{
			return std::isfinite(a_value.x) && std::isfinite(a_value.y) && std::isfinite(a_value.z);
		}

		bool IsHovering(RE::Actor& a_actor)
		{
			std::int32_t animationState{};
			std::int32_t hoveringState{};
			if (a_actor.GetGraphVariableInt("iState", animationState) &&
				a_actor.GetGraphVariableInt("iState_DragonHovering", hoveringState)) {
				return animationState == hoveringState;
			}

			return a_actor.GetFlyState() == RE::FLY_STATE::kHovering;
		}

		bool IsSustainedBreath(RE::MagicItem* a_spell)
		{
			if (!a_spell)
				return false;
			if (a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration)
				return true;
			for (const auto* effect : a_spell->effects) {
				if (!effect || !effect->baseEffect)
					continue;
				const auto* projectile = effect->baseEffect->data.projectileBase;
				if (projectile && projectile->data.types.any(RE::BGSProjectileData::Type::kFlamethrower,
					RE::BGSProjectileData::Type::kCone))
					return true;
			}
			return false;
		}

		enum class EventType
		{
			Thrust,
			WingSound,
			NormalImpact,
			ForcefulImpact,
			CrashStart,
			Shout
		};

		struct QueuedEvent
		{
			RE::ActorHandle actor;
			EventType type;
			RE::FormID spellFormID{};
		};

		struct DragonShoutRoute
		{
			uint8_t rank{};
		};

		struct DragonState
		{
			RE::ActorHandle handle;
			float3 previousPosition{};
			float3 filteredVelocity{};
			float thrustElapsed = std::numeric_limits<float>::max();
			float wingbeatElapsed = std::numeric_limits<float>::max();
			float impactElapsed = std::numeric_limits<float>::max();
			float crashFallbackRemaining{};
			bool hasPreviousPosition{};
			bool crashImpactArmed{};
			bool measuredMotionValid{};
			bool wasCruising{};
			float measuredSpeed{};
			float lastMovementElapsed = std::numeric_limits<float>::max();
			float crashMotionRemaining{};
			float stoppedTime{};
			float breathReleaseRemaining{};
			WindField::TransientWindSource breathSource{};
		};

		struct TrackingEvent
		{
			RE::ActorHandle actor;
			RE::FormID formID{};
			bool loaded{};
		};

		struct WakeSnapshot
		{
			RE::FormID formID{};
			std::string reason;
			float speed{};
			std::uint32_t flyState{};
			bool active{};
			WindField::TransientWindSource source{};
			float grassHeight{};
			bool onGround{};
			bool graphFlying{};
			float filteredSpeed{};
		};

		class Manager :
			public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
			public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
			public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static Manager& GetSingleton()
			{
				static Manager singleton;
				return singleton;
			}

			void Register()
			{
				if (registered)
					return;
				auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!eventSourceHolder || !dataHandler) {
					logger::warn("Unable to register dragon wind: game event source is unavailable");
					return;
				}
				if (auto* dragonKeywordForm = RE::TESForm::LookupByEditorID("ActorTypeDragon"))
					dragonKeyword = dragonKeywordForm->As<RE::BGSKeyword>();

				std::unordered_set<RE::FormID> fusRoDahSpells;
				FusRoDahWind::CollectOwnedMagicItems(*dataHandler, fusRoDahSpells);
				dragonShoutRoutes.clear();
				for (const auto* shout : dataHandler->GetFormArray<RE::TESShout>()) {
					if (!shout)
						continue;
					for (uint8_t rank = 0; rank < RE::TESShout::VariationIDs::kTotal; ++rank) {
						const auto* spell = shout->variations[rank].spell;
						if (spell && !fusRoDahSpells.contains(spell->GetFormID()))
							dragonShoutRoutes.insert_or_assign(spell->GetFormID(), DragonShoutRoute{ rank });
					}
				}
				eventSourceHolder->AddEventSink<RE::TESObjectLoadedEvent>(this);
				eventSourceHolder->AddEventSink<RE::TESSpellCastEvent>(this);
				registered = true;
				logger::info("Registered dragon wind tracking{} with {} non-Fus shout variations",
					dragonKeyword ? "" : " with behavior-graph fallback", dragonShoutRoutes.size());
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event,
				RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				if (!a_event)
					return RE::BSEventNotifyControl::kContinue;

				if (!a_event->loaded) {
					std::lock_guard lock(eventMutex);
					if (subscribedDragonIDs.erase(a_event->formID) != 0)
						trackingEvents.push_back({ {}, a_event->formID, false });
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_event->formID);
				if (!IsDragon(actor))
					return RE::BSEventNotifyControl::kContinue;

				bool subscribe = false;
				{
					std::lock_guard lock(eventMutex);
					subscribe = subscribedDragonIDs.insert(a_event->formID).second;
					trackingEvents.push_back({ actor->GetHandle(), a_event->formID, true });
				}
				if (subscribe)
					actor->AddAnimationGraphEventSink(this);
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
				RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!a_event || !a_event->holder)
					return RE::BSEventNotifyControl::kContinue;
				auto* actor = a_event->holder->As<RE::Actor>();
				if (!actor)
					return RE::BSEventNotifyControl::kContinue;

				const std::string_view tag = a_event->tag.c_str();
				QueuedEvent queuedEvent{ actor->GetHandle(), EventType::Thrust };
				bool queue = true;
				if (tag == "FlapThrustBegin") {
					queuedEvent.type = EventType::Thrust;
				} else if (tag.find("SoundPlay.NPCDragonWingFlap") != std::string_view::npos) {
					queuedEvent.type = EventType::WingSound;
				} else if (tag == "DragonLandEffect") {
					queuedEvent.type = EventType::NormalImpact;
				} else if (tag == "DragonForcefulLandEffect") {
					queuedEvent.type = EventType::ForcefulImpact;
				} else if (_stricmp(a_event->tag.c_str(), "flightCrashLandStart") == 0) {
					queuedEvent.type = EventType::CrashStart;
				} else {
					queue = false;
				}

				if (queue) {
					std::lock_guard lock(eventMutex);
					queuedEvents.push_back(queuedEvent);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event,
				RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!a_event || !a_event->object || !dragonShoutRoutes.contains(a_event->spell))
					return RE::BSEventNotifyControl::kContinue;
				auto* actor = a_event->object->As<RE::Actor>();
				if (!IsDragon(actor))
					return RE::BSEventNotifyControl::kContinue;

				std::lock_guard lock(eventMutex);
				const auto duplicate = std::ranges::find_if(queuedEvents, [&](const QueuedEvent& a_pending) {
					return a_pending.type == EventType::Shout &&
					       a_pending.actor == actor->GetHandle() && a_pending.spellFormID == a_event->spell;
				});
				if (duplicate == queuedEvents.end())
					queuedEvents.push_back({ actor->GetHandle(), EventType::Shout, a_event->spell });
				return RE::BSEventNotifyControl::kContinue;
			}

			void Update(float a_frameTime, const DragonWind::Settings& a_settings)
			{
				const bool trackingChanged = DrainTrackingEvents();
				if (!a_settings.enabled || a_settings.strength <= 0.0f) {
					Reset();
					return;
				}

				const float frameTime = std::isfinite(a_frameTime) ? std::max(a_frameTime, 0.0f) : 0.0f;
				fallbackScanRemaining -= frameTime;
				bool fallbackChanged = false;
				if (fallbackScanRemaining <= 0.0f) {
					fallbackChanged = ScanFallbackActors();
					fallbackScanRemaining = kFallbackScanInterval;
				}

				trackingRefreshRemaining -= frameTime;
				const bool trackingDistanceChanged =
					!std::isfinite(lastTrackingDistance) || lastTrackingDistance != a_settings.trackingDistance;
				if (trackingChanged || fallbackChanged || trackingDistanceChanged || trackingRefreshRemaining <= 0.0f) {
					RefreshTrackedDragons(a_settings.trackingDistance);
					lastTrackingDistance = a_settings.trackingDistance;
					trackingRefreshRemaining = kTrackingRefreshInterval;
				}
				AdvanceTimers(frameTime);
				DrainEvents(a_settings);

				std::vector<Wind::TransientWindSourceSubmission> wakes;
				std::vector<WakeSnapshot> snapshots;
				for (auto& entry : dragonStates) {
					auto& dragonState = entry.second;
					if (auto actor = dragonState.handle.get()) {
						UpdateVelocity(*actor, dragonState, frameTime);
						UpdateWake(*actor, dragonState, frameTime, a_settings, wakes, snapshots);
						UpdateBreath(*actor, dragonState, frameTime, a_settings, wakes);
						if (a_settings.wingbeatsEnabled &&
							IsHovering(*actor) &&
							dragonState.wingbeatElapsed >= kHoverWingbeatInterval) {
							QueueWingbeat(*actor, dragonState, a_settings);
							dragonState.wingbeatElapsed = 0.0f;
						}
					}
				}
				UpdateSoulAbsorption(a_settings, wakes);
				Wind::Get().SetAttachedTransientWindSources(Wind::TransientWindSourceOwner::Dragon, wakes);
				std::lock_guard lock(wakeMutex);
				wakeSnapshots = std::move(snapshots);
			}

			void Reset()
			{
				{
					std::lock_guard lock(eventMutex);
					queuedEvents.clear();
				}
				{
					std::lock_guard lock(wakeMutex);
					wakeSnapshots.clear();
				}
				dragonStates.clear();
				trackingRefreshRemaining = 0.0f;
				Wind::Get().ClearTransientWindSources(Wind::TransientWindSourceOwner::Dragon);
			}

			nlohmann::json GetWakeDiagnostics(const nlohmann::json& a_args)
			{
				std::lock_guard lock(wakeMutex);
				nlohmann::json result = nlohmann::json::array();
				for (const auto& snapshot : wakeSnapshots) {
					nlohmann::json row{
						{ "formID", snapshot.formID }, { "reason", snapshot.reason },
						{ "speed", snapshot.speed }, { "flyState", snapshot.flyState }, { "active", snapshot.active },
						{ "onGround", snapshot.onGround }, { "graphFlying", snapshot.graphFlying },
						{ "filteredSpeed", snapshot.filteredSpeed }
					};
					if (snapshot.active) {
						const auto& source = snapshot.source;
						row["sections"] = nlohmann::json::array();
						for (const float along : { 0.0f, WindField::kWakeWidestFraction, 0.5f, 0.75f, 1.0f }) {
							float3 center, right, up;
							WindField::GetAttachedWedgeSection(source, along, center, right, up);
							row["sections"].push_back({ { "along", along }, { "center", { center.x, center.y, center.z } },
								{ "right", { right.x, right.y, right.z } }, { "up", { up.x, up.y, up.z } } });
						}
						float3 probe, right, up;
						WindField::GetAttachedWedgeSection(source, 0.5f, probe, right, up);
						probe.z = snapshot.grassHeight;
						row["grassHeightProbe"] = { probe.x, probe.y, probe.z };
						row["grassHeightForce"] = Length(WindField::SampleTransientImpulse(probe, source).velocity);
						row["length"] = source.maxDistance;
						row["halfWidth"] = source.collisionRadius;
						row["downwardReach"] = source.verticalScale;
						if (a_args.contains("x") && a_args.contains("y") && a_args.contains("z") &&
							a_args["x"].is_number() && a_args["y"].is_number() && a_args["z"].is_number()) {
							probe = { a_args["x"].get<float>(), a_args["y"].get<float>(), a_args["z"].get<float>() };
							if (IsFinite(probe)) {
								const auto sample = WindField::SampleTransientImpulse(probe, source);
								row["probeVelocity"] = { sample.velocity.x, sample.velocity.y, sample.velocity.z };
								row["probeIntensity"] = sample.intensity;
							}
						}
					}
					result.push_back(std::move(row));
				}
				return result;
			}

		private:
			void UpdateWake(RE::Actor& a_actor, DragonState& a_state, float a_frameTime,
				const DragonWind::Settings& a_settings, std::vector<Wind::TransientWindSourceSubmission>& a_wakes,
				std::vector<WakeSnapshot>& a_snapshots)
			{
				const auto flyState = a_actor.GetFlyState();
				std::int32_t graphState{}, flyingState{};
				const bool graphFlying = a_actor.GetGraphVariableInt("iState", graphState) &&
					a_actor.GetGraphVariableInt("iState_DragonFlying", flyingState) && graphState == flyingState;
				bool onGround = false;
				a_actor.GetGraphVariableBool("IsOnGround", onGround);
				// kPerching also occurs during moving flight; ground and motion checks must gate its wake.
				const bool cruising = !a_actor.IsDead() && !onGround &&
					flyState != RE::FLY_STATE::kTakeOff && flyState != RE::FLY_STATE::kHovering &&
					flyState != RE::FLY_STATE::kLanding &&
					(flyState == RE::FLY_STATE::kCruising || flyState == RE::FLY_STATE::kPerching || graphFlying);
				if (a_state.wasCruising && a_actor.IsDead())
					a_state.crashMotionRemaining = kCrashMotionTimeout;
				a_state.wasCruising = cruising;
				const bool moving = a_state.measuredMotionValid &&
					a_state.lastMovementElapsed < kWakeMovementGraceTime &&
					Length(a_state.filteredVelocity) > kWakeMinimumSpeed;
				a_state.stoppedTime = moving ? 0.0f : a_state.stoppedTime + a_frameTime;
				a_state.crashMotionRemaining = a_state.stoppedTime >= kCrashStoppedTime ? 0.0f :
					std::max(a_state.crashMotionRemaining - a_frameTime, 0.0f);
				if (flyState == RE::FLY_STATE::kTakeOff)
					a_state.crashMotionRemaining = 0.0f;
				const bool crashing = a_state.crashMotionRemaining > 0.0f;
				const auto* root = a_actor.Get3D();
				WakeSnapshot snapshot{ a_actor.GetFormID(), "", a_state.measuredSpeed,
					static_cast<std::uint32_t>(flyState), false, {},
					OpenWind::Game::player ? OpenWind::Game::player->GetPositionZ() : 0.0f,
					onGround, graphFlying, Length(a_state.filteredVelocity) };
				if (!a_settings.wakeEnabled || a_settings.wakeStrength <= 0.0f)
					snapshot.reason = "disabled";
				else if (!root || !a_state.measuredMotionValid)
					snapshot.reason = "no valid movement sample";
				else if (!cruising && !crashing) {
					if (flyState == RE::FLY_STATE::kTakeOff)
						snapshot.reason = "taking off";
					else if (flyState == RE::FLY_STATE::kHovering)
						snapshot.reason = "hovering";
					else if (onGround)
						snapshot.reason = "on ground";
					else
						snapshot.reason = "not cruising or crashing";
				}
				else if (!moving)
					snapshot.reason = "below movement threshold";
				else {
					const auto right = root->world.rotate * RE::NiPoint3{ 1.0f, 0.0f, 0.0f };
					const float3 heading = Normalize(a_state.filteredVelocity);
					const float3 side{ right.x, right.y, right.z };
					if (IsFinite(heading) && IsFinite(side) && LengthSquared(heading) > 0.5f) {
						const float speedFraction = std::clamp(
							(Length(a_state.filteredVelocity) - kWakeMinimumSpeed) /
								(a_settings.wakeFullSizeSpeed - kWakeMinimumSpeed), 0.0f, 1.0f);
						const float scale = a_settings.wakeMaximumScale * speedFraction;
						snapshot.source = WindField::MakeAttachedWedge(Util::GetVisualOrigin(a_actor), heading, side,
							a_settings.strength * a_settings.wakeStrength * speedFraction * speedFraction,
							a_settings.wakeLength * scale, a_settings.wakeHalfWidth * scale,
							a_settings.wakeDownwardReach * scale, a_settings.wakeEdgeSoftness,
							a_settings.wakeOutwardFlow, a_settings.wakeDownwash);
						snapshot.active = true;
						snapshot.reason = crashing ? "moving crash" : "moving flight";
						a_wakes.push_back({ snapshot.source, Wind::TransientWindSourcePriority::Flight });
					} else
						snapshot.reason = "invalid body orientation";
				}
				a_snapshots.push_back(std::move(snapshot));
			}

			bool IsDragon(RE::Actor* a_actor) const
			{
				return a_actor && Util::IsDragon(*a_actor, dragonKeyword);
			}

			bool DrainTrackingEvents()
			{
				std::vector<TrackingEvent> events;
				{
					std::lock_guard lock(eventMutex);
					events.swap(trackingEvents);
				}
				for (const auto& event : events) {
					if (event.loaded)
						knownDragons[event.formID] = event.actor;
					else {
						knownDragons.erase(event.formID);
						dragonStates.erase(event.formID);
					}
				}
				return !events.empty();
			}

			bool ScanFallbackActors()
			{
				auto* processLists = RE::ProcessLists::GetSingleton();
				if (!processLists || processLists->highActorHandles.empty()) {
					fallbackActorCursor = 0;
					return false;
				}

				const auto actorCount = processLists->highActorHandles.size();
				fallbackActorCursor %= actorCount;
				const auto scanCount = std::min<std::size_t>(actorCount, kFallbackActorsPerScan);
				bool changed = false;
				for (std::size_t offset = 0; offset < scanCount; ++offset) {
					const auto index = static_cast<std::uint32_t>(
						(fallbackActorCursor + offset) % actorCount);
					const auto& handle = processLists->highActorHandles[index];
					auto actor = handle.get();
					if (!actor || !actor->Is3DLoaded() || actor->IsDead() || !IsDragon(actor.get()))
						continue;

					const auto formID = actor->GetFormID();
					bool subscribe = false;
					{
						std::lock_guard lock(eventMutex);
						subscribe = subscribedDragonIDs.insert(formID).second;
					}
					if (subscribe)
						actor->AddAnimationGraphEventSink(this);
					changed |= knownDragons.insert_or_assign(formID, handle).second;
				}
				fallbackActorCursor = (fallbackActorCursor + scanCount) % actorCount;
				return changed;
			}

			void RefreshTrackedDragons(float a_trackingDistance)
			{
				auto* player = OpenWind::Game::player;
				if (!player) {
					dragonStates.clear();
					return;
				}
				const float trackingDistance = std::isfinite(a_trackingDistance) ?
				                                   std::max(a_trackingDistance, 0.0f) :
				                                   0.0f;
				const float squaredTrackingDistance = trackingDistance * trackingDistance;
				const auto playerPosition = player->GetPosition();
				std::unordered_set<RE::FormID> trackedDragonIDs;
				trackedDragonIDs.reserve(knownDragons.size());
				for (auto iterator = knownDragons.begin(); iterator != knownDragons.end();) {
					auto actor = iterator->second.get();
					if (!actor || !actor->Is3DLoaded()) {
						const auto staleFormID = iterator->first;
						iterator = knownDragons.erase(iterator);
						dragonStates.erase(staleFormID);
						std::lock_guard lock(eventMutex);
						subscribedDragonIDs.erase(staleFormID);
						continue;
					}

					const auto formID = iterator->first;
					++iterator;
					const auto previous = dragonStates.find(formID);
					if (actor->IsDead() && (previous == dragonStates.end() ||
						(!previous->second.wasCruising && previous->second.crashMotionRemaining <= 0.0f &&
							previous->second.stoppedTime >= kCrashStoppedTime)))
						continue;
					const float squaredDistance = actor->GetPosition().GetSquaredDistance(playerPosition);
					if (squaredDistance > squaredTrackingDistance)
						continue;

					trackedDragonIDs.insert(formID);
					auto state = dragonStates.try_emplace(formID).first;
					state->second.handle = actor->GetHandle();
				}
				std::erase_if(dragonStates, [&](const auto& entry) {
					return !trackedDragonIDs.contains(entry.first);
				});
			}

			void AdvanceTimers(float a_frameTime)
			{
				for (auto& entry : dragonStates) {
					auto& state = entry.second;
					state.thrustElapsed += a_frameTime;
					state.wingbeatElapsed += a_frameTime;
					state.impactElapsed += a_frameTime;
					if (state.crashImpactArmed && state.crashFallbackRemaining > 0.0f)
						state.crashFallbackRemaining -= a_frameTime;
				}
			}

			void DrainEvents(const DragonWind::Settings& a_settings)
			{
				std::vector<QueuedEvent> events;
				{
					std::lock_guard lock(eventMutex);
					events.swap(queuedEvents);
				}
				for (const auto& event : events) {
					auto actor = event.actor.get();
					if (!actor)
						continue;
					auto iterator = dragonStates.find(actor->GetFormID());
					if (iterator == dragonStates.end())
						continue;
					auto& state = iterator->second;
					switch (event.type) {
					case EventType::Thrust:
						if (!a_settings.wingbeatsEnabled)
							break;
						if (state.wingbeatElapsed >= a_settings.wingbeatFallbackCooldown)
							QueueWingbeat(*actor, state, a_settings);
						state.thrustElapsed = 0.0f;
						state.wingbeatElapsed = 0.0f;
						break;
					case EventType::WingSound:
						if (!a_settings.wingbeatsEnabled)
							break;
						if (state.thrustElapsed >= a_settings.wingbeatFallbackCooldown &&
							state.wingbeatElapsed >= a_settings.wingbeatFallbackCooldown) {
							QueueWingbeat(*actor, state, a_settings);
							state.wingbeatElapsed = 0.0f;
						}
						break;
					case EventType::NormalImpact:
					case EventType::ForcefulImpact:
						{
							const bool crashImpact = state.crashImpactArmed && a_settings.crashesEnabled;
							if (!crashImpact && !a_settings.landingsEnabled) {
								state.crashImpactArmed = false;
								break;
							}
							const auto& impactProfile = crashImpact ?
							                                a_settings.crashImpact :
							                            event.type == EventType::NormalImpact ?
							                                a_settings.normalImpact :
							                                a_settings.forcefulImpact;
							QueueImpact(*actor, state, impactProfile, a_settings);
							state.crashImpactArmed = false;
							break;
						}
					case EventType::CrashStart:
						state.crashMotionRemaining = kCrashMotionTimeout;
						state.stoppedTime = 0.0f;
						if (!a_settings.crashesEnabled) {
							state.crashImpactArmed = false;
							break;
						}
						state.crashImpactArmed = true;
						state.crashFallbackRemaining = kCrashImpactFallbackTime;
						break;
					case EventType::Shout:
						if (a_settings.shoutsEnabled)
							QueueShout(*actor, event, a_settings);
						break;
					}
				}

				for (auto& entry : dragonStates) {
					auto& state = entry.second;
					if (state.crashImpactArmed && state.crashFallbackRemaining <= 0.0f) {
						if (a_settings.crashesEnabled) {
							if (auto actor = state.handle.get())
								QueueImpact(*actor, state, a_settings.crashImpact, a_settings);
						}
						state.crashImpactArmed = false;
					}
				}
			}

			void UpdateVelocity(RE::Actor& a_actor, DragonState& a_state, float a_frameTime)
			{
				if (a_frameTime <= 0.0f)
					return;
				a_state.lastMovementElapsed += a_frameTime;
				a_state.measuredMotionValid = false;
				a_state.measuredSpeed = 0.0f;
				const float3 position = Util::GetVisualOrigin(a_actor);
				float3 sampledVelocity{};
				RE::NiPoint3 linearVelocity{};
				a_actor.GetLinearVelocity(linearVelocity);
				const float3 engineVelocity{ linearVelocity.x, linearVelocity.y, linearVelocity.z };
				if (IsFinite(engineVelocity) && Length(engineVelocity) <= kMaximumTrackedVelocity)
					sampledVelocity = engineVelocity;

				if (a_state.hasPreviousPosition && a_frameTime > 1e-4f) {
					const float3 positionDelta = position - a_state.previousPosition;
					const float deltaLength = Length(positionDelta);
					if (std::isfinite(deltaLength) && deltaLength <= kTeleportDistance) {
						const float3 positionVelocity = positionDelta / a_frameTime;
						if (IsFinite(positionVelocity) && Length(positionVelocity) <= kMaximumTrackedVelocity) {
							sampledVelocity = positionVelocity;
							a_state.measuredSpeed = Length(positionVelocity);
							a_state.measuredMotionValid = true;
							if (a_state.measuredSpeed >= kWakeMinimumSpeed)
								a_state.lastMovementElapsed = 0.0f;
						}
					} else {
						a_state.filteredVelocity = float3{};
					}
				}
				if (!a_state.measuredMotionValid)
					a_state.lastMovementElapsed = std::numeric_limits<float>::max();
				if (a_frameTime > 0.0f) {
					const float blend = 1.0f - std::exp(-a_frameTime / kVelocitySmoothingTime);
					a_state.filteredVelocity += (sampledVelocity - a_state.filteredVelocity) * blend;
				}
				a_state.previousPosition = position;
				a_state.hasPreviousPosition = true;
			}

			void QueueWingbeat(RE::Actor& a_actor, const DragonState& a_state,
				const DragonWind::Settings& a_settings)
			{
				float3 horizontalDirection = Normalize(
					{ a_state.filteredVelocity.x, a_state.filteredVelocity.y, 0.0f });
				if (LengthSquared(horizontalDirection) <= 1e-6f) {
					const auto aimDirection = Util::GetAimDirection(a_actor);
					horizontalDirection = Normalize({ aimDirection.x, aimDirection.y, 0.0f });
				}
				const float3 downwashDirection = Normalize({ horizontalDirection.x * kWingbeatHorizontalDrift,
					horizontalDirection.y * kWingbeatHorizontalDrift,
					-1.0f });
				auto source = WindField::MakeDirectionalWave(
					Util::GetVisualOrigin(a_actor), downwashDirection,
					a_settings.strength * a_settings.wingbeatStrength,
					a_settings.wingbeatDistance, a_settings.wingbeatWaveHalfWidth,
					a_settings.wingbeatPropagationSpeed, 1.0f,
					a_settings.wingbeatDecayTime);
				source.collisionRadius = a_settings.wingbeatStartRadius;
				source.coneSpreadTangent = (a_settings.wingbeatEndRadius - a_settings.wingbeatStartRadius) /
				                          a_settings.wingbeatDistance;
				source.sideSpreadTangent = source.coneSpreadTangent;
				Wind::Get().QueueTransientWindSource(source,
					Wind::TransientWindSourceOwner::Dragon,
					Wind::TransientWindSourcePriority::Wingbeat);
			}

			void QueueImpact(RE::Actor& a_actor, DragonState& a_state,
				const DragonWind::ImpactProfile& a_profile,
				const DragonWind::Settings& a_settings)
			{
				if (a_state.impactElapsed < a_settings.impactDeduplicationTime)
					return;
				const auto actorPosition = a_actor.GetPosition();
				float3 fallbackDirection = Normalize(
					{ a_state.filteredVelocity.x, a_state.filteredVelocity.y, 0.0f });
				const auto source = WindField::MakeRadialWave(
					{ actorPosition.x, actorPosition.y, actorPosition.z }, fallbackDirection,
					a_settings.strength * a_profile.strength, a_profile.distance,
					a_profile.waveHalfWidth, a_profile.propagationSpeed,
					a_settings.impactDecayTime);
				Wind::Get().QueueTransientWindSource(source,
					Wind::TransientWindSourceOwner::Dragon,
					Wind::TransientWindSourcePriority::Impact);
				a_state.impactElapsed = 0.0f;
			}

			void UpdateSoulAbsorption(const DragonWind::Settings& a_settings,
				std::vector<Wind::TransientWindSourceSubmission>& a_sources) const
			{
				auto* player = OpenWind::Game::player;
				auto* processes = RE::ProcessLists::GetSingleton();
				if (!a_settings.soulAbsorptionEnabled || a_settings.soulAbsorptionStrength <= 0.0f || !player || !processes)
					return;
				std::unordered_set<RE::FormID> emittingDragons;
				processes->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
					if (!a_effect || a_effect->finished || !a_effect->initialized ||
						(a_effect->lifetime >= 0.0f && a_effect->age >= a_effect->lifetime))
						return RE::BSContainer::ForEachResult::kContinue;
					const auto target = a_effect->target.get();
					const auto recipient = a_effect->aimAtTarget.get();
					if (!target || recipient.get() != player)
						return RE::BSContainer::ForEachResult::kContinue;
					auto* dragon = target->As<RE::Actor>();
					if (!IsDragon(dragon) || !dragon->IsDead() || !dragon->Is3DLoaded())
						return RE::BSContainer::ForEachResult::kContinue;
					const auto dragonPosition = dragon->GetPosition();
					const auto playerPosition = player->GetPosition();
					const float3 origin{ dragonPosition.x, dragonPosition.y, dragonPosition.z };
					const float3 offset{ playerPosition.x - origin.x, playerPosition.y - origin.y, playerPosition.z - origin.z };
					const float distance = Length(offset);
					if (!IsFinite(offset) || distance <= 0.0f || distance > a_settings.trackingDistance ||
						!emittingDragons.insert(dragon->GetFormID()).second)
						return RE::BSContainer::ForEachResult::kContinue;
					float envelope = 1.0f;
					if (a_settings.soulAbsorptionFadeTime > 0.0f) {
						envelope = std::clamp(a_effect->age / a_settings.soulAbsorptionFadeTime, 0.0f, 1.0f);
						if (a_effect->lifetime > 0.0f)
							envelope *= std::clamp((a_effect->lifetime - a_effect->age) / a_settings.soulAbsorptionFadeTime, 0.0f, 1.0f);
					}
					const auto direction = offset / distance;
					const auto source = WindField::MakeOrientedFlow(origin + offset * 0.5f, direction,
						a_settings.strength * a_settings.soulAbsorptionStrength * envelope,
						a_settings.soulAbsorptionRadius, distance * 0.5f + a_settings.soulAbsorptionRadius,
						distance * 0.5f + a_settings.soulAbsorptionRadius);
					a_sources.push_back({ source, Wind::TransientWindSourcePriority::Breath });
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}

			void UpdateBreath(RE::Actor& a_actor, DragonState& a_state, float a_frameTime,
				const DragonWind::Settings& a_settings,
				std::vector<Wind::TransientWindSourceSubmission>& a_sources) const
			{
				if (!a_settings.shoutsEnabled || a_actor.IsDead()) {
					a_state.breathReleaseRemaining = 0.0f;
					return;
				}
				auto* caster = a_actor.GetMagicCaster(RE::MagicSystem::CastingSource::kOther);
				const bool casting = caster && caster->state.get() == RE::MagicCaster::State::kCasting &&
					IsSustainedBreath(caster->currentSpell);
				if (casting) {
					auto direction = Util::GetAimDirection(a_actor);
					if (const auto* magicNode = caster->GetMagicNode()) {
						const auto forward = magicNode->world.rotate.GetVectorY();
						const auto nodeDirection = Normalize({ forward.x, forward.y, forward.z });
						if (LengthSquared(nodeDirection) > 0.0f)
							direction = nodeDirection;
					}
					const float angle = a_settings.breathConeHalfAngle * (std::numbers::pi_v<float> / 180.0f);
					a_state.breathSource = WindField::MakeDirectionalCone(
						Util::GetMagicOrigin(a_actor), direction,
						a_settings.strength * a_settings.breathStrength, a_settings.breathDistance, std::cos(angle));
					a_state.breathReleaseRemaining = a_settings.breathReleaseTime;
				} else {
					a_state.breathReleaseRemaining = std::max(0.0f, a_state.breathReleaseRemaining - a_frameTime);
				}
				if (casting || a_state.breathReleaseRemaining > 0.0f) {
					auto source = a_state.breathSource;
					if (!casting)
						source.strength *= a_settings.breathReleaseTime > 0.0f ?
							std::clamp(a_state.breathReleaseRemaining / a_settings.breathReleaseTime, 0.0f, 1.0f) : 0.0f;
					a_sources.push_back({ source, Wind::TransientWindSourcePriority::Breath, Wind::TransientWindPhysics::Wind });
				}
			}

			void QueueShout(RE::Actor& a_actor, const QueuedEvent& a_event,
				const DragonWind::Settings& a_settings) const
			{
				const auto route = dragonShoutRoutes.find(a_event.spellFormID);
				if (route == dragonShoutRoutes.end() ||
					IsSustainedBreath(RE::TESForm::LookupByID<RE::SpellItem>(a_event.spellFormID)))
					return;
				const std::size_t rank = std::min<std::size_t>(
					route->second.rank, ShoutWindProfiles::kGenericDragonShoutProfiles.size() - 1);
				const auto& profile = ShoutWindProfiles::kGenericDragonShoutProfiles[rank];
				const float coneRadians = profile.coneHalfAngle * (std::numbers::pi_v<float> / 180.0f);
				const auto source = WindField::MakeDirectionalWave(
					Util::GetMagicOrigin(a_actor), Util::GetAimDirection(a_actor),
					a_settings.strength * profile.strength, profile.distance, profile.waveHalfWidth,
					profile.propagationSpeed, std::cos(coneRadians), profile.decayTime);
				Wind::Get().QueueTransientWindSource(source,
					Wind::TransientWindSourceOwner::Dragon,
					Wind::TransientWindSourcePriority::Breath);
			}

			RE::BGSKeyword* dragonKeyword{};
			std::unordered_map<RE::FormID, RE::ActorHandle> knownDragons;
			std::unordered_map<RE::FormID, DragonState> dragonStates;
			std::unordered_map<RE::FormID, DragonShoutRoute> dragonShoutRoutes;
			std::vector<QueuedEvent> queuedEvents;
			std::vector<TrackingEvent> trackingEvents;
			std::unordered_set<RE::FormID> subscribedDragonIDs;
			std::mutex eventMutex;
			std::mutex wakeMutex;
			std::vector<WakeSnapshot> wakeSnapshots;
			std::size_t fallbackActorCursor{};
			float fallbackScanRemaining{};
			float trackingRefreshRemaining{};
			float lastTrackingDistance = std::numeric_limits<float>::quiet_NaN();
			bool registered{};
		};
	}

	void Register()
	{
		Manager::GetSingleton().Register();
	}

	void Update(float a_frameTime, const DragonWind::Settings& a_settings)
	{
		Manager::GetSingleton().Update(a_frameTime, a_settings);
	}

	void Reset()
	{
		Manager::GetSingleton().Reset();
	}
}

std::string DragonWind::GetDisplayName() const
{
	return "Dragons";
}

void DragonWind::LoadSettings(const nlohmann::json& a_json)
{
	if (const auto legacy = a_json.find("dragonWind"); legacy != a_json.end() && legacy->is_object())
		settings = *legacy;
	else
		settings = a_json;
	SanitizeSettings();
}

void DragonWind::SaveSettings(nlohmann::json& a_json) const
{
	a_json = settings;
}

void DragonWind::RestoreDefaultSettings()
{
	settings = {};
	Reset();
}

void DragonWind::DataLoaded()
{
	DragonWindRuntime::Register();
}

void DragonWind::Update(float a_frameTime)
{
	DragonWindRuntime::Update(a_frameTime, settings);
}

void DragonWind::Reset()
{
	DragonWindRuntime::Reset();
}

void DragonWind::SanitizeSettings()
{
	const Settings defaults{};
	auto sanitizeImpact = [&](ImpactProfile& a_profile, const ImpactProfile& a_default) {
		a_profile.strength = WindMath::ClampFiniteOrDefault(a_profile.strength,
			DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, a_default.strength);
		a_profile.distance = WindMath::ClampFiniteOrDefault(a_profile.distance,
			DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, a_default.distance);
		a_profile.waveHalfWidth = WindMath::ClampFiniteOrDefault(a_profile.waveHalfWidth,
			DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, a_default.waveHalfWidth);
		a_profile.propagationSpeed = WindMath::ClampFiniteOrDefault(a_profile.propagationSpeed,
			DragonWindRuntime::kSpeedMin, DragonWindRuntime::kSpeedMax, a_default.propagationSpeed);
	};

	settings.soulAbsorptionStrength = WindMath::ClampFiniteOrDefault(settings.soulAbsorptionStrength,
		DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, defaults.soulAbsorptionStrength);
	settings.soulAbsorptionRadius = WindMath::ClampFiniteOrDefault(settings.soulAbsorptionRadius,
		DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, defaults.soulAbsorptionRadius);
	settings.soulAbsorptionFadeTime = WindMath::ClampFiniteOrDefault(settings.soulAbsorptionFadeTime,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.soulAbsorptionFadeTime);
	settings.breathStrength = WindMath::ClampFiniteOrDefault(settings.breathStrength,
		DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, defaults.breathStrength);
	settings.breathDistance = WindMath::ClampFiniteOrDefault(settings.breathDistance,
		DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, defaults.breathDistance);
	settings.breathConeHalfAngle = WindMath::ClampFiniteOrDefault(settings.breathConeHalfAngle,
		DragonWindRuntime::kBreathAngleMin, DragonWindRuntime::kBreathAngleMax, defaults.breathConeHalfAngle);
	settings.breathReleaseTime = WindMath::ClampFiniteOrDefault(settings.breathReleaseTime,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.breathReleaseTime);
	settings.wakeStrength = WindMath::ClampFiniteOrDefault(settings.wakeStrength,
		DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, defaults.wakeStrength);
	settings.wakeLength = WindMath::ClampFiniteOrDefault(settings.wakeLength,
		DragonWindRuntime::kWakeDimensionMin, DragonWindRuntime::kWakeDimensionMax, defaults.wakeLength);
	settings.wakeHalfWidth = WindMath::ClampFiniteOrDefault(settings.wakeHalfWidth,
		DragonWindRuntime::kWakeDimensionMin, DragonWindRuntime::kWakeDimensionMax, defaults.wakeHalfWidth);
	settings.wakeDownwardReach = WindMath::ClampFiniteOrDefault(settings.wakeDownwardReach,
		DragonWindRuntime::kWakeDimensionMin, DragonWindRuntime::kWakeDimensionMax, defaults.wakeDownwardReach);
	settings.wakeFullSizeSpeed = WindMath::ClampFiniteOrDefault(settings.wakeFullSizeSpeed,
		DragonWindRuntime::kWakeDimensionMin, DragonWindRuntime::kWakeDimensionMax, defaults.wakeFullSizeSpeed);
	settings.wakeMaximumScale = WindMath::ClampFiniteOrDefault(settings.wakeMaximumScale,
		DragonWindRuntime::kWakeScaleMin, DragonWindRuntime::kWakeScaleMax, defaults.wakeMaximumScale);
	settings.wakeEdgeSoftness = WindMath::ClampFiniteOrDefault(settings.wakeEdgeSoftness,
		DragonWindRuntime::kWakeEdgeMin, DragonWindRuntime::kWakeEdgeMax, defaults.wakeEdgeSoftness);
	settings.wakeOutwardFlow = WindMath::ClampFiniteOrDefault(settings.wakeOutwardFlow,
		DragonWindRuntime::kWakeFlowMin, DragonWindRuntime::kWakeFlowMax, defaults.wakeOutwardFlow);
	settings.wakeDownwash = WindMath::ClampFiniteOrDefault(settings.wakeDownwash,
		DragonWindRuntime::kWakeFlowMin, DragonWindRuntime::kWakeFlowMax, defaults.wakeDownwash);
	settings.strength = WindMath::ClampFiniteOrDefault(settings.strength,
		DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, defaults.strength);
	settings.trackingDistance = WindMath::ClampFiniteOrDefault(settings.trackingDistance,
		DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, defaults.trackingDistance);
	settings.wingbeatStrength = WindMath::ClampFiniteOrDefault(settings.wingbeatStrength,
		DragonWindRuntime::kStrengthMin, DragonWindRuntime::kStrengthMax, defaults.wingbeatStrength);
	settings.wingbeatDistance = WindMath::ClampFiniteOrDefault(settings.wingbeatDistance,
		DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, defaults.wingbeatDistance);
	settings.wingbeatStartRadius = WindMath::ClampFiniteOrDefault(settings.wingbeatStartRadius,
		DragonWindRuntime::kWingbeatRadiusMin, DragonWindRuntime::kDistanceMax, defaults.wingbeatStartRadius);
	settings.wingbeatEndRadius = std::max(settings.wingbeatStartRadius,
		WindMath::ClampFiniteOrDefault(settings.wingbeatEndRadius,
			DragonWindRuntime::kWingbeatRadiusMin, DragonWindRuntime::kDistanceMax, defaults.wingbeatEndRadius));
	settings.wingbeatWaveHalfWidth = WindMath::ClampFiniteOrDefault(settings.wingbeatWaveHalfWidth,
		DragonWindRuntime::kDistanceMin, DragonWindRuntime::kDistanceMax, defaults.wingbeatWaveHalfWidth);
	settings.wingbeatPropagationSpeed = WindMath::ClampFiniteOrDefault(settings.wingbeatPropagationSpeed,
		DragonWindRuntime::kSpeedMin, DragonWindRuntime::kSpeedMax, defaults.wingbeatPropagationSpeed);
	settings.wingbeatDecayTime = WindMath::ClampFiniteOrDefault(settings.wingbeatDecayTime,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.wingbeatDecayTime);
	settings.wingbeatFallbackCooldown = WindMath::ClampFiniteOrDefault(settings.wingbeatFallbackCooldown,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.wingbeatFallbackCooldown);
	sanitizeImpact(settings.normalImpact, defaults.normalImpact);
	sanitizeImpact(settings.forcefulImpact, defaults.forcefulImpact);
	sanitizeImpact(settings.crashImpact, defaults.crashImpact);
	settings.impactDecayTime = WindMath::ClampFiniteOrDefault(settings.impactDecayTime,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.impactDecayTime);
	settings.impactDeduplicationTime = WindMath::ClampFiniteOrDefault(settings.impactDeduplicationTime,
		DragonWindRuntime::kTimeMin, DragonWindRuntime::kTimeMax, defaults.impactDeduplicationTime);
}

nlohmann::json DragonWind::GetWakeDiagnostics(const nlohmann::json& a_args)
{
	return DragonWindRuntime::Manager::GetSingleton().GetWakeDiagnostics(a_args);
}
