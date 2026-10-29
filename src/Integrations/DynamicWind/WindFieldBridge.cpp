#include "DynamicWind/DynamicWindAPI.h"
#include "Integration.h"
#include "WindField.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>

namespace OpenWind::Integrations::DynamicWind
{
	namespace
	{
		std::atomic_bool enabled{ true };
		std::atomic<float> signSensitivity{ kDefaultSignSensitivity };
		std::atomic<PushResponse> pushResponse{ PushResponse::Linear };
		enum class Connection
		{
			Missing,
			Unsupported,
			Occupied,
			Registered
		};
		std::atomic<Connection> connection{ Connection::Missing };
		constexpr float MinimumDirectionSpeed = 0.0001f;
		constexpr std::array<std::string_view, 6> HandlerNames{
			"Animation", "Rotation", "Visibility", "Physics", "Model swaps", "Base-object swaps"
		};
		struct SampleCounts
		{
			std::atomic<uint64_t> requested{};
			std::atomic<uint64_t> successful{};
			std::atomic<uint64_t> transientHits{};
		};

		struct Bridge
		{
			std::vector<Vector> positions;
			std::vector<FieldSample> samples;
			std::atomic<uint64_t> calls{};
			std::array<SampleCounts, HandlerNames.size()> counts;
			void Sample(const DynamicWindAPI::Frame& frame, const DynamicWindAPI::Request* requests,
				DynamicWindAPI::Response* responses, uint32_t count)
			{
				calls.fetch_add(1, std::memory_order_relaxed);
				if (!count)
					return;
				std::array<uint32_t, HandlerNames.size()> requested{};
				std::array<uint32_t, HandlerNames.size()> successful{};
				std::array<uint32_t, HandlerNames.size()> transientHits{};
				positions.resize(count);
				for (uint32_t i = 0; i < count; ++i) {
					positions[i] = { requests[i].position.x, requests[i].position.y, requests[i].position.z };
					const auto handler = static_cast<size_t>(requests[i].handler);
					if (handler < counts.size())
						++requested[handler];
				}
				for (size_t i = 0; i < counts.size(); ++i) {
					if (requested[i])
						counts[i].requested.fetch_add(requested[i], std::memory_order_relaxed);
				}
				if (!SampleField(positions, samples))
					return;
				const float pushSensitivity = GetSignSensitivity();
				const auto response = GetPushResponse();
				for (uint32_t i = 0; i < count; ++i) {
					const auto& request = requests[i];
					const auto handler = static_cast<size_t>(request.handler);
					if (handler >= counts.size())
						continue;
					auto velocity = samples[i].ambient;
					if (request.handler == DynamicWindAPI::Handler::Animation)
						velocity = samples[i].full;
					else if (request.handler == DynamicWindAPI::Handler::Push)
						velocity = samples[i].excludingHavokImpulses;
					const float speed = std::hypot(velocity.x, velocity.y);
					const bool modelVariant = request.handler == DynamicWindAPI::Handler::ModelSwap || request.handler == DynamicWindAPI::Handler::BaseObjectSwap;
					const float strength = modelVariant ? std::clamp(speed, 0.0f, 1.0f) : speed;
					responses[i] = { { strength, speed > MinimumDirectionSpeed ? std::atan2(velocity.y, velocity.x) : frame.baseline.angle },
						DynamicWindAPI::Result::Ready, { velocity.x, velocity.y, velocity.z } };
					if (request.handler == DynamicWindAPI::Handler::Push) {
						responses[i].pushResponse = response;
						responses[i].pushSensitivity = pushSensitivity;
					}
					++successful[handler];
					if (samples[i].transientIntensity > 0.0f)
						++transientHits[handler];
				}
				for (size_t i = 0; i < counts.size(); ++i) {
					if (successful[i])
						counts[i].successful.fetch_add(successful[i], std::memory_order_relaxed);
					if (transientHits[i])
						counts[i].transientHits.fetch_add(transientHits[i], std::memory_order_relaxed);
				}
			}
		};

		Bridge& GetBridge()
		{
			static auto* bridge = new Bridge;
			return *bridge;
		}

