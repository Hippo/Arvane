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
  ChunkCoord center_chunk{};
  i32 client_view_distance{};
  std::optional<ChunkCoord> previous_center_chunk{};
  u32 sequence_id{};
};

struct ChunkData {
  WorldID world_id{};
  ChunkCoord coord{};
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

struct ChunkUnload {
  WorldID world_id{};
  ChunkCoord coord{};
};

std::vector<u8> serialize_chunk_interest_update(const ChunkInterestUpdate& update);
Expected<ChunkInterestUpdate, ProtocolError> deserialize_chunk_interest_update(
    std::span<const u8> bytes);

std::vector<u8> serialize_chunk_data(const ChunkData& data);
Expected<ChunkData, ProtocolError> deserialize_chunk_data(std::span<const u8> bytes);

std::vector<u8> serialize_chunk_unload(const ChunkUnload& unload);
Expected<ChunkUnload, ProtocolError> deserialize_chunk_unload(std::span<const u8> bytes);

} // namespace arv::protocol
