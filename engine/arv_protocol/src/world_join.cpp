#include "arv/protocol/world_join.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_clientbound_world_join_result(const ClientboundWorldJoinResult& result) {
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
  write_f32(writer, result.environment.gravity_scale);
  writer.write_bool(result.environment.has_atmosphere);
  writer.write_bool(result.environment.has_oxygen);
  writer.write_string(result.environment.sky);
  write_f32(writer, result.environment.ambient_light);
  writer.write_string(result.environment.fog);
  writer.write_bool(result.space_environment);
  writer.write<u32>(static_cast<u32>(result.space_bodies.size()));
  for (const ClientboundSpaceBodyView& body : result.space_bodies) {
    writer.write_string(body.body_id);
    writer.write_string(body.display_name);
    writer.write_string(body.target_world_id);
    writer.write_string(body.render_model);
    writer.write_string(body.render_texture);
    write_f32(writer, body.position.x);
    write_f32(writer, body.position.y);
    write_f32(writer, body.position.z);
    write_f32(writer, body.visual_radius);
    write_f32(writer, body.entry_radius);
    writer.write_bool(body.entry_enabled);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundWorldJoinResult, ProtocolError> deserialize_clientbound_world_join_result(std::span<const u8> bytes) {
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
  auto environment_gravity_scale = read_f32(reader);
  auto environment_has_atmosphere = reader.read_bool();
  auto environment_has_oxygen = reader.read_bool();
  auto environment_sky = reader.read_string();
  auto environment_ambient_light = read_f32(reader);
  auto environment_fog = reader.read_string();
  auto space_environment = reader.read_bool();
  auto space_body_count = reader.read<u32>();
  if (!accepted || !reason || !world_id || !player_id || !entity_id || !pos_x ||
      !pos_y || !pos_z || !initial_tick || !tick_rate || !view_distance ||
      !environment_gravity_scale || !environment_has_atmosphere ||
      !environment_has_oxygen || !environment_sky || !environment_ambient_light ||
      !environment_fog || !space_environment || !space_body_count) {
    return Expected<ClientboundWorldJoinResult, ProtocolError>::failure(
        ProtocolError{"failed to decode world join result"});
  }

  std::vector<ClientboundSpaceBodyView> space_bodies;
  space_bodies.reserve(space_body_count.value());
  for (u32 index = 0; index < space_body_count.value(); ++index) {
    auto body_id = reader.read_string();
    auto display_name = reader.read_string();
    auto target_world_id = reader.read_string();
    auto render_model = reader.read_string();
    auto render_texture = reader.read_string();
    auto body_x = read_f32(reader);
    auto body_y = read_f32(reader);
    auto body_z = read_f32(reader);
    auto visual_radius = read_f32(reader);
    auto entry_radius = read_f32(reader);
    auto entry_enabled = reader.read_bool();
    if (!body_id || !display_name || !target_world_id || !render_model ||
        !render_texture || !body_x || !body_y || !body_z || !visual_radius ||
        !entry_radius || !entry_enabled) {
      return Expected<ClientboundWorldJoinResult, ProtocolError>::failure(
          ProtocolError{"failed to decode world join space body"});
    }
    space_bodies.push_back({
        std::move(body_id.value()),
        std::move(display_name.value()),
        std::move(target_world_id.value()),
        std::move(render_model.value()),
        std::move(render_texture.value()),
        {body_x.value(), body_y.value(), body_z.value()},
        visual_radius.value(),
        entry_radius.value(),
        entry_enabled.value()});
  }

  auto trailing = require_no_trailing_bytes(reader, "world join message");
  if (!trailing) {
    return Expected<ClientboundWorldJoinResult, ProtocolError>::failure(trailing.error());
  }

  ClientboundWorldJoinResult result;
  result.accepted = accepted.value();
  result.reject_reason = std::move(reason.value());
  result.world_id = WorldID{world_id.value()};
  result.player_id = PlayerID{player_id.value()};
  result.local_entity_id = EntityNetID{entity_id.value()};
  result.spawn_position = {pos_x.value(), pos_y.value(), pos_z.value()};
  result.initial_tick = initial_tick.value();
  result.tick_rate = tick_rate.value();
  result.effective_view_distance = view_distance.value();
  result.environment.gravity_scale = environment_gravity_scale.value();
  result.environment.has_atmosphere = environment_has_atmosphere.value();
  result.environment.has_oxygen = environment_has_oxygen.value();
  result.environment.sky = std::move(environment_sky.value());
  result.environment.ambient_light = environment_ambient_light.value();
  result.environment.fog = std::move(environment_fog.value());
  result.space_environment = space_environment.value();
  result.space_bodies = std::move(space_bodies);
  return Expected<ClientboundWorldJoinResult, ProtocolError>::success(std::move(result));
}

std::vector<u8> serialize_clientbound_world_leave(const ClientboundWorldLeave& leave) {
  MessageWriter writer;
  writer.write<u32>(leave.world_id.value);
  writer.write<u16>(leave.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundWorldLeave, ProtocolError> deserialize_clientbound_world_leave(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto reason = reader.read<u16>();
  if (!world_id || !reason) {
    return Expected<ClientboundWorldLeave, ProtocolError>::failure(
        ProtocolError{"failed to decode world leave"});
  }
  auto trailing = require_no_trailing_bytes(reader, "world join message");
  if (!trailing) {
    return Expected<ClientboundWorldLeave, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundWorldLeave, ProtocolError>::success({WorldID{world_id.value()}, reason.value()});
}

} // namespace arv::protocol
