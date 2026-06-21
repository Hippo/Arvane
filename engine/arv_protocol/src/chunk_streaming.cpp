#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <array>
#include <limits>
#include <utility>

namespace arv::protocol {
namespace {

void write_chunk_coord(MessageWriter& writer, ChunkSectionCoord coord) {
  writer.write<i32>(coord.value.x);
  writer.write<i32>(coord.value.y);
  writer.write<i32>(coord.value.z);
}

Expected<ChunkSectionCoord, ProtocolError> read_chunk_coord(MessageReader& reader) {
  auto x = reader.read<i32>();
  auto y = reader.read<i32>();
  auto z = reader.read<i32>();
  if (!x || !y || !z) {
    return Expected<ChunkSectionCoord, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk coordinate"});
  }
  return Expected<ChunkSectionCoord, ProtocolError>::success({{x.value(), y.value(), z.value()}});
}

enum class ChunkArrayEncoding : u8 {
  Dense = 0,
  RunLength = 1,
};

[[nodiscard]] constexpr usize fluid_wire_size() noexcept {
  return sizeof(u32) + sizeof(u8) + sizeof(u8);
}

[[nodiscard]] constexpr usize chunk_data_header_wire_size() noexcept {
  return sizeof(u32) + sizeof(i32) * 3U + sizeof(u64) * 3U + sizeof(u8) * 3U;
}

template<typename T, usize Size>
[[nodiscard]] usize run_count(const std::array<T, Size>& values) noexcept {
  static_assert(Size <= static_cast<usize>(std::numeric_limits<u16>::max()));
  if constexpr (Size == 0U) {
    return 0U;
  } else {
    usize runs = 1U;
    for (usize index = 1U; index < Size; ++index) {
      if (!(values[index] == values[index - 1U])) {
        ++runs;
      }
    }
    return runs;
  }
}

[[nodiscard]] usize rle_array_wire_size(usize runs, usize value_wire_size) noexcept {
  return sizeof(u16) + runs * (sizeof(u16) + value_wire_size);
}

[[nodiscard]] ChunkArrayEncoding best_array_encoding(
    usize dense_wire_size,
    usize rle_wire_size) noexcept {
  return rle_wire_size < dense_wire_size ? ChunkArrayEncoding::RunLength
                                         : ChunkArrayEncoding::Dense;
}

void write_array_value(MessageWriter& writer, voxel::BlockVariantID value) {
  writer.write<u32>(value.value);
}

void write_array_value(MessageWriter& writer, voxel::LightWord value) {
  writer.write<voxel::LightWord>(value);
}

void write_array_value(MessageWriter& writer, voxel::FluidCell value) {
  writer.write<u32>(value.type.value);
  writer.write<u8>(value.level);
  writer.write<u8>(static_cast<u8>(value.flags));
}

template<typename T, usize Size>
void write_dense_array(MessageWriter& writer, const std::array<T, Size>& values) {
  for (const T& value : values) {
    write_array_value(writer, value);
  }
}

template<typename T, usize Size>
void write_rle_array(MessageWriter& writer, const std::array<T, Size>& values) {
  static_assert(Size <= static_cast<usize>(std::numeric_limits<u16>::max()));
  writer.write<u16>(static_cast<u16>(run_count(values)));
  usize index = 0U;
  while (index < Size) {
    const T& value = values[index];
    usize length = 1U;
    while (index + length < Size && values[index + length] == value) {
      ++length;
    }
    writer.write<u16>(static_cast<u16>(length));
    write_array_value(writer, value);
    index += length;
  }
}

template<typename T, usize Size>
void write_encoded_array(
    MessageWriter& writer,
    const std::array<T, Size>& values,
    usize value_wire_size) {
  const usize dense_size = Size * value_wire_size;
  const usize rle_size = rle_array_wire_size(run_count(values), value_wire_size);
  const ChunkArrayEncoding encoding = best_array_encoding(dense_size, rle_size);
  writer.write<ChunkArrayEncoding>(encoding);
  switch (encoding) {
    case ChunkArrayEncoding::Dense:
      write_dense_array(writer, values);
      return;
    case ChunkArrayEncoding::RunLength:
      write_rle_array(writer, values);
      return;
  }
}

Expected<voxel::BlockVariantID, ProtocolError> read_block_variant(MessageReader& reader) {
  auto value = reader.read<u32>();
  if (!value) {
    return Expected<voxel::BlockVariantID, ProtocolError>::failure(value.error());
  }
  return Expected<voxel::BlockVariantID, ProtocolError>::success(
      voxel::BlockVariantID{value.value()});
}

Expected<voxel::LightWord, ProtocolError> read_light_word(MessageReader& reader) {
  auto value = reader.read<voxel::LightWord>();
  if (!value) {
    return Expected<voxel::LightWord, ProtocolError>::failure(value.error());
  }
  return Expected<voxel::LightWord, ProtocolError>::success(value.value());
}

Expected<voxel::FluidCell, ProtocolError> read_fluid_cell(MessageReader& reader) {
  auto type = reader.read<u32>();
  auto level = reader.read<u8>();
  auto flags = reader.read<u8>();
  if (!type || !level || !flags) {
    return Expected<voxel::FluidCell, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk section array value"});
  }
  return Expected<voxel::FluidCell, ProtocolError>::success(
      {voxel::FluidTypeID{type.value()},
       level.value(),
       static_cast<voxel::FluidFlags>(flags.value())});
}

template<typename T>
using ArrayValueReader = Expected<T, ProtocolError> (*)(MessageReader&);

template<typename T, usize Size>
Expected<void, ProtocolError> read_dense_array(
    MessageReader& reader,
    std::array<T, Size>& values,
    ArrayValueReader<T> read_value,
    const char* failure_message) {
  for (T& value : values) {
    auto decoded = read_value(reader);
    if (!decoded) {
      return Expected<void, ProtocolError>::failure(
          ProtocolError{failure_message});
    }
    value = decoded.value();
  }
  return Expected<void, ProtocolError>::success();
}

template<typename T, usize Size>
Expected<void, ProtocolError> read_rle_array(
    MessageReader& reader,
    std::array<T, Size>& values,
    ArrayValueReader<T> read_value,
    const char* failure_message) {
  auto encoded_run_count = reader.read<u16>();
  if (!encoded_run_count) {
    return Expected<void, ProtocolError>::failure(ProtocolError{failure_message});
  }
  if (encoded_run_count.value() == 0U) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"chunk section array has no runs"});
  }

  usize index = 0U;
  for (u16 run = 0U; run < encoded_run_count.value(); ++run) {
    auto length = reader.read<u16>();
    auto decoded = read_value(reader);
    if (!length || !decoded) {
      return Expected<void, ProtocolError>::failure(
          ProtocolError{failure_message});
    }
    if (length.value() == 0U ||
        static_cast<usize>(length.value()) > Size - index) {
      return Expected<void, ProtocolError>::failure(
          ProtocolError{"chunk section array run length is invalid"});
    }
    for (usize count = 0U; count < length.value(); ++count) {
      values[index++] = decoded.value();
    }
  }

  if (index != Size) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"chunk section array run length total is invalid"});
  }
  return Expected<void, ProtocolError>::success();
}

