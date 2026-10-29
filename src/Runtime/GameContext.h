#pragma once

#include <RE/Skyrim.h>

namespace OpenWind::Game
{
	inline RE::PlayerCharacter* player{};
	inline RE::Sky* sky{};
	inline RE::UI* ui{};
	inline RE::TES* tes{};
	inline bool isVR{};
	/** Caches game context once Skyrim data is ready. */
	void Initialize();
}
