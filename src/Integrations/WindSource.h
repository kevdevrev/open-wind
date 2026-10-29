#pragma once

#include "OpenWind/API.h"

namespace OpenWind::Integrations
{
	/** Returns the discovered sampling API, or an unavailable source when Open Wind is absent. */
	const OpenWind::API& WindSource() noexcept;
}
