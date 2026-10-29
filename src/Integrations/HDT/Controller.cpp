#include "Controller.h"
#include "Integrations/WindSource.h"
#include "Settings.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace OpenWind::Integrations::HDT
{
	namespace
	{
		RE::NiPoint3 Point(const hdt::WindVector& value) { return { value.x, value.y, value.z }; }
		hdt::WindVector Vector(const RE::NiPoint3& value) { return { value.x, value.y, value.z }; }
		bool Finite(const RE::NiPoint3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	Controller& Controller::Get()
	{
		static auto* controller = new Controller;
		return *controller;
	}

	Controller::Controller()
	{
		m_descriptor.name = "Open Wind";
		m_descriptor.context = this;
		m_descriptor.isEnabled = IsEnabled;
		m_descriptor.sampleShelter = SampleShelter;
		m_descriptor.evaluate = Evaluate;
	}

	void Controller::SetPhysicsInterface(hdt::PluginInterface* physicsInterface)
	{
		{
			std::lock_guard lock(m_linkLock);
			if (!physicsInterface)
				return;
			const auto version = physicsInterface->getVersionInfo().interfaceVersion;
			m_incompatible = version.major != 2 || version.minor < 1;
			if (!m_incompatible)
				m_physics = physicsInterface;
		}
		RefreshRegistration();
	}

	void Controller::RefreshRegistration()
	{
		const auto settings = GetSettings();
		std::lock_guard lock(m_linkLock);
		if (!m_physics)
			return;
		if (settings.enabled) {
			if (!m_registered) {
				m_registered = m_physics->registerWindController(&m_descriptor);
				SKSE::log::info("FSMP wind controller: {}", m_registered ? "registered" : "already owned by another plugin");
			}
		} else if (m_registered) {
			m_physics->unregisterWindController(&m_descriptor);
			m_registered = false;
			// Unregister waits for the physics callback, so its private clocks can now be released.
			m_clocks.clear();
			SKSE::log::info("Released FSMP wind control");
		}
	}

	bool Controller::IsRegistered()
	{
		std::lock_guard lock(m_linkLock);
		return m_registered;
	}

	std::string Controller::Status()
	{
		const auto settings = GetSettings();
		std::lock_guard lock(m_linkLock);
		if (m_incompatible)
			return "This FSMP build needs the wind-controller API (interface 2.1).";
		if (!m_physics)
			return GetModuleHandleW(L"hdtsmp64.dll") ? "Waiting for FSMP's wind-controller API." : "FSMP is not installed.";
		if (!settings.enabled)
			return "Disabled. FSMP uses its native wind.";
		if (!m_registered)
			return "Another plugin owns FSMP wind. Disable it, then retry.";
		if (!WindSource().IsReady())
			return "Waiting for an available Open Wind field snapshot. FSMP uses its native wind while waiting.";
		return "Registered with FSMP; Open Wind is ready. FSMP's master wind switch still controls whether forces are applied.";
	}

	bool Controller::IsEnabled(void*) noexcept
	{
		try {
			return GetSettings().enabled && WindSource().IsReady();
		} catch (...) {
			return false;
		}
	}

	bool Controller::SampleShelter(void*, const hdt::WindVector& position, hdt::WindVector& direction) noexcept
	{
		const auto& wind = WindSource();
		const OpenWind::Vector3 location{ position.x, position.y, position.z };
		OpenWind::WindSample sample{};
		if (!wind.IsReady() || !wind.SampleWind(&location, &sample, 1))
			return false;
		// Local impacts may come from another side; FSMP's shelter ray follows ambient wind only.
		direction = { sample.baseVelocity.x + sample.gustVelocity.x + sample.fineVelocity.x,
			sample.baseVelocity.y + sample.gustVelocity.y + sample.fineVelocity.y,
			sample.baseVelocity.z + sample.gustVelocity.z + sample.fineVelocity.z };
		return true;
	}

	bool Controller::Evaluate(void* context, const hdt::WindFrame& frame, hdt::WindForce* outputs) noexcept
	{
		try {
			return static_cast<Controller*>(context)->EvaluateFrame(frame, outputs);
		} catch (...) {
			// Never unwind through FSMP; a failed update contributes no wind.
			return false;
		}
	}

	bool Controller::EvaluateFrame(const hdt::WindFrame& frame, hdt::WindForce* outputs)
	{
		constexpr float kEpsilon = std::numeric_limits<float>::epsilon();
		constexpr double kTwoPi = 2.0 * std::numbers::pi;
		constexpr double kFlutterHz = 2.0;
		constexpr double kSecondaryHz = 3.1;
		constexpr float kMaxFrequencyIncrease = 0.75f;
		constexpr float kMaxRelativeResponse = 1.4f;
		constexpr float kSidewaysAmplitude = 0.06f;
		constexpr float kVerticalAmplitude = 0.02f;
		constexpr float kSteadyOffset = 0.35f;
		constexpr float kOscillatingOffset = 0.25f;
		const RE::NiPoint3 phaseScale{ 0.012f, 0.017f, 0.02f };
		const RE::NiPoint3 up{ 0.f, 0.f, 1.f };
		const auto settings = GetSettings();
		const auto& wind = WindSource();
		if (!settings.enabled || !wind.IsReady())
			return false;
		const auto forceScale = frame.windStrength * frame.unitsPerMeter;
		const auto step = std::clamp(frame.timeStep, 0.f, 0.1f) * settings.flutterRate;
		m_positions.resize(frame.systemCount);
		m_samples.assign(frame.systemCount, OpenWind::WindSample{});
		for (std::uint32_t i = 0; i < frame.systemCount; ++i) {
			const auto& p = frame.systems[i].position;
			m_positions[i] = { p.x, p.y, p.z };
		}
		for (std::uint32_t first = 0; first < frame.systemCount;) {
			const auto count = std::min(frame.systemCount - first, wind.maximumBatchSize);
			if (!wind.SampleWind(m_positions.data() + first, m_samples.data() + first, count))
				return false;
			first += count;
		}

		++m_frame;
		for (std::uint32_t i = 0; i < frame.systemCount; ++i) {
			const auto& system = frame.systems[i];
			const auto& sample = m_samples[i];
			const RE::NiPoint3 ambient{ sample.baseVelocity.x + sample.gustVelocity.x + sample.fineVelocity.x,
				sample.baseVelocity.y + sample.gustVelocity.y + sample.fineVelocity.y,
				sample.baseVelocity.z + sample.gustVelocity.z + sample.fineVelocity.z };
			const RE::NiPoint3 transient{ sample.transientVelocity.x, sample.transientVelocity.y, sample.transientVelocity.z };
			const auto windExposure = std::clamp(sample.windExposure, 0.f, 1.f);
			const auto shelter = std::min(windExposure, std::clamp(system.shelterFactor, 0.f, 1.f));
			const auto additionalShelter = windExposure > kEpsilon ? shelter / windExposure : 1.f;
			const auto ambientForce = ambient * (additionalShelter * forceScale);
			const auto transientForce = transient * forceScale;
			if (!Finite(ambientForce) || !Finite(transientForce))
				continue;
			const float magnitude = ambientForce.Length();
			const bool hasAmbient = magnitude > kEpsilon;
			const auto direction = hasAmbient ? ambientForce / magnitude : RE::NiPoint3{};
			auto side = direction.Cross(up);
			if (side.SqrLength() < kEpsilon)
				side = { 1.f, 0.f, 0.f };
			else
				side.Unitize();
			const auto lift = side.Cross(direction);
			const auto strength = (ambient * additionalShelter).Length();
			auto& clock = m_clocks[system.id];
			clock.frame = m_frame;
			// Integrate frequency so changing gust strength cannot jump the flutter phase.
			clock.time += static_cast<double>(step) * (1.f + kMaxFrequencyIncrease * strength / (1.f + strength));
			const auto phase = static_cast<float>(std::fmod(clock.time * kTwoPi * kFlutterHz, kTwoPi));
			const auto secondaryPhase = static_cast<float>(std::fmod(clock.time * kTwoPi * kSecondaryHz, kTwoPi));
			for (std::uint32_t j = system.firstBody; j < system.firstBody + system.bodyCount; ++j) {
				const auto& body = frame.bodies[j];
				auto windForce = ambientForce;
				RE::NiPoint3 pressureOffset{};
				if (hasAmbient) {
					const auto relativeScale = settings.velocityResponse == 0.f ? 1.f :
					                                                              std::clamp((ambientForce - Point(body.velocity) * settings.velocityResponse).Dot(direction) / magnitude, 0.f, kMaxRelativeResponse);
					windForce *= relativeScale;
					if (settings.flutterAmount > 0.f || settings.torqueOffset > 0.f) {
						// Local offsets keep adjacent bones coherent without world-position phase jumps.
						const auto spatialPhase = (Point(body.position) - Point(system.position)).Dot(phaseScale);
						const auto flutter = std::sin(phase + spatialPhase);
						const auto secondary = std::sin(secondaryPhase - spatialPhase);
						windForce += (side * (flutter * kSidewaysAmplitude) + lift * (secondary * kVerticalAmplitude)) * (magnitude * settings.flutterAmount);
						pressureOffset = (side * (flutter * settings.flutterAmount) + lift * (kSteadyOffset + secondary * kOscillatingOffset * settings.flutterAmount)) * settings.torqueOffset;
					}
				}
				// r cross F preserves the off-center ambient force. Transients stay centered and unsheltered.
				outputs[j] = { Vector(windForce * body.windFactor + transientForce * body.windFactor),
					Vector(pressureOffset.Cross((windForce * body.windFactor) * Point(body.linearFactor))) };
			}
		}
		std::erase_if(m_clocks, [this](const auto& entry) { return entry.second.frame != m_frame; });
		return true;
	}
}
