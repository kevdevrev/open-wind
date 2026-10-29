#include "WindField.h"
#include "Integrations/WindSource.h"

#include <algorithm>
#include <cmath>

namespace OpenWind::Integrations::DynamicWind
{
	bool SourceAvailable() { return WindSource().IsReady(); }

	bool SampleField(std::span<const Vector> positions, std::vector<FieldSample>& samples)
	{
		const auto& source = WindSource();
		if (!source.maximumBatchSize || !source.IsReady())
			return false;
		thread_local std::vector<OpenWind::Vector3> input;
		thread_local std::vector<OpenWind::WindSample> output;
		input.resize(positions.size());
		output.resize(positions.size());
		for (size_t i = 0; i < positions.size(); ++i)
			input[i] = { positions[i].x, positions[i].y, positions[i].z };
		for (size_t offset = 0; offset < input.size();) {
			const auto count = static_cast<uint32_t>(std::min(input.size() - offset, size_t(source.maximumBatchSize)));
			if (!source.SampleWind(input.data() + offset, output.data() + offset, count))
				return false;
			offset += count;
		}
		const auto finite = [](float x, float y, float z) { return std::isfinite(std::hypot(x, y, z)); };
		for (const auto& value : output) {
			if (value.frameId != output.front().frameId ||
				!finite(value.baseVelocity.x + value.gustVelocity.x + value.fineVelocity.x,
					value.baseVelocity.y + value.gustVelocity.y + value.fineVelocity.y,
					value.baseVelocity.z + value.gustVelocity.z + value.fineVelocity.z) ||
				!finite(value.finalVelocity.x, value.finalVelocity.y, value.finalVelocity.z) ||
				!finite(value.windExcludingHavokImpulses.x, value.windExcludingHavokImpulses.y, value.windExcludingHavokImpulses.z))
				return false;
		}
		samples.resize(positions.size());
		for (size_t i = 0; i < samples.size(); ++i) {
			const auto& value = output[i];
			samples[i] = {
				{ value.baseVelocity.x + value.gustVelocity.x + value.fineVelocity.x,
					value.baseVelocity.y + value.gustVelocity.y + value.fineVelocity.y,
					value.baseVelocity.z + value.gustVelocity.z + value.fineVelocity.z },
				{ value.finalVelocity.x, value.finalVelocity.y, value.finalVelocity.z },
				{ value.windExcludingHavokImpulses.x, value.windExcludingHavokImpulses.y, value.windExcludingHavokImpulses.z },
				value.frameId,
				value.transientIntensity
			};
		}
		return true;
	}
}
