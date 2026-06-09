#include "arv/protocol/world_time.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_clientbound_world_time(const ClientboundWorldTime& time) {
  MessageWriter writer;
  writer.write<u32>(time.world_id.value);
  writer.write<u64>(time.world_tick);
  writer.write<u32>(time.day_time_tick);
  writer.write<u32>(time.day_length_ticks);
  write_f32(writer, time.day_length_seconds);
  write_f32(writer, time.time_scale);
  writer.write_bool(time.daylight_cycle);
  return std::move(writer).take_bytes();
}

Expected<ClientboundWorldTime, ProtocolError> deserialize_clientbound_world_time(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto world_tick = reader.read<u64>();
  auto day_time_tick = reader.read<u32>();
  auto day_length_ticks = reader.read<u32>();
  auto day_length_seconds = read_f32(reader);
  auto time_scale = read_f32(reader);
  auto daylight_cycle = reader.read_bool();
  if (!world_id || !world_tick || !day_time_tick || !day_length_ticks ||
      !day_length_seconds || !time_scale || !daylight_cycle) {
    return Expected<ClientboundWorldTime, ProtocolError>::failure(
        ProtocolError{"failed to decode world time"});
  }
  auto trailing = require_no_trailing_bytes(reader, "world time message");
  if (!trailing) {
    return Expected<ClientboundWorldTime, ProtocolError>::failure(trailing.error());
  }

  ClientboundWorldTime time;
  time.world_id = WorldID{world_id.value()};
  time.world_tick = world_tick.value();
  time.day_time_tick = day_time_tick.value();
  time.day_length_ticks = day_length_ticks.value();
  time.day_length_seconds = day_length_seconds.value();
  time.time_scale = time_scale.value();
  time.daylight_cycle = daylight_cycle.value();
  return Expected<ClientboundWorldTime, ProtocolError>::success(time);
}

} // namespace arv::protocol
