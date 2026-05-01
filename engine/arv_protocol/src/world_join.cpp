#include "arv/protocol/world_join.hpp"

#include <bit>
#include <utility>

namespace arv::protocol {
namespace {

void write_f32(MessageWriter& writer, f32 value) {
  writer.write<u32>(std::bit_cast<u32>(value));
}

Expected<f32, ProtocolError> read_f32(MessageReader& reader) {
  auto value = reader.read<u32>();
  if (!value) {
    return Expected<f32, ProtocolError>::failure(value.error());
  }
  return Expected<f32, ProtocolError>::success(std::bit_cast<f32>(value.value()));
}

Expected<void, ProtocolError> require_no_trailing_bytes(MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(ProtocolError{"trailing bytes in world join message"});
  }
  return Expected<void, ProtocolError>::success();
}

} // namespace

std::vector<u8> serialize_world_join_result(const WorldJoinResult& result) {
  MessageWriter writer;
  writer.write_bool(result.accepted);
  writer.write_string(result.reject_reason);
  writer.write<u32>(result.world_id.value);
  writer.write<u64>(result.player_id.value);
  writer.write<u32>(result.local_entity_id.value);
  write_f32(writer, result.spawn_position.x);
  write_f32(writer, result.spawn_position.y);
  write_f32(writer, result.spawn_position.z);
  writer.write<u64>(result.initial_tick);
  writer.write<u32>(result.tick_rate);
  writer.write<i32>(result.effective_view_distance);
  return std::move(writer).take_bytes();
}

Expected<WorldJoinResult, ProtocolError> deserialize_world_join_result(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto accepted = reader.read_bool();
  auto reason = reader.read_string();
  auto world_id = reader.read<u32>();
  auto player_id = reader.read<u64>();
  auto entity_id = reader.read<u32>();
  auto pos_x = read_f32(reader);
  auto pos_y = read_f32(reader);
  auto pos_z = read_f32(reader);
  auto initial_tick = reader.read<u64>();
  auto tick_rate = reader.read<u32>();
  auto view_distance = reader.read<i32>();
  if (!accepted || !reason || !world_id || !player_id || !entity_id || !pos_x ||
      !pos_y || !pos_z || !initial_tick || !tick_rate || !view_distance) {
    return Expected<WorldJoinResult, ProtocolError>::failure(
        ProtocolError{"failed to decode world join result"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<WorldJoinResult, ProtocolError>::failure(trailing.error());
  }

  WorldJoinResult result;
  result.accepted = accepted.value();
  result.reject_reason = std::move(reason.value());
  result.world_id = WorldID{world_id.value()};
  result.player_id = PlayerID{player_id.value()};
  result.local_entity_id = EntityNetID{entity_id.value()};
  result.spawn_position = {pos_x.value(), pos_y.value(), pos_z.value()};
  result.initial_tick = initial_tick.value();
  result.tick_rate = tick_rate.value();
  result.effective_view_distance = view_distance.value();
  return Expected<WorldJoinResult, ProtocolError>::success(std::move(result));
}

std::vector<u8> serialize_world_leave(const WorldLeave& leave) {
  MessageWriter writer;
  writer.write<u32>(leave.world_id.value);
  writer.write<u16>(leave.reason);
  return std::move(writer).take_bytes();
}

Expected<WorldLeave, ProtocolError> deserialize_world_leave(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto reason = reader.read<u16>();
  if (!world_id || !reason) {
    return Expected<WorldLeave, ProtocolError>::failure(
        ProtocolError{"failed to decode world leave"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<WorldLeave, ProtocolError>::failure(trailing.error());
  }
  return Expected<WorldLeave, ProtocolError>::success({WorldID{world_id.value()}, reason.value()});
}

} // namespace arv::protocol
