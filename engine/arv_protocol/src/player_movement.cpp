#include "arv/protocol/player_movement.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_serverbound_player_move(const ServerboundPlayerMove& move) {
  MessageWriter writer;
  writer.write<u32>(move.sequence);
  writer.write<u64>(move.client_tick);
  write_vec3(writer, move.position);
  write_vec3(writer, move.velocity);
  write_f32(writer, move.yaw_radians);
  write_f32(writer, move.pitch_radians);
  writer.write<PlayerMoveFlags>(move.flags);
  writer.write<PlayerActionFlags>(move.action_flags);
  writer.write<u8>(move.selected_hotbar_slot);
  return std::move(writer).take_bytes();
}

Expected<ServerboundPlayerMove, ProtocolError> deserialize_serverbound_player_move(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto sequence = reader.read<u32>();
  auto client_tick = reader.read<u64>();
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto yaw = read_f32(reader);
  auto pitch = read_f32(reader);
  auto flags = reader.read<PlayerMoveFlags>();
  auto action_flags = reader.read<PlayerActionFlags>();
  auto selected_hotbar_slot = reader.read<u8>();
  if (!sequence || !client_tick || !position || !velocity || !yaw || !pitch ||
      !flags || !action_flags || !selected_hotbar_slot) {
    return Expected<ServerboundPlayerMove, ProtocolError>::failure(
        ProtocolError{"failed to decode player move"});
  }
  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ServerboundPlayerMove, ProtocolError>::failure(trailing.error());
  }

  return Expected<ServerboundPlayerMove, ProtocolError>::success(
      {sequence.value(),
       client_tick.value(),
       position.value(),
       velocity.value(),
       yaw.value(),
       pitch.value(),
       flags.value(),
       action_flags.value(),
       selected_hotbar_slot.value()});
}

std::vector<u8> serialize_clientbound_player_move_correction(
    const ClientboundPlayerMoveCorrection& correction) {
  MessageWriter writer;
  writer.write<u32>(correction.rejected_sequence);
  write_vec3(writer, correction.position);
  write_vec3(writer, correction.velocity);
  write_f32(writer, correction.yaw_radians);
  write_f32(writer, correction.pitch_radians);
  writer.write<PlayerMoveFlags>(correction.flags);
  writer.write<PlayerMoveCorrectionReason>(correction.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerMoveCorrection, ProtocolError> deserialize_clientbound_player_move_correction(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto rejected_sequence = reader.read<u32>();
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto yaw = read_f32(reader);
  auto pitch = read_f32(reader);
  auto flags = reader.read<PlayerMoveFlags>();
  auto reason = reader.read<PlayerMoveCorrectionReason>();
  if (!rejected_sequence || !position || !velocity || !yaw || !pitch || !flags || !reason) {
    return Expected<ClientboundPlayerMoveCorrection, ProtocolError>::failure(
        ProtocolError{"failed to decode player move correction"});
  }
  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ClientboundPlayerMoveCorrection, ProtocolError>::failure(trailing.error());
  }

  return Expected<ClientboundPlayerMoveCorrection, ProtocolError>::success(
      {rejected_sequence.value(),
       position.value(),
       velocity.value(),
       yaw.value(),
       pitch.value(),
       flags.value(),
       reason.value()});
}

} // namespace arv::protocol
