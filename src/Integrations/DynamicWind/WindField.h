#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace OpenWind::Integrations::DynamicWind
{
	struct Vector
	{
		float x{}, y{}, z{};
	};
	struct FieldSample
	{
		Vector ambient;
		Vector full;
		Vector excludingHavokImpulses;
		uint64_t frameId{};
		float transientIntensity{};
	};
	/** Checks whether Open Wind has published a usable snapshot. */
	bool SourceAvailable();
	/** Samples one snapshot without changing caller outputs on failure. */
	bool SampleField(std::span<const Vector> positions, std::vector<FieldSample>& samples);
}
