#include "arv/protocol/player_input.hpp"

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

void write_vec3(MessageWriter& writer, Vec3f value) {
  write_f32(writer, value.x);
  write_f32(writer, value.y);
  write_f32(writer, value.z);
}

void write_block_pos(MessageWriter& writer, BlockPos pos) {
  writer.write<i32>(pos.value.x);
  writer.write<i32>(pos.value.y);
  writer.write<i32>(pos.value.z);
}

Expected<Vec3f, ProtocolError> read_vec3(MessageReader& reader) {
  auto x = read_f32(reader);
  auto y = read_f32(reader);
  auto z = read_f32(reader);
  if (!x || !y || !z) {
    return Expected<Vec3f, ProtocolError>::failure(ProtocolError{"failed to decode vector"});
  }
  return Expected<Vec3f, ProtocolError>::success({x.value(), y.value(), z.value()});
}

Expected<BlockPos, ProtocolError> read_block_pos(MessageReader& reader) {
  auto x = reader.read<i32>();
  auto y = reader.read<i32>();
  auto z = reader.read<i32>();
  if (!x || !y || !z) {
    return Expected<BlockPos, ProtocolError>::failure(
        ProtocolError{"failed to decode block position"});
  }
  return Expected<BlockPos, ProtocolError>::success({{x.value(), y.value(), z.value()}});
}

Expected<void, ProtocolError> require_no_trailing_bytes(MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"trailing bytes in player message"});
  }
  return Expected<void, ProtocolError>::success();
}

} // namespace

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
  auto trailing = require_no_trailing_bytes(reader);
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
  auto trailing = require_no_trailing_bytes(reader);
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

std::vector<u8> serialize_clientbound_player_velocity(
    const ClientboundPlayerVelocity& velocity) {
  MessageWriter writer;
  writer.write<u64>(velocity.server_tick);
  writer.write<u64>(velocity.player_id.value);
  write_vec3(writer, velocity.velocity);
  writer.write<u32>(velocity.knockback_control_suppression_ticks);
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerVelocity, ProtocolError> deserialize_clientbound_player_velocity(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto server_tick = reader.read<u64>();
  auto player_id = reader.read<u64>();
  auto velocity = read_vec3(reader);
  auto suppression_ticks = reader.read<u32>();
  if (!server_tick || !player_id || !velocity || !suppression_ticks) {
    return Expected<ClientboundPlayerVelocity, ProtocolError>::failure(
        ProtocolError{"failed to decode player velocity"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundPlayerVelocity, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPlayerVelocity, ProtocolError>::success(
      {server_tick.value(),
       PlayerID{player_id.value()},
       velocity.value(),
       suppression_ticks.value()});
}

std::vector<u8> serialize_clientbound_set_movement_hz(
    const ClientboundSetMovementHz& settings) {
  MessageWriter writer;
  write_f32(writer, settings.movement_hz);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSetMovementHz, ProtocolError> deserialize_clientbound_set_movement_hz(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto movement_hz = read_f32(reader);
  if (!movement_hz) {
    return Expected<ClientboundSetMovementHz, ProtocolError>::failure(
        ProtocolError{"failed to decode movement hz"});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundSetMovementHz, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundSetMovementHz, ProtocolError>::success({movement_hz.value()});
}

std::vector<u8> serialize_clientbound_player_block_break_progress(
    const ClientboundPlayerBlockBreakProgress& progress) {
  MessageWriter writer;
  writer.write<u64>(progress.server_tick);
  writer.write<u64>(progress.player_id.value);
  writer.write_bool(progress.block_break.active);
  if (progress.block_break.active) {
    write_block_pos(writer, progress.block_break.block);
    writer.write<u32>(progress.block_break.variant.value);
    write_f32(writer, progress.block_break.progress);
    writer.write<u8>(progress.block_break.stage);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>
deserialize_clientbound_player_block_break_progress(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto server_tick = reader.read<u64>();
  auto player_id = reader.read<u64>();
  auto active = reader.read_bool();
  if (!server_tick || !player_id || !active) {
    return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::failure(
        ProtocolError{"failed to decode player block break progress"});
  }

  ClientboundPlayerBlockBreakProgress progress;
  progress.server_tick = server_tick.value();
  progress.player_id = PlayerID{player_id.value()};
  progress.block_break.active = active.value();
  if (progress.block_break.active) {
    auto block = read_block_pos(reader);
    auto variant = reader.read<u32>();
    auto amount = read_f32(reader);
    auto stage = reader.read<u8>();
    if (!block || !variant || !amount || !stage) {
      return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::failure(
          ProtocolError{"failed to decode active block break progress"});
    }
    progress.block_break.block = block.value();
    progress.block_break.variant = voxel::BlockVariantID{variant.value()};
    progress.block_break.progress = amount.value();
    progress.block_break.stage = stage.value();
  }

  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::success(progress);
}

} // namespace arv::protocol
