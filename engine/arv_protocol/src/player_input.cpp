#include "arv/protocol/player_input.hpp"

#include <bit>
#include <utility>

namespace arv::protocol {
namespace {

void write_f32(MessageWriter& writer, f32 value) {
  writer.write<u32>(std::bit_cast<u32>(value));
}

[[nodiscard]] u16 pack_core_permissions(
    const player::ResolvedCorePermissions& permissions) noexcept {
  u16 flags = 0;
  auto set = [&flags](bool value, u16 bit) {
    if (value) {
      flags = static_cast<u16>(flags | bit);
    }
  };
  set(permissions.can_take_damage, 1U << 0U);
  set(permissions.can_modify_world, 1U << 1U);
  set(permissions.can_interact, 1U << 2U);
  set(permissions.can_open_inventory, 1U << 3U);
  set(permissions.has_creative_inventory, 1U << 4U);
  set(permissions.can_fly, 1U << 5U);
  set(permissions.has_collision, 1U << 6U);
  set(permissions.is_visible_to_others, 1U << 7U);
  return flags;
}

[[nodiscard]] player::ResolvedCorePermissions unpack_core_permissions(u16 flags) noexcept {
  return {
      (flags & (1U << 0U)) != 0U,
      (flags & (1U << 1U)) != 0U,
      (flags & (1U << 2U)) != 0U,
      (flags & (1U << 3U)) != 0U,
      (flags & (1U << 4U)) != 0U,
      (flags & (1U << 5U)) != 0U,
      (flags & (1U << 6U)) != 0U,
      (flags & (1U << 7U)) != 0U,
  };
}

Expected<f32, ProtocolError> read_f32(MessageReader& reader) {
  auto value = reader.read<u32>();
  if (!value) {
    return Expected<f32, ProtocolError>::failure(value.error());
  }
  return Expected<f32, ProtocolError>::success(std::bit_cast<f32>(value.value()));
}

} // namespace

std::vector<u8> serialize_client_input_batch(const ClientInputBatch& batch) {
  MessageWriter writer;
  writer.write<u64>(batch.first_input_tick);
  writer.write<u16>(static_cast<u16>(batch.inputs.size()));
  for (const InputSample& input : batch.inputs) {
    writer.write<ClientInputButtons>(input.buttons);
    writer.write<i16>(input.move_x);
    writer.write<i16>(input.move_z);
    writer.write<i16>(input.look_delta_x);
    writer.write<i16>(input.look_delta_y);
    writer.write<u8>(input.selected_hotbar_slot);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientInputBatch, ProtocolError> deserialize_client_input_batch(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto first_tick = reader.read<u64>();
  auto input_count = reader.read<u16>();
  if (!first_tick || !input_count) {
    return Expected<ClientInputBatch, ProtocolError>::failure(ProtocolError{"failed to decode input batch header"});
  }

  ClientInputBatch batch;
  batch.first_input_tick = first_tick.value();
  batch.inputs.reserve(input_count.value());
  for (u16 index = 0; index < input_count.value(); ++index) {
    auto buttons = reader.read<ClientInputButtons>();
    auto move_x = reader.read<i16>();
    auto move_z = reader.read<i16>();
    auto look_delta_x = reader.read<i16>();
    auto look_delta_y = reader.read<i16>();
    auto slot = reader.read<u8>();
    if (!buttons || !move_x || !move_z || !look_delta_x || !look_delta_y || !slot) {
      return Expected<ClientInputBatch, ProtocolError>::failure(
          ProtocolError{"failed to decode input sample"});
    }
    batch.inputs.emplace_back(
        buttons.value(),
        move_x.value(),
        move_z.value(),
        look_delta_x.value(),
        look_delta_y.value(),
        slot.value());
  }

  return Expected<ClientInputBatch, ProtocolError>::success(std::move(batch));
}

std::vector<u8> serialize_player_correction(const PlayerCorrection& correction) {
  MessageWriter writer;
  writer.write<u64>(correction.server_tick);
  writer.write<u64>(correction.player_id.value);
  writer.write<u32>(correction.world_id.value);
  write_f32(writer, correction.position.x);
  write_f32(writer, correction.position.y);
  write_f32(writer, correction.position.z);
  write_f32(writer, correction.velocity.x);
  write_f32(writer, correction.velocity.y);
  write_f32(writer, correction.velocity.z);
  write_f32(writer, correction.yaw_radians);
  write_f32(writer, correction.pitch_radians);
  writer.write<player::MovementMode>(correction.movement_mode);
  writer.write_string(correction.game_mode_id);
  writer.write<u16>(pack_core_permissions(correction.resolved_core_permissions));
  writer.write_bool(correction.grounded);
  writer.write_bool(correction.sneaking);
  writer.write_bool(correction.swimming);
  write_f32(writer, correction.breath_seconds);
  writer.write<u8>(correction.selected_hotbar_slot);
  return std::move(writer).take_bytes();
}

Expected<PlayerCorrection, ProtocolError> deserialize_player_correction(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto server_tick = reader.read<u64>();
  auto player_id = reader.read<u64>();
  auto world_id = reader.read<u32>();
  auto pos_x = read_f32(reader);
  auto pos_y = read_f32(reader);
  auto pos_z = read_f32(reader);
  auto vel_x = read_f32(reader);
  auto vel_y = read_f32(reader);
  auto vel_z = read_f32(reader);
  auto yaw = read_f32(reader);
  auto pitch = read_f32(reader);
  auto movement_mode = reader.read<player::MovementMode>();
  auto game_mode_id = reader.read_string();
  auto resolved_core_permissions = reader.read<u16>();
  auto grounded = reader.read_bool();
  auto sneaking = reader.read_bool();
  auto swimming = reader.read_bool();
  auto breath_seconds = read_f32(reader);
  auto selected_hotbar_slot = reader.read<u8>();

  if (!server_tick || !player_id || !world_id || !pos_x || !pos_y || !pos_z || !vel_x ||
      !vel_y || !vel_z || !yaw || !pitch || !movement_mode || !game_mode_id ||
      !resolved_core_permissions || !grounded || !sneaking || !swimming ||
      !breath_seconds || !selected_hotbar_slot) {
    return Expected<PlayerCorrection, ProtocolError>::failure(
        ProtocolError{"failed to decode player correction"});
  }

  PlayerCorrection correction;
  correction.server_tick = server_tick.value();
  correction.player_id = PlayerID{player_id.value()};
  correction.world_id = WorldID{world_id.value()};
  correction.position = {pos_x.value(), pos_y.value(), pos_z.value()};
  correction.velocity = {vel_x.value(), vel_y.value(), vel_z.value()};
  correction.yaw_radians = yaw.value();
  correction.pitch_radians = pitch.value();
  correction.movement_mode = movement_mode.value();
  correction.game_mode_id = std::move(game_mode_id.value());
  correction.resolved_core_permissions = unpack_core_permissions(resolved_core_permissions.value());
  correction.grounded = grounded.value();
  correction.sneaking = sneaking.value();
  correction.swimming = swimming.value();
  correction.breath_seconds = breath_seconds.value();
  correction.selected_hotbar_slot = selected_hotbar_slot.value();
  return Expected<PlayerCorrection, ProtocolError>::success(std::move(correction));
}

player::PlayerCommand input_sample_to_command(
    const InputSample& sample,
    TickIndex input_tick) noexcept {
  player::PlayerCommand command;
  command.client_tick = input_tick;
  command.move_x = sample.move_x;
  command.move_z = sample.move_z;
  command.look_delta_x = sample.look_delta_x;
  command.look_delta_y = sample.look_delta_y;
  command.jump_pressed = has_button(sample.buttons, ClientInputButtons::JumpPressed);
  command.jump_held = has_button(sample.buttons, ClientInputButtons::JumpHeld);
  command.sneak_held = has_button(sample.buttons, ClientInputButtons::SneakHeld);
  command.sprint_held = has_button(sample.buttons, ClientInputButtons::SprintHeld);
  command.primary_pressed = has_button(sample.buttons, ClientInputButtons::PrimaryPressed);
  command.primary_held = has_button(sample.buttons, ClientInputButtons::PrimaryHeld);
  command.secondary_pressed = has_button(sample.buttons, ClientInputButtons::SecondaryPressed);
  command.secondary_held = has_button(sample.buttons, ClientInputButtons::SecondaryHeld);
  command.toggle_fly_pressed = has_button(sample.buttons, ClientInputButtons::ToggleFlyPressed);
  command.selected_hotbar_slot = sample.selected_hotbar_slot;
  return command;
}

InputSample input_sample_from_command(const player::PlayerCommand& command) noexcept {
  ClientInputButtons buttons = ClientInputButtons::None;
  if (command.jump_pressed) {
    buttons = buttons | ClientInputButtons::JumpPressed;
  }
  if (command.jump_held) {
    buttons = buttons | ClientInputButtons::JumpHeld;
  }
  if (command.sneak_held) {
    buttons = buttons | ClientInputButtons::SneakHeld;
  }
  if (command.sprint_held) {
    buttons = buttons | ClientInputButtons::SprintHeld;
  }
  if (command.primary_pressed) {
    buttons = buttons | ClientInputButtons::PrimaryPressed;
  }
  if (command.primary_held) {
    buttons = buttons | ClientInputButtons::PrimaryHeld;
  }
  if (command.secondary_pressed) {
    buttons = buttons | ClientInputButtons::SecondaryPressed;
  }
  if (command.secondary_held) {
    buttons = buttons | ClientInputButtons::SecondaryHeld;
  }
  if (command.toggle_fly_pressed) {
    buttons = buttons | ClientInputButtons::ToggleFlyPressed;
  }

  return {
      buttons,
      command.move_x,
      command.move_z,
      command.look_delta_x,
      command.look_delta_y,
      command.selected_hotbar_slot};
}

} // namespace arv::protocol
