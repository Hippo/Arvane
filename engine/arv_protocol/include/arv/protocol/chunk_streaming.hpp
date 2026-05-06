#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace arv::protocol {

struct ChunkInterestUpdate {
  WorldID world_id{};
  ChunkSectionCoord center_chunk{};
  i32 client_view_distance{};
  std::optional<ChunkSectionCoord> previous_center_chunk{};
  u32 sequence_id{};
};

struct ChunkSectionData {
  WorldID world_id{};
  ChunkSectionCoord coord{};
  u64 chunk_version{};
  u64 chunk_content_version{};
  u64 chunk_light_version{};
  bool has_lighting{};
  bool lighting_valid{};
  bool has_fluids{};
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks{};
  std::array<voxel::LightWord, voxel::ChunkVolume> lights{};
  std::array<voxel::FluidCell, voxel::ChunkVolume> fluids{};
};

struct ChunkSectionUnload {
  WorldID world_id{};
  ChunkSectionCoord coord{};
};

std::vector<u8> serialize_chunk_interest_update(const ChunkInterestUpdate& update);
Expected<ChunkInterestUpdate, ProtocolError> deserialize_chunk_interest_update(
    std::span<const u8> bytes);

std::vector<u8> serialize_chunk_section_data(const ChunkSectionData& data);
Expected<ChunkSectionData, ProtocolError> deserialize_chunk_section_data(std::span<const u8> bytes);

std::vector<u8> serialize_chunk_section_unload(const ChunkSectionUnload& unload);
Expected<ChunkSectionUnload, ProtocolError> deserialize_chunk_section_unload(
    std::span<const u8> bytes);

using ChunkData = ChunkSectionData;
using ChunkUnload = ChunkSectionUnload;

inline std::vector<u8> serialize_chunk_data(const ChunkData& data) {
  return serialize_chunk_section_data(data);
}

inline Expected<ChunkData, ProtocolError> deserialize_chunk_data(std::span<const u8> bytes) {
  return deserialize_chunk_section_data(bytes);
}

inline std::vector<u8> serialize_chunk_unload(const ChunkUnload& unload) {
  return serialize_chunk_section_unload(unload);
}

inline Expected<ChunkUnload, ProtocolError> deserialize_chunk_unload(std::span<const u8> bytes) {
  return deserialize_chunk_section_unload(bytes);
}

} // namespace arv::protocol
