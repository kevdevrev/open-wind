#pragma once

#include <nlohmann/json.hpp>

namespace OpenWind::Integrations
{
	/** Forwards integration queries and edits to installed bridge DLLs. */
	nlohmann::json Command(const nlohmann::json& request);
}
