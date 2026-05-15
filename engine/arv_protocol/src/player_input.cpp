#include "arv/protocol/player_input.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {
namespace {

enum class ResolvedCorePermissionFlags : u16 {
  None = 0U,
  CanTakeDamage = 1U << 0U,
  CanModifyWorld = 1U << 1U,
  CanInteract = 1U << 2U,
  CanOpenInventory = 1U << 3U,
  HasCreativeInventory = 1U << 4U,
  CanFly = 1U << 5U,
  HasCollision = 1U << 6U,
  IsVisibleToOthers = 1U << 7U,
};

[[nodiscard]] constexpr ResolvedCorePermissionFlags operator|(
    ResolvedCorePermissionFlags lhs,
    ResolvedCorePermissionFlags rhs) noexcept {
  return static_cast<ResolvedCorePermissionFlags>(
      static_cast<u16>(lhs) | static_cast<u16>(rhs));
}

constexpr ResolvedCorePermissionFlags& operator|=(
    ResolvedCorePermissionFlags& lhs,
    ResolvedCorePermissionFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool has_core_permission_flag(
    ResolvedCorePermissionFlags flags,
    ResolvedCorePermissionFlags flag) noexcept {
  return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0U;
}

[[nodiscard]] ResolvedCorePermissionFlags core_permission_flags_from(
    player::ResolvedCorePermissions core) noexcept {
  ResolvedCorePermissionFlags flags = ResolvedCorePermissionFlags::None;
  if (core.can_take_damage) {
    flags |= ResolvedCorePermissionFlags::CanTakeDamage;
  }
  if (core.can_modify_world) {
    flags |= ResolvedCorePermissionFlags::CanModifyWorld;
  }
  if (core.can_interact) {
    flags |= ResolvedCorePermissionFlags::CanInteract;
  }
  if (core.can_open_inventory) {
    flags |= ResolvedCorePermissionFlags::CanOpenInventory;
  }
  if (core.has_creative_inventory) {
    flags |= ResolvedCorePermissionFlags::HasCreativeInventory;
  }
  if (core.can_fly) {
    flags |= ResolvedCorePermissionFlags::CanFly;
  }
  if (core.has_collision) {
    flags |= ResolvedCorePermissionFlags::HasCollision;
  }
  if (core.is_visible_to_others) {
    flags |= ResolvedCorePermissionFlags::IsVisibleToOthers;
  }
  return flags;
}

[[nodiscard]] player::ResolvedCorePermissions core_permissions_from_flags(
    ResolvedCorePermissionFlags flags) noexcept {
  return {
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::CanTakeDamage),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::CanModifyWorld),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::CanInteract),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::CanOpenInventory),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::HasCreativeInventory),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::CanFly),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::HasCollision),
      has_core_permission_flag(flags, ResolvedCorePermissionFlags::IsVisibleToOthers),
  };
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
  auto trailing = require_no_trailing_bytes(reader, "player message");
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
  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ClientboundSetMovementHz, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundSetMovementHz, ProtocolError>::success({movement_hz.value()});
}

std::vector<u8> serialize_clientbound_player_permissions(
    const ClientboundPlayerPermissions& permissions) {
  MessageWriter writer;
  writer.write_string(permissions.game_mode_id);
  writer.write<ResolvedCorePermissionFlags>(
      core_permission_flags_from(permissions.core));
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerPermissions, ProtocolError> deserialize_clientbound_player_permissions(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto game_mode_id = reader.read_string();
  auto core_flags = reader.read<ResolvedCorePermissionFlags>();
  if (!game_mode_id || !core_flags) {
    return Expected<ClientboundPlayerPermissions, ProtocolError>::failure(
        ProtocolError{"failed to decode player permissions"});
  }
  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ClientboundPlayerPermissions, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPlayerPermissions, ProtocolError>::success(
      {std::move(game_mode_id.value()), core_permissions_from_flags(core_flags.value())});
}

std::vector<u8> serialize_clientbound_player_movement_tuning(
    const ClientboundPlayerMovementTuning& tuning) {
  MessageWriter writer;
  write_f32(writer, tuning.fly_speed);
  write_f32(writer, tuning.fly_vertical_speed);
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerMovementTuning, ProtocolError>
deserialize_clientbound_player_movement_tuning(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto fly_speed = read_f32(reader);
  auto fly_vertical_speed = read_f32(reader);
  if (!fly_speed || !fly_vertical_speed) {
    return Expected<ClientboundPlayerMovementTuning, ProtocolError>::failure(
        ProtocolError{"failed to decode player movement tuning"});
  }
  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ClientboundPlayerMovementTuning, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPlayerMovementTuning, ProtocolError>::success(
      {fly_speed.value(), fly_vertical_speed.value()});
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

  auto trailing = require_no_trailing_bytes(reader, "player message");
  if (!trailing) {
    return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>::success(progress);
}

} // namespace arv::protocol
