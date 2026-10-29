#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "OpenWind/Types.h"
#include "OpenWind/WindField.h"
#include "OpenWind/WindSettings.h"
#include "OpenWind/WindRuntimeState.h"

using OpenWind::float2;
using OpenWind::float3;
using OpenWind::float4;
using OpenWind::RuntimeWindTest;
using OpenWind::WindFieldDebugView;
using OpenWind::WindRuntimeState;
using OpenWind::WindSettings;
namespace WindField = OpenWind::WindField;
namespace WindSettingsLimits = OpenWind::WindSettingsLimits;

using namespace std::literals;
using json = nlohmann::json;
namespace logger = SKSE::log;

namespace stl
{
	using namespace SKSE::stl;

	template <class F, std::size_t Index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtable{ F::VTABLE[Index] };
		T::func = vtable.write_vfunc(T::size, T::thunk);
	}
}
