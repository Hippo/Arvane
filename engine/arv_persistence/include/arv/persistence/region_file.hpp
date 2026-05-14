#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/light.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace arv::persistence {

struct PersistenceError {
  std::string message{};
};

inline constexpr u16 CurrentRegionFormatVersion = 3;
inline constexpr u16 MinimumReadableRegionFormatVersion = 1;
inline constexpr u16 RegionHeaderSize = 48;
inline constexpr u32 RegionChunkSlotCount =
    static_cast<u32>(RegionSizeInChunks * RegionSizeInChunks);

struct RegionFileHeader {
  u16 format_version{CurrentRegionFormatVersion};
  u16 header_size{RegionHeaderSize};
  RegionCoord region{};
  u32 chunk_count{RegionChunkSlotCount};
  u32 location_table_offset{RegionHeaderSize};
  u32 metadata_table_offset{location_table_offset + RegionChunkSlotCount * 8U};
  u32 region_tables_offset{metadata_table_offset + RegionChunkSlotCount * 8U};
  u32 data_start_offset{};
  u32 file_size{};
  u32 flags{};
};

struct SavedChunkSection {
  ChunkSectionCoord coord{};
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks{};
  std::array<voxel::LightWord, voxel::ChunkVolume> lights{};
  std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};
  bool lighting_valid{true};
};

struct DecodedRegionFile {
  RegionFileHeader header{};
  std::vector<SavedChunkSection> chunks{};
};

[[nodiscard]] Expected<RegionFileHeader, PersistenceError> parse_region_header(std::span<const u8> bytes);
[[nodiscard]] Expected<std::vector<u8>, PersistenceError> encode_region_file(
    RegionCoord region,
    std::span<const SavedChunkSection> chunks,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y);
[[nodiscard]] Expected<DecodedRegionFile, PersistenceError> decode_region_file(
    std::span<const u8> bytes,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y);
Expected<void, PersistenceError> write_region_file(
    const std::filesystem::path& path,
    RegionCoord region,
    std::span<const SavedChunkSection> chunks,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y);
[[nodiscard]] Expected<DecodedRegionFile, PersistenceError> read_region_file(
    const std::filesystem::path& path,
    const voxel::BlockRegistry& registry,
    i32 min_chunk_y);

} // namespace arv::persistence
