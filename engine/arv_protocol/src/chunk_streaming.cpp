#include "arv/protocol/chunk_streaming.hpp"

#include <utility>

namespace arv::protocol {
namespace {

void write_chunk_coord(MessageWriter& writer, ChunkCoord coord) {
  writer.write<i32>(coord.value.x);
  writer.write<i32>(coord.value.y);
  writer.write<i32>(coord.value.z);
}

Expected<ChunkCoord, ProtocolError> read_chunk_coord(MessageReader& reader) {
  auto x = reader.read<i32>();
  auto y = reader.read<i32>();
  auto z = reader.read<i32>();
  if (!x || !y || !z) {
    return Expected<ChunkCoord, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk coordinate"});
  }
  return Expected<ChunkCoord, ProtocolError>::success({{x.value(), y.value(), z.value()}});
}

Expected<void, ProtocolError> require_no_trailing_bytes(MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(ProtocolError{"trailing bytes in chunk message"});
  }
  return Expected<void, ProtocolError>::success();
}

[[nodiscard]] constexpr usize chunk_data_wire_size(bool has_lighting) noexcept {
  return sizeof(u32) + sizeof(i32) * 3U + sizeof(u64) * 3U + sizeof(u8) +
         voxel::ChunkVolume * sizeof(u32) +
         (has_lighting ? voxel::ChunkVolume * sizeof(voxel::LightWord) : 0U);
}

} // namespace

std::vector<u8> serialize_chunk_interest_update(const ChunkInterestUpdate& update) {
  MessageWriter writer;
  writer.write<u32>(update.world_id.value);
  write_chunk_coord(writer, update.center_chunk);
  writer.write<i32>(update.client_view_distance);
  writer.write_bool(update.previous_center_chunk.has_value());
  if (update.previous_center_chunk.has_value()) {
    write_chunk_coord(writer, update.previous_center_chunk.value());
  }
  writer.write<u32>(update.sequence_id);
  return std::move(writer).take_bytes();
}

Expected<ChunkInterestUpdate, ProtocolError> deserialize_chunk_interest_update(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto center = read_chunk_coord(reader);
  auto view_distance = reader.read<i32>();
  auto has_previous = reader.read_bool();
  if (!world_id || !center || !view_distance || !has_previous) {
    return Expected<ChunkInterestUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk interest update header"});
  }

  std::optional<ChunkCoord> previous;
  if (has_previous.value()) {
    auto previous_coord = read_chunk_coord(reader);
    if (!previous_coord) {
      return Expected<ChunkInterestUpdate, ProtocolError>::failure(previous_coord.error());
    }
    previous = previous_coord.value();
  }

  auto sequence = reader.read<u32>();
  if (!sequence) {
    return Expected<ChunkInterestUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk interest update sequence"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ChunkInterestUpdate, ProtocolError>::failure(trailing.error());
  }

  ChunkInterestUpdate update;
  update.world_id = WorldID{world_id.value()};
  update.center_chunk = center.value();
  update.client_view_distance = view_distance.value();
  update.previous_center_chunk = previous;
  update.sequence_id = sequence.value();
  return Expected<ChunkInterestUpdate, ProtocolError>::success(update);
}

std::vector<u8> serialize_chunk_data(const ChunkData& data) {
  MessageWriter writer(chunk_data_wire_size(data.has_lighting));
  writer.write<u32>(data.world_id.value);
  write_chunk_coord(writer, data.coord);
  writer.write<u64>(data.chunk_version);
  writer.write<u64>(data.chunk_content_version);
  writer.write<u64>(data.chunk_light_version);
  writer.write_bool(data.has_lighting);
  for (const voxel::BlockVariantID block : data.blocks) {
    writer.write<u32>(block.value);
  }
  if (data.has_lighting) {
    for (const voxel::LightWord light : data.lights) {
      writer.write<voxel::LightWord>(light);
    }
  }
  return std::move(writer).take_bytes();
}

Expected<ChunkData, ProtocolError> deserialize_chunk_data(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto coord = read_chunk_coord(reader);
  auto chunk_version = reader.read<u64>();
  auto chunk_content_version = reader.read<u64>();
  auto chunk_light_version = reader.read<u64>();
  auto has_lighting = reader.read_bool();
  if (!world_id || !coord || !chunk_version || !chunk_content_version ||
      !chunk_light_version || !has_lighting) {
    return Expected<ChunkData, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk data header"});
  }

  ChunkData data;
  data.world_id = WorldID{world_id.value()};
  data.coord = coord.value();
  data.chunk_version = chunk_version.value();
  data.chunk_content_version = chunk_content_version.value();
  data.chunk_light_version = chunk_light_version.value();
  data.has_lighting = has_lighting.value();
  data.blocks.fill(voxel::AirBlockVariantID);
  data.lights.fill(voxel::NoLight);

  for (voxel::BlockVariantID& block : data.blocks) {
    auto value = reader.read<u32>();
    if (!value) {
      return Expected<ChunkData, ProtocolError>::failure(
          ProtocolError{"failed to decode chunk block data"});
    }
    block = voxel::BlockVariantID{value.value()};
  }

  if (data.has_lighting) {
    for (voxel::LightWord& light : data.lights) {
      auto value = reader.read<voxel::LightWord>();
      if (!value) {
        return Expected<ChunkData, ProtocolError>::failure(
            ProtocolError{"failed to decode chunk lighting data"});
      }
      light = value.value();
    }
  }

  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ChunkData, ProtocolError>::failure(trailing.error());
  }
  return Expected<ChunkData, ProtocolError>::success(std::move(data));
}

std::vector<u8> serialize_chunk_unload(const ChunkUnload& unload) {
  MessageWriter writer;
  writer.write<u32>(unload.world_id.value);
  write_chunk_coord(writer, unload.coord);
  return std::move(writer).take_bytes();
}

Expected<ChunkUnload, ProtocolError> deserialize_chunk_unload(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto coord = read_chunk_coord(reader);
  if (!world_id || !coord) {
    return Expected<ChunkUnload, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk unload"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ChunkUnload, ProtocolError>::failure(trailing.error());
  }

  return Expected<ChunkUnload, ProtocolError>::success({WorldID{world_id.value()}, coord.value()});
}

} // namespace arv::protocol
