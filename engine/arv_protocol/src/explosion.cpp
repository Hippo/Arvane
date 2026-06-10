#include "arv/protocol/explosion.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <cmath>
#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_clientbound_explosion_event(
    const ClientboundExplosionEvent& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.world_id.value);
  write_vec3(writer, packet.origin);
  writer.write_string(packet.profile_id);
  write_f32(writer, packet.radius);
  writer.write<u64>(packet.seed);
  writer.write_bool(packet.visual_enabled);
  writer.write_bool(packet.future_sound_enabled);
  writer.write_string(packet.future_sound_id);
  return std::move(writer).take_bytes();
}

Expected<ClientboundExplosionEvent, ProtocolError> deserialize_clientbound_explosion_event(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto origin = read_vec3(reader);
  auto profile_id = reader.read_string();
  auto radius = read_f32(reader);
  auto seed = reader.read<u64>();
  auto visual_enabled = reader.read_bool();
  auto future_sound_enabled = reader.read_bool();
  auto future_sound_id = reader.read_string();
  if (!world_id || !origin || !profile_id || !radius || !seed ||
      !visual_enabled || !future_sound_enabled || !future_sound_id) {
    return Expected<ClientboundExplosionEvent, ProtocolError>::failure(
        ProtocolError{"failed to decode explosion event message"});
  }
  if (profile_id.value().empty() ||
      profile_id.value().size() > MaxExplosionProfileIDBytes) {
    return Expected<ClientboundExplosionEvent, ProtocolError>::failure(
        ProtocolError{"invalid explosion profile id length"});
  }
  if (!std::isfinite(radius.value()) || radius.value() <= 0.0F) {
    return Expected<ClientboundExplosionEvent, ProtocolError>::failure(
        ProtocolError{"invalid explosion radius"});
  }
  if (future_sound_id.value().size() > MaxExplosionSoundIDBytes) {
    return Expected<ClientboundExplosionEvent, ProtocolError>::failure(
        ProtocolError{"invalid explosion sound id length"});
  }
  auto trailing = require_no_trailing_bytes(reader, "explosion event message");
  if (!trailing) {
    return Expected<ClientboundExplosionEvent, ProtocolError>::failure(trailing.error());
  }

  ClientboundExplosionEvent packet;
  packet.world_id = WorldID{world_id.value()};
  packet.origin = origin.value();
  packet.profile_id = std::move(profile_id.value());
  packet.radius = radius.value();
  packet.seed = seed.value();
  packet.visual_enabled = visual_enabled.value();
  packet.future_sound_enabled = future_sound_enabled.value();
  packet.future_sound_id = std::move(future_sound_id.value());
  return Expected<ClientboundExplosionEvent, ProtocolError>::success(std::move(packet));
}

} // namespace arv::protocol
