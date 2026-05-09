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

struct ServerboundChunkInterestUpdate {
  WorldID world_id{};
  ChunkSectionCoord center_chunk{};
  i32 client_view_distance{};
  std::optional<ChunkSectionCoord> previous_center_chunk{};
  u32 sequence_id{};
};

struct ClientboundChunkSectionData {
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

struct ClientboundChunkSectionUnload {
  WorldID world_id{};
  ChunkSectionCoord coord{};
};

std::vector<u8> serialize_serverbound_chunk_interest_update(const ServerboundChunkInterestUpdate& update);
Expected<ServerboundChunkInterestUpdate, ProtocolError> deserialize_serverbound_chunk_interest_update(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_chunk_section_data(const ClientboundChunkSectionData& data);
Expected<ClientboundChunkSectionData, ProtocolError> deserialize_clientbound_chunk_section_data(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_chunk_section_unload(const ClientboundChunkSectionUnload& unload);
Expected<ClientboundChunkSectionUnload, ProtocolError> deserialize_clientbound_chunk_section_unload(
    std::span<const u8> bytes);

using ClientboundChunkData = ClientboundChunkSectionData;
using ClientboundChunkUnload = ClientboundChunkSectionUnload;

inline std::vector<u8> serialize_clientbound_chunk_data(const ClientboundChunkData& data) {
  return serialize_clientbound_chunk_section_data(data);
}

inline Expected<ClientboundChunkData, ProtocolError> deserialize_clientbound_chunk_data(std::span<const u8> bytes) {
  return deserialize_clientbound_chunk_section_data(bytes);
}

inline std::vector<u8> serialize_clientbound_chunk_unload(const ClientboundChunkUnload& unload) {
  return serialize_clientbound_chunk_section_unload(unload);
}

inline Expected<ClientboundChunkUnload, ProtocolError> deserialize_clientbound_chunk_unload(std::span<const u8> bytes) {
  return deserialize_clientbound_chunk_section_unload(bytes);
}

} // namespace arv::protocol