template<typename T, usize Size>
Expected<void, ProtocolError> read_encoded_array(
    MessageReader& reader,
    std::array<T, Size>& values,
    ArrayValueReader<T> read_value,
    const char* failure_message) {
  auto encoding = reader.read<ChunkArrayEncoding>();
  if (!encoding) {
    return Expected<void, ProtocolError>::failure(ProtocolError{failure_message});
  }

  switch (encoding.value()) {
    case ChunkArrayEncoding::Dense:
      return read_dense_array(reader, values, read_value, failure_message);
    case ChunkArrayEncoding::RunLength:
      return read_rle_array(reader, values, read_value, failure_message);
  }

  return Expected<void, ProtocolError>::failure(
      ProtocolError{"unknown chunk section array encoding"});
}

} // namespace

std::vector<u8> serialize_serverbound_chunk_interest_update(const ServerboundChunkInterestUpdate& update) {
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

Expected<ServerboundChunkInterestUpdate, ProtocolError> deserialize_serverbound_chunk_interest_update(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto center = read_chunk_coord(reader);
  auto view_distance = reader.read<i32>();
  auto has_previous = reader.read_bool();
  if (!world_id || !center || !view_distance || !has_previous) {
    return Expected<ServerboundChunkInterestUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk interest update header"});
  }

  std::optional<ChunkSectionCoord> previous;
  if (has_previous.value()) {
    auto previous_coord = read_chunk_coord(reader);
    if (!previous_coord) {
      return Expected<ServerboundChunkInterestUpdate, ProtocolError>::failure(previous_coord.error());
    }
    previous = previous_coord.value();
  }

  auto sequence = reader.read<u32>();
  if (!sequence) {
    return Expected<ServerboundChunkInterestUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk interest update sequence"});
  }
  auto trailing = require_no_trailing_bytes(reader, "chunk message");
  if (!trailing) {
    return Expected<ServerboundChunkInterestUpdate, ProtocolError>::failure(trailing.error());
  }

  ServerboundChunkInterestUpdate update;
  update.world_id = WorldID{world_id.value()};
  update.center_chunk = center.value();
  update.client_view_distance = view_distance.value();
  update.previous_center_chunk = previous;
  update.sequence_id = sequence.value();
  return Expected<ServerboundChunkInterestUpdate, ProtocolError>::success(update);
}

std::vector<u8> serialize_clientbound_chunk_section_data(const ClientboundChunkSectionData& data) {
  MessageWriter writer(chunk_data_header_wire_size());
  writer.write<u32>(data.world_id.value);
  write_chunk_coord(writer, data.coord);
  writer.write<u64>(data.chunk_version);
  writer.write<u64>(data.chunk_content_version);
  writer.write<u64>(data.chunk_light_version);
  writer.write_bool(data.has_lighting);
  writer.write_bool(data.lighting_valid);
  writer.write_bool(data.has_fluids);
  write_encoded_array(writer, data.blocks, sizeof(u32));
  if (data.has_lighting) {
    write_encoded_array(writer, data.lights, sizeof(voxel::LightWord));
  }
  if (data.has_fluids) {
    write_encoded_array(writer, data.fluids, fluid_wire_size());
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundChunkSectionData, ProtocolError> deserialize_clientbound_chunk_section_data(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto coord = read_chunk_coord(reader);
  auto chunk_version = reader.read<u64>();
  auto chunk_content_version = reader.read<u64>();
  auto chunk_light_version = reader.read<u64>();
  auto has_lighting = reader.read_bool();
  auto lighting_valid = reader.read_bool();
  auto has_fluids = reader.read_bool();
  if (!world_id || !coord || !chunk_version || !chunk_content_version ||
      !chunk_light_version || !has_lighting || !lighting_valid || !has_fluids) {
    return Expected<ClientboundChunkSectionData, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk section data header"});
  }

  ClientboundChunkSectionData data;
  data.world_id = WorldID{world_id.value()};
  data.coord = coord.value();
  data.chunk_version = chunk_version.value();
  data.chunk_content_version = chunk_content_version.value();
  data.chunk_light_version = chunk_light_version.value();
  data.has_lighting = has_lighting.value();
  data.lighting_valid = lighting_valid.value();
  data.has_fluids = has_fluids.value();
  data.blocks.fill(voxel::AirBlockVariantID);
  data.lights.fill(voxel::NoLight);
  data.fluids.fill(voxel::empty_fluid_cell());

  auto blocks = read_encoded_array(
      reader,
      data.blocks,
      read_block_variant,
      "failed to decode chunk section block data");
  if (!blocks) {
    return Expected<ClientboundChunkSectionData, ProtocolError>::failure(blocks.error());
  }

  if (data.has_lighting) {
    auto lights = read_encoded_array(
        reader,
        data.lights,
        read_light_word,
        "failed to decode chunk section lighting data");
    if (!lights) {
      return Expected<ClientboundChunkSectionData, ProtocolError>::failure(lights.error());
    }
  }
  if (data.has_fluids) {
    auto fluids = read_encoded_array(
        reader,
        data.fluids,
        read_fluid_cell,
        "failed to decode chunk section fluid data");
    if (!fluids) {
      return Expected<ClientboundChunkSectionData, ProtocolError>::failure(fluids.error());
    }
  }

  auto trailing = require_no_trailing_bytes(reader, "chunk message");
  if (!trailing) {
    return Expected<ClientboundChunkSectionData, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundChunkSectionData, ProtocolError>::success(std::move(data));
}

std::vector<u8> serialize_clientbound_chunk_section_unload(const ClientboundChunkSectionUnload& unload) {
  MessageWriter writer;
  writer.write<u32>(unload.world_id.value);
  write_chunk_coord(writer, unload.coord);
  return std::move(writer).take_bytes();
}

Expected<ClientboundChunkSectionUnload, ProtocolError> deserialize_clientbound_chunk_section_unload(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto coord = read_chunk_coord(reader);
  if (!world_id || !coord) {
    return Expected<ClientboundChunkSectionUnload, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk section unload"});
  }
  auto trailing = require_no_trailing_bytes(reader, "chunk message");
  if (!trailing) {
    return Expected<ClientboundChunkSectionUnload, ProtocolError>::failure(trailing.error());
  }

  return Expected<ClientboundChunkSectionUnload, ProtocolError>::success(
      {WorldID{world_id.value()}, coord.value()});
}

} // namespace arv::protocol
