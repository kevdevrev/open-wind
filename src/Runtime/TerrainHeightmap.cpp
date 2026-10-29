#include "Runtime/TerrainHeightmap.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace OpenWind::TerrainShelter
{
	namespace
	{
		constexpr uint32_t kR32Float = 41;
		constexpr uint32_t kR16Float = 54;
		constexpr uint32_t kR16Unorm = 56;
		constexpr uint32_t kBC4Unorm = 80;
		constexpr uint32_t kMaximumDimension = 16384;
		constexpr uint64_t kMaximumBytes = 256ull * 1024 * 1024;
		constexpr float kWorldCellSize = 4096.0f;
		constexpr float kHeightUnit = 8.0f;

		uint32_t ReadUInt(const std::byte* a_data) noexcept
		{
			uint32_t value;
			std::memcpy(&value, a_data, sizeof(value));
			return value;
		}

		bool SameName(std::string_view a_left, std::string_view a_right)
		{
			return a_left.size() == a_right.size() && _strnicmp(a_left.data(), a_right.data(), a_left.size()) == 0;
		}

		bool ParseBounds(const std::filesystem::path& a_path, std::string_view a_worldspace,
			TerrainHeightmap& a_map)
		{
			if (!SameName(a_path.extension().string(), ".dds"))
				return false;
			const std::string stem = a_path.stem().string();
			std::vector<std::string_view> parts;
			std::size_t first = 0;
			while (first <= stem.size()) {
				const auto end = stem.find('.', first);
				parts.emplace_back(stem.data() + first, (end == std::string::npos ? stem.size() : end) - first);
				if (end == std::string::npos)
					break;
				first = end + 1;
			}
			if (parts.empty() || !SameName(parts[0], a_worldspace))
				return false;
			const bool xlodgen = parts.size() == 9 && SameName(parts[1], "Terrain") && SameName(parts[2], "HeightMap");
			if (!xlodgen && !(parts.size() == 10 && SameName(parts[1], "HeightMap")))
				return false;
			const std::size_t offset = xlodgen ? 3 : 2;
			std::array<int32_t, 8> values{};
			for (std::size_t index = offset; index < parts.size(); ++index) {
				const auto text = parts[index];
				const auto result = std::from_chars(text.data(), text.data() + text.size(), values[index - offset]);
				if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
					return false;
			}
			a_map.minimum = { values[0] * kWorldCellSize, (static_cast<float>(values[3]) + 1.0f) * kWorldCellSize,
				(xlodgen ? -32767.0f : static_cast<float>(values[4])) * kHeightUnit };
			a_map.maximum = { (static_cast<float>(values[2]) + 1.0f) * kWorldCellSize, values[1] * kWorldCellSize,
				(xlodgen ? 32767.0f : static_cast<float>(values[5])) * kHeightUnit };
			return a_map.maximum.x > a_map.minimum.x && a_map.minimum.y > a_map.maximum.y &&
			       a_map.maximum.z > a_map.minimum.z;
		}

		bool LoadPixels(const std::filesystem::path& a_path, TerrainHeightmap& a_map)
		{
			std::ifstream stream(a_path, std::ios::binary | std::ios::ate);
			if (!stream)
				return false;
			const auto size = stream.tellg();
			if (size < 128 || size > static_cast<std::streamoff>(kMaximumBytes + 148))
				return false;
			stream.seekg(0);
			std::array<std::byte, 148> header{};
			if (!stream.read(reinterpret_cast<char*>(header.data()), 128) ||
				ReadUInt(header.data()) != 0x20534444 || ReadUInt(header.data() + 4) != 124 ||
				ReadUInt(header.data() + 76) != 32 || ReadUInt(header.data() + 112) != 0)
				return false;
			a_map.height = ReadUInt(header.data() + 12);
			a_map.width = ReadUInt(header.data() + 16);
			if (!a_map.width || !a_map.height || a_map.width > kMaximumDimension || a_map.height > kMaximumDimension)
				return false;
			uint32_t dataOffset = 128;
			const auto pixelFlags = ReadUInt(header.data() + 80);
			const auto fourCC = ReadUInt(header.data() + 84);
			if ((pixelFlags & 4) && fourCC == 0x30315844) {
				if (!stream.read(reinterpret_cast<char*>(header.data() + 128), 20))
					return false;
				a_map.format = ReadUInt(header.data() + 128);
				if (ReadUInt(header.data() + 132) != 3 || ReadUInt(header.data() + 136) != 0 ||
					ReadUInt(header.data() + 140) != 1)
					return false;
				dataOffset = 148;
			} else if ((pixelFlags & 4) && (fourCC == 111 || fourCC == 114)) {
				a_map.format = fourCC == 111 ? kR16Float : kR32Float;
			} else if ((pixelFlags & 4) && (fourCC == 0x55344342 || fourCC == 0x31495441)) {
				a_map.format = kBC4Unorm;
			} else if ((pixelFlags & 0x20000) && ReadUInt(header.data() + 88) == 16 &&
				ReadUInt(header.data() + 92) == 0xffff) {
				a_map.format = kR16Unorm;
			} else {
				return false;
			}
			if (a_map.format != kR32Float && a_map.format != kR16Float && a_map.format != kR16Unorm && a_map.format != kBC4Unorm)
				return false;
			const bool compressed = a_map.format == kBC4Unorm;
			const auto packedPitch = compressed ? ((a_map.width + 3) / 4) * 8 : a_map.width * (a_map.format == kR32Float ? 4u : 2u);
			a_map.pitch = !compressed && (ReadUInt(header.data() + 8) & 8) ? ReadUInt(header.data() + 20) : packedPitch;
			const uint64_t byteCount = static_cast<uint64_t>(a_map.pitch) * (compressed ? (a_map.height + 3) / 4 : a_map.height);
			if (a_map.pitch < packedPitch || byteCount > kMaximumBytes || byteCount + dataOffset > static_cast<uint64_t>(size))
				return false;
			a_map.pixels.resize(static_cast<std::size_t>(byteCount));
			return static_cast<bool>(stream.read(reinterpret_cast<char*>(a_map.pixels.data()), static_cast<std::streamsize>(byteCount)));
		}

		std::shared_ptr<const TerrainHeightmap> LoadHeightmap(std::string_view a_worldspace)
		{
			std::vector<std::filesystem::path> candidates;
			std::error_code error;
			const std::filesystem::path custom("Data/textures/heightmaps");
			if (std::filesystem::is_directory(custom, error))
				for (const auto& entry : std::filesystem::directory_iterator(custom))
					if (entry.is_regular_file())
						candidates.push_back(entry.path());
			const std::filesystem::path terrain("Data/textures/Terrain");
			if (std::filesystem::is_directory(terrain, error))
				for (const auto& directory : std::filesystem::directory_iterator(terrain))
					if (directory.is_directory())
						for (const auto& entry : std::filesystem::directory_iterator(directory.path()))
							if (entry.is_regular_file())
								candidates.push_back(entry.path());
			for (const auto& path : candidates) {
				auto map = std::make_shared<TerrainHeightmap>();
				if (ParseBounds(path, a_worldspace, *map) && LoadPixels(path, *map))
					return map;
			}
			return {};
		}

		float Decode(const std::byte* a_pixel, uint32_t a_format) noexcept
		{
			if (a_format == kR32Float) {
				float value;
				std::memcpy(&value, a_pixel, sizeof(value));
				return value;
			}
			uint16_t value;
			std::memcpy(&value, a_pixel, sizeof(value));
			if (a_format == kR16Unorm)
				return value / 65535.0f;
			const uint32_t exponent = (value >> 10) & 31;
			const uint32_t fraction = value & 1023;
			if (exponent == 31)
				return std::numeric_limits<float>::quiet_NaN();
			const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(fraction), -24) :
			                                         std::ldexp(static_cast<float>(fraction + 1024), static_cast<int>(exponent) - 25);
			return (value & 0x8000) ? -magnitude : magnitude;
		}

		float DecodeBC4(const std::byte* a_block, uint32_t a_x, uint32_t a_y) noexcept
		{
			const auto first = std::to_integer<uint32_t>(a_block[0]);
			const auto second = std::to_integer<uint32_t>(a_block[1]);
			uint64_t indices = 0;
			for (uint32_t index = 0; index < 6; ++index)
				indices |= static_cast<uint64_t>(std::to_integer<uint8_t>(a_block[index + 2])) << (index * 8);
			const auto selected = static_cast<uint32_t>((indices >> (3 * ((a_y % 4) * 4 + a_x % 4))) & 7);
			if (selected == 0)
				return first / 255.0f;
			if (selected == 1)
				return second / 255.0f;
			if (first > second)
				return ((8 - selected) * first + (selected - 1) * second) / (7.0f * 255.0f);
			if (selected < 6)
				return ((6 - selected) * first + (selected - 1) * second) / (5.0f * 255.0f);
			return selected == 6 ? 0.0f : 1.0f;
		}
	}

	bool TerrainHeightmap::Sample(float a_x, float a_y, float& a_height) const noexcept
	{
		const float u = (a_x - minimum.x) / (maximum.x - minimum.x);
		const float v = (a_y - minimum.y) / (maximum.y - minimum.y);
		if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f))
			return false;
		const float x = std::clamp(u * width, 0.0f, static_cast<float>(width - 1));
		const float y = std::clamp(v * height - 1.0f, 0.0f, static_cast<float>(height - 1));
		const auto lowerX = static_cast<uint32_t>(x);
		const auto lowerY = static_cast<uint32_t>(y);
		float normalized = 0.0f;
		for (uint32_t row = 0; row < 2; ++row)
			for (uint32_t column = 0; column < 2; ++column) {
				const uint32_t pixelX = std::min(lowerX + column, width - 1);
				const uint32_t pixelY = std::min(lowerY + row, height - 1);
				const auto index = format == kBC4Unorm ? static_cast<std::size_t>(pixelY / 4) * pitch + (pixelX / 4) * 8 :
				                                       static_cast<std::size_t>(pixelY) * pitch + pixelX * (format == kR32Float ? 4 : 2);
				const float value = format == kBC4Unorm ? DecodeBC4(pixels.data() + index, pixelX, pixelY) : Decode(pixels.data() + index, format);
				if (!std::isfinite(value))
					return false;
				normalized += value * (column ? x - lowerX : 1.0f - x + lowerX) * (row ? y - lowerY : 1.0f - y + lowerY);
			}
		a_height = std::lerp(minimum.z, maximum.z, normalized);
		return std::isfinite(a_height);
	}

	std::shared_ptr<const TerrainHeightmap> RequestHeightmap(std::string_view a_worldspace)
	{
		struct Cache
		{
			std::mutex mutex;
			std::string worldspace;
			std::shared_ptr<const TerrainHeightmap> published;
			bool loading{};
		};
		static auto* cache = new Cache;
		std::lock_guard lock(cache->mutex);
		if (SameName(cache->worldspace, a_worldspace))
			return cache->published;
		if (cache->loading)
			return {};
		cache->worldspace = a_worldspace;
		cache->published.reset();
		cache->loading = true;
		try {
			std::thread([name = std::string(a_worldspace)] {
				std::shared_ptr<const TerrainHeightmap> map;
				try { map = LoadHeightmap(name); } catch (...) {}
				std::lock_guard completed(cache->mutex);
				cache->published = std::move(map);
				cache->loading = false;
			}).detach();
		} catch (...) {
			cache->loading = false;
			cache->worldspace.clear();
		}
		return {};
	}
}
