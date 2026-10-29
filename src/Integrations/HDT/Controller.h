#pragma once

#include "FSMP/PluginAPI.h"
#include "OpenWind/API.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace OpenWind::Integrations::HDT
{
	class Controller
	{
	public:
		Controller();
		static Controller& Get();
		void SetPhysicsInterface(hdt::PluginInterface* physicsInterface);
		void RefreshRegistration();
		bool IsRegistered();
		std::string Status();

	private:
		static bool IsEnabled(void*) noexcept;
		static bool SampleShelter(void*, const hdt::WindVector&, hdt::WindVector&) noexcept;
		static bool Evaluate(void*, const hdt::WindFrame&, hdt::WindForce*) noexcept;
		bool EvaluateFrame(const hdt::WindFrame&, hdt::WindForce*);

		std::mutex m_linkLock;
		hdt::PluginInterface* m_physics{};
		hdt::WindController m_descriptor;
		bool m_registered{};
		bool m_incompatible{};
		// Only the serialized FSMP force callback touches these reusable buffers and clocks.
		std::vector<OpenWind::Vector3> m_positions;
		std::vector<OpenWind::WindSample> m_samples;
		struct Clock
		{
			double time{};
			std::uint64_t frame{};
		};
		std::uint64_t m_frame{};
		std::unordered_map<std::uint64_t, Clock> m_clocks;
	};
}
