#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
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

struct ChunkArrayEncodingPlan {
  ChunkArrayEncoding encoding{ChunkArrayEncoding::Dense};
  usize runs{};
  usize wire_size{};
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

template<typename T, usize Size>
[[nodiscard]] ChunkArrayEncodingPlan plan_encoded_array(
    const std::array<T, Size>& values,
    usize value_wire_size) noexcept {
  const usize dense_size = Size * value_wire_size;
  const usize runs = run_count(values);
  const usize rle_size = rle_array_wire_size(runs, value_wire_size);
  const ChunkArrayEncoding encoding = best_array_encoding(dense_size, rle_size);
  return {
      encoding,
      runs,
      sizeof(u8) + (encoding == ChunkArrayEncoding::Dense ? dense_size : rle_size)};
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

template<usize Size>
void write_dense_array(
    MessageWriter& writer,
    const std::array<voxel::BlockVariantID, Size>& values) {
  static_assert(std::is_trivially_copyable_v<voxel::BlockVariantID>);
  static_assert(sizeof(voxel::BlockVariantID) == sizeof(u32));
  if constexpr (std::endian::native == std::endian::little) {
    writer.write_bytes(std::as_bytes(std::span<const voxel::BlockVariantID, Size>{values}));
  } else {
    for (const voxel::BlockVariantID value : values) {
      write_array_value(writer, value);
    }
  }
}

template<usize Size>
void write_dense_array(
    MessageWriter& writer,
    const std::array<voxel::LightWord, Size>& values) {
  static_assert(std::is_trivially_copyable_v<voxel::LightWord>);
  static_assert(sizeof(voxel::LightWord) == sizeof(u16));
  if constexpr (std::endian::native == std::endian::little) {
    writer.write_bytes(std::as_bytes(std::span<const voxel::LightWord, Size>{values}));
  } else {
    for (const voxel::LightWord value : values) {
      write_array_value(writer, value);
    }
  }
}

template<typename T, usize Size>
void write_rle_array(
    MessageWriter& writer,
    const std::array<T, Size>& values,
    usize runs) {
  static_assert(Size <= static_cast<usize>(std::numeric_limits<u16>::max()));
  writer.write<u16>(static_cast<u16>(runs));
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
    ChunkArrayEncodingPlan plan) {
  writer.write<ChunkArrayEncoding>(plan.encoding);
  switch (plan.encoding) {
    case ChunkArrayEncoding::Dense:
      write_dense_array(writer, values);
      return;
    case ChunkArrayEncoding::RunLength:
      write_rle_array(writer, values, plan.runs);
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

template<usize Size>
Expected<void, ProtocolError> read_dense_array(
    MessageReader& reader,
    std::array<voxel::BlockVariantID, Size>& values,
    ArrayValueReader<voxel::BlockVariantID> read_value,
    const char* failure_message) {
  static_assert(std::is_trivially_copyable_v<voxel::BlockVariantID>);
  static_assert(sizeof(voxel::BlockVariantID) == sizeof(u32));
  if constexpr (std::endian::native == std::endian::little) {
    auto decoded = reader.read_bytes(
        std::as_writable_bytes(std::span<voxel::BlockVariantID, Size>{values}));
    if (!decoded) {
      return Expected<void, ProtocolError>::failure(ProtocolError{failure_message});
    }
    return Expected<void, ProtocolError>::success();
  } else {
    return read_dense_array<voxel::BlockVariantID, Size>(
        reader,
        values,
        read_value,
        failure_message);
  }
}

template<usize Size>
Expected<void, ProtocolError> read_dense_array(
    MessageReader& reader,
    std::array<voxel::LightWord, Size>& values,
    ArrayValueReader<voxel::LightWord> read_value,
    const char* failure_message) {
  static_assert(std::is_trivially_copyable_v<voxel::LightWord>);
  static_assert(sizeof(voxel::LightWord) == sizeof(u16));
  if constexpr (std::endian::native == std::endian::little) {
    auto decoded = reader.read_bytes(
        std::as_writable_bytes(std::span<voxel::LightWord, Size>{values}));
    if (!decoded) {
      return Expected<void, ProtocolError>::failure(ProtocolError{failure_message});
    }
    return Expected<void, ProtocolError>::success();
  } else {
    return read_dense_array<voxel::LightWord, Size>(
        reader,
        values,
        read_value,
        failure_message);
  }
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
    auto out = values.begin() + static_cast<std::ptrdiff_t>(index);
    std::fill_n(out, length.value(), decoded.value());
    index += length.value();
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

std::vector<u8> serialize_clientbound_chunk_section_data_arrays(
    WorldID world_id,
    ChunkSectionCoord coord,
    u64 chunk_version,
    u64 chunk_content_version,
    u64 chunk_light_version,
    bool has_lighting,
    bool lighting_valid,
    bool has_fluids,
    const std::array<voxel::BlockVariantID, voxel::ChunkVolume>& blocks,
    const std::array<voxel::LightWord, voxel::ChunkVolume>* lights,
    const std::array<voxel::FluidCell, voxel::ChunkVolume>* fluids) {
  const ChunkArrayEncodingPlan block_plan =
      plan_encoded_array(blocks, sizeof(u32));
  const ChunkArrayEncodingPlan light_plan =
      has_lighting && lights != nullptr
          ? plan_encoded_array(*lights, sizeof(voxel::LightWord))
          : ChunkArrayEncodingPlan{};
  const ChunkArrayEncodingPlan fluid_plan =
      has_fluids && fluids != nullptr
          ? plan_encoded_array(*fluids, fluid_wire_size())
          : ChunkArrayEncodingPlan{};
  const usize reserve_bytes =
      chunk_data_header_wire_size() +
      block_plan.wire_size +
      (has_lighting && lights != nullptr ? light_plan.wire_size : 0U) +
      (has_fluids && fluids != nullptr ? fluid_plan.wire_size : 0U);
  MessageWriter writer(reserve_bytes);
  writer.write<u32>(world_id.value);
  write_chunk_coord(writer, coord);
  writer.write<u64>(chunk_version);
  writer.write<u64>(chunk_content_version);
  writer.write<u64>(chunk_light_version);
  writer.write_bool(has_lighting && lights != nullptr);
  writer.write_bool(lighting_valid);
  writer.write_bool(has_fluids && fluids != nullptr);
  write_encoded_array(writer, blocks, block_plan);
  if (has_lighting && lights != nullptr) {
    write_encoded_array(writer, *lights, light_plan);
  }
  if (has_fluids && fluids != nullptr) {
    write_encoded_array(writer, *fluids, fluid_plan);
  }
  return std::move(writer).take_bytes();
}

std::vector<u8> serialize_clientbound_chunk_section_data(const ClientboundChunkSectionData& data) {
  return serialize_clientbound_chunk_section_data_arrays(
      data.world_id,
      data.coord,
      data.chunk_version,
      data.chunk_content_version,
      data.chunk_light_version,
      data.has_lighting,
      data.lighting_valid,
      data.has_fluids,
      data.blocks,
      data.has_lighting && data.lights.has_value() ? &data.lights.value() : nullptr,
      data.has_fluids && data.fluids.has_value() ? &data.fluids.value() : nullptr);
}

Expected<void, ProtocolError> deserialize_clientbound_chunk_section_data_into(
    std::span<const u8> bytes,
    ClientboundChunkSectionData& data) {
  data.lights.reset();
  data.fluids.reset();
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
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"failed to decode chunk section data header"});
  }

  data.world_id = WorldID{world_id.value()};
  data.coord = coord.value();
  data.chunk_version = chunk_version.value();
  data.chunk_content_version = chunk_content_version.value();
  data.chunk_light_version = chunk_light_version.value();
  data.has_lighting = has_lighting.value();
  data.lighting_valid = lighting_valid.value();
  data.has_fluids = has_fluids.value();

  auto blocks = read_encoded_array(
      reader,
      data.blocks,
      read_block_variant,
      "failed to decode chunk section block data");
  if (!blocks) {
    return Expected<void, ProtocolError>::failure(blocks.error());
  }

  if (data.has_lighting) {
    data.lights.emplace();
    auto lights = read_encoded_array(
        reader,
        data.lights.value(),
        read_light_word,
        "failed to decode chunk section lighting data");
    if (!lights) {
      return Expected<void, ProtocolError>::failure(lights.error());
    }
  }
  if (data.has_fluids) {
    data.fluids.emplace();
    auto fluids = read_encoded_array(
        reader,
        data.fluids.value(),
        read_fluid_cell,
        "failed to decode chunk section fluid data");
    if (!fluids) {
      return Expected<void, ProtocolError>::failure(fluids.error());
    }
  }

  auto trailing = require_no_trailing_bytes(reader, "chunk message");
  if (!trailing) {
    return Expected<void, ProtocolError>::failure(trailing.error());
  }
  return Expected<void, ProtocolError>::success();
}

Expected<ClientboundChunkSectionData, ProtocolError> deserialize_clientbound_chunk_section_data(std::span<const u8> bytes) {
  ClientboundChunkSectionData data;
  auto decoded = deserialize_clientbound_chunk_section_data_into(bytes, data);
  if (!decoded) {
    return Expected<ClientboundChunkSectionData, ProtocolError>::failure(decoded.error());
  }
  return Expected<ClientboundChunkSectionData, ProtocolError>::success_in_place(std::move(data));
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