		const DynamicWindAPI::Provider& GetProvider()
		{
			static const DynamicWindAPI::Provider provider{
				sizeof(DynamicWindAPI::Provider), DynamicWindAPI::Version, &GetBridge(),
				DynamicWindAPI::Transitions | DynamicWindAPI::Gusts | DynamicWindAPI::Trees | DynamicWindAPI::GlobalWind,
				[](void*) noexcept { return enabled.load() && SourceAvailable(); },
				nullptr,
				[](void* value, const DynamicWindAPI::Frame* frame, const DynamicWindAPI::Request* requests,
					DynamicWindAPI::Response* responses, uint32_t count) noexcept {
					try {
						static_cast<Bridge*>(value)->Sample(*frame, requests, responses, count);
					} catch (...) {
						for (uint32_t i = 0; i < count; ++i) responses[i] = { frame->baseline, DynamicWindAPI::Result::Fallback };
					}
				},
				nullptr, nullptr, nullptr
			};
			return provider;
		}
	}

	void Connect()
	{
		if (IsRegistered())
			return;
		const auto module = GetModuleHandleW(L"DynamicWind.dll");
		if (!module) {
			connection.store(Connection::Missing);
			return;
		}
		const auto request = reinterpret_cast<DynamicWindAPI::RequestAPIFunction>(GetProcAddress(module, "RequestDynamicWindAPI"));
		const auto* api = request ? request(DynamicWindAPI::Version) : nullptr;
		if (!api || api->size != sizeof(*api) || api->version != DynamicWindAPI::Version || !api->RegisterProvider) {
			if (connection.exchange(Connection::Unsupported) != Connection::Unsupported)
				logger::warn("Dynamic Wind is installed without compatible provider API {}; Open Wind integration is inactive", DynamicWindAPI::Version);
			return;
		}
		if (!api->RegisterProvider(&GetProvider())) {
			if (connection.exchange(Connection::Occupied) != Connection::Occupied)
				logger::warn("Dynamic Wind rejected Open Wind's provider; another provider may already own wind");
			return;
		}
		connection.store(Connection::Registered);
		logger::info("Open Wind registered its Dynamic Wind bridge with provider API {}", DynamicWindAPI::Version);
	}

	bool IsRegistered() { return connection.load() == Connection::Registered; }
	bool IsEnabled() { return enabled.load(); }
	void SetEnabled(bool value) { enabled.store(value); }
	PushResponse GetPushResponse() noexcept { return pushResponse.load(std::memory_order_relaxed); }
	void SetPushResponse(PushResponse response) noexcept { pushResponse.store(response, std::memory_order_relaxed); }
	float GetSignSensitivity() noexcept { return signSensitivity.load(std::memory_order_relaxed); }
	void SetSignSensitivity(float value) noexcept
	{
		if (std::isfinite(value))
			signSensitivity.store(std::clamp(value, kMinimumSignSensitivity, kMaximumSignSensitivity), std::memory_order_relaxed);
	}

	std::string Status()
	{
		switch (connection.load()) {
		case Connection::Missing:
			return "Dynamic Wind is not installed.";
		case Connection::Unsupported:
			return std::format("Dynamic Wind requires a build exposing provider API version {}.", DynamicWindAPI::Version);
		case Connection::Occupied:
			return "Dynamic Wind rejected registration. Another provider may already control wind.";
		case Connection::Registered:
			if (!IsEnabled())
				return "Disabled. Dynamic Wind uses its native wind.";
			if (!SourceAvailable())
				return "Waiting for an Open Wind snapshot. Dynamic Wind uses its native wind.";
			{
				const auto& bridge = GetBridge();
				std::string status = std::format("Registered with Dynamic Wind; Open Wind is ready.\nDWF sampling calls since startup: {}.",
					bridge.calls.load(std::memory_order_relaxed));
				for (size_t i = 0; i < bridge.counts.size(); ++i) {
					const auto& counts = bridge.counts[i];
					status += std::format("\n{}: {} requested, {} sampled, {} samples hit by transients.", HandlerNames[i],
						counts.requested.load(std::memory_order_relaxed), counts.successful.load(std::memory_order_relaxed),
						counts.transientHits.load(std::memory_order_relaxed));
				}
				return status;
			}
		}
		return "Dynamic Wind integration is unavailable.";
	}
}
