#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/message_writer.hpp"

#include <bit>
#include <string>
#include <string_view>
#include <utility>

namespace arv::protocol {

inline void write_f32(MessageWriter& writer, f32 value) {
  writer.write<u32>(std::bit_cast<u32>(value));
}

[[nodiscard]] inline Expected<f32, ProtocolError> read_f32(MessageReader& reader) {
  auto value = reader.read<u32>();
  if (!value) {
    return Expected<f32, ProtocolError>::failure(value.error());
  }
  return Expected<f32, ProtocolError>::success(std::bit_cast<f32>(value.value()));
}

inline void write_vec3(MessageWriter& writer, Vec3f value) {
  write_f32(writer, value.x);
  write_f32(writer, value.y);
  write_f32(writer, value.z);
}

[[nodiscard]] inline Expected<Vec3f, ProtocolError> read_vec3(MessageReader& reader) {
  auto x = read_f32(reader);
  auto y = read_f32(reader);
  auto z = read_f32(reader);
  if (!x || !y || !z) {
    return Expected<Vec3f, ProtocolError>::failure(ProtocolError{"failed to decode vector"});
  }
  return Expected<Vec3f, ProtocolError>::success({x.value(), y.value(), z.value()});
}

inline void write_quat(MessageWriter& writer, Quatf value) {
  write_f32(writer, value.x);
  write_f32(writer, value.y);
  write_f32(writer, value.z);
  write_f32(writer, value.w);
}

[[nodiscard]] inline Expected<Quatf, ProtocolError> read_quat(MessageReader& reader) {
  auto x = read_f32(reader);
  auto y = read_f32(reader);
  auto z = read_f32(reader);
  auto w = read_f32(reader);
  if (!x || !y || !z || !w) {
    return Expected<Quatf, ProtocolError>::failure(ProtocolError{"failed to decode rotation"});
  }
  return Expected<Quatf, ProtocolError>::success({x.value(), y.value(), z.value(), w.value()});
}

inline void write_block_pos(MessageWriter& writer, BlockPos pos) {
  writer.write<i32>(pos.value.x);
  writer.write<i32>(pos.value.y);
  writer.write<i32>(pos.value.z);
}

[[nodiscard]] inline Expected<BlockPos, ProtocolError> read_block_pos(MessageReader& reader) {
  auto x = reader.read<i32>();
  auto y = reader.read<i32>();
  auto z = reader.read<i32>();
  if (!x || !y || !z) {
    return Expected<BlockPos, ProtocolError>::failure(
        ProtocolError{"failed to decode block position"});
  }
  return Expected<BlockPos, ProtocolError>::success({{x.value(), y.value(), z.value()}});
}

[[nodiscard]] inline Expected<void, ProtocolError> require_no_trailing_bytes(
    MessageReader& reader,
    std::string_view packet_name) {
  if (reader.remaining() == 0U) {
    return Expected<void, ProtocolError>::success();
  }
  std::string message{"trailing bytes in "};
  message.append(packet_name);
  return Expected<void, ProtocolError>::failure(ProtocolError{std::move(message)});
}

} // namespace arv::protocol
