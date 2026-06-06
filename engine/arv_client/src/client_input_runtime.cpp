#include "client_input_runtime.hpp"

#include "client_entity_mirror.hpp"
#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

#include "arv/entity/components.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace arv::client::detail {
namespace {

[[nodiscard]] bool command_chat_shortcut_requested(const InputFrame& input) noexcept {
  return !input.text_input.empty() && input.text_input.front() == '/';
}

[[nodiscard]] protocol::PlayerActionFlags action_flags_from_command(
    const player::PlayerCommand& command) noexcept {
  protocol::PlayerActionFlags flags = protocol::PlayerActionFlags::None;
  if (command.primary_pressed) {
    flags |= protocol::PlayerActionFlags::PrimaryPressed;
  }
  if (command.primary_held) {
    flags |= protocol::PlayerActionFlags::PrimaryHeld;
  }
  if (command.secondary_pressed) {
    flags |= protocol::PlayerActionFlags::SecondaryPressed;
  }
  if (command.secondary_held) {
    flags |= protocol::PlayerActionFlags::SecondaryHeld;
  }
  if (command.toggle_fly_pressed) {
    flags |= protocol::PlayerActionFlags::ToggleFlyPressed;
  }
  return flags;
}

[[nodiscard]] bool has_action(protocol::PlayerActionFlags flags) noexcept {
  return flags != protocol::PlayerActionFlags::None;
}

[[nodiscard]] f32 approach(f32 value, f32 target, f32 max_delta) noexcept {
  if (value < target) {
    return std::min(value + max_delta, target);
  }
  return std::max(value - max_delta, target);
}

[[nodiscard]] f32 wrap_angle_radians(f32 radians) noexcept {
  while (radians > PiRadians) {
    radians -= TauRadians;
  }
  while (radians < -PiRadians) {
    radians += TauRadians;
  }
  return radians;
}

[[nodiscard]] f32 approach_angle(f32 value, f32 target, f32 max_delta) noexcept {
  const f32 delta = wrap_angle_radians(target - value);
  if (std::abs(delta) <= max_delta) {
    return target;
  }
  return wrap_angle_radians(value + (delta < 0.0F ? -max_delta : max_delta));
}

[[nodiscard]] f32 yaw_from_rotation(Quatf rotation) noexcept {
  const Quatf normalized = normalize_quat(rotation);
  const f32 sin_yaw =
      2.0F * (normalized.w * normalized.y + normalized.x * normalized.z);
  const f32 cos_yaw =
      1.0F - 2.0F * (normalized.y * normalized.y + normalized.z * normalized.z);
  return std::atan2(sin_yaw, cos_yaw);
}

struct VehicleMovementProfile {
  f32 max_forward_speed{4.0F};
  f32 max_reverse_speed{4.0F};
  f32 acceleration{8.0F};
  f32 brake_force{10.0F};
  f32 turn_rate{1.5F};
  protocol::VehicleMoveFlags base_flags{protocol::VehicleMoveFlags::OnGround};
};

[[nodiscard]] VehicleMovementProfile vehicle_movement_profile(
    const ReplicatedEntityMirror& mirror,
    const entity::VehicleDefinitionRegistry* registry,
    const player::PlayerCommand& command) noexcept {
  VehicleMovementProfile profile;
  const entity::VehicleDefinition* definition =
      registry != nullptr ? registry->find(mirror.entity_type_name) : nullptr;
  if (definition == nullptr) {
    return profile;
  }

  switch (definition->physics) {
    case entity::VehiclePhysicsKind::LivingMount:
      profile.max_forward_speed = command.sprint_held
                                      ? definition->living_mount.sprint_speed
                                      : definition->living_mount.walk_speed;
      profile.max_reverse_speed = definition->living_mount.walk_speed * 0.4F;
      profile.acceleration = definition->living_mount.acceleration;
      profile.brake_force = std::max(definition->living_mount.acceleration, 1.0F);
      profile.turn_rate = definition->living_mount.turn_rate;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      profile.max_forward_speed = definition->surface_water.max_speed;
      profile.max_reverse_speed = definition->surface_water.max_speed * 0.35F;
      profile.acceleration = definition->surface_water.acceleration;
      profile.brake_force = definition->surface_water.acceleration +
                            std::max(definition->surface_water.water_drag, 0.0F);
      profile.turn_rate = definition->surface_water.turn_rate;
      profile.base_flags = protocol::VehicleMoveFlags::InWater;
      break;
    case entity::VehiclePhysicsKind::GroundVehicle:
      profile.max_forward_speed = definition->ground.max_forward_speed;
      profile.max_reverse_speed = definition->ground.max_reverse_speed;
      profile.acceleration = definition->ground.acceleration;
      profile.brake_force = definition->ground.brake_force;
      profile.turn_rate = definition->ground.turn_rate;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
  }

  profile.max_forward_speed = std::max(profile.max_forward_speed, 0.0F);
  profile.max_reverse_speed = std::max(profile.max_reverse_speed, 0.0F);
  profile.acceleration = std::max(profile.acceleration, 0.0F);
  profile.brake_force = std::max(profile.brake_force, profile.acceleration);
  profile.turn_rate = std::max(profile.turn_rate, 0.0F);
  return profile;
}

} // namespace

void ClientInputRuntime::submit_input(
    InputFrame input,
    ClientUiState& ui,
    LocalPlayerController& player_controller,
    ClientProtocolSender& sender,
    net::ConnectionState connection_state) {
  ui.set_tab_menu_visible(input.tab_menu_held);
  const bool was_chat_open = ui.chat_open();

  if (input.text_cancel_pressed && (ui.inventory_open() || ui.chat_open())) {
    ui.close_ui();
  } else if (!ui.chat_open() && input.open_inventory_pressed) {
    ui.toggle_inventory_if_allowed(player_controller.resolved_permissions());
  }
  if (!was_chat_open && input.open_chat_pressed) {
    ui.open_chat();
  } else if (!was_chat_open && command_chat_shortcut_requested(input)) {
    ui.open_chat_with_draft("/");
  }

  std::optional<std::string> chat_submission = ui.handle_chat_input(input, was_chat_open);
  if (chat_submission.has_value() && connection_state == net::ConnectionState::InGame) {
    (void)sender.send_chat_message({std::move(chat_submission.value()), std::nullopt});
  }

  if (ui.inventory_open() || ui.chat_open()) {
    input.move_forward = false;
    input.move_backward = false;
    input.move_left = false;
    input.move_right = false;
    input.jump_pressed = false;
    input.jump_held = false;
    input.sneak_held = false;
    input.sprint_held = false;
    input.primary_pressed = false;
    input.primary_held = false;
    input.secondary_pressed = false;
    input.secondary_held = false;
    input.hotbar_select.reset();
    input.hotbar_scroll_steps = 0;
    input.mouse_delta_x = 0.0F;
    input.mouse_delta_y = 0.0F;
  }

  player_controller.submit_input(std::move(input));
}

void ClientInputRuntime::send_fixed_tick_input(
    TickIndex tick_index,
    LocalPlayerController& player_controller,
    ClientProtocolSender& sender,
    const ClientEntityMirrorStore& entities,
    const entity::VehicleDefinitionRegistry* vehicle_registry,
    const physics::ICollisionQuery* collision_query,
    Seconds tick_seconds) {
  const player::PlayerCommand command = player_controller.consume_command(tick_index);
  if (player_controller.mounted_vehicle_id().has_value()) {
    if (command.sneak_held) {
      if (!dismount_request_sent_) {
        (void)sender.send_dismount_request(
            {player_controller.mounted_vehicle_id().value()});
        dismount_request_sent_ = true;
      }
      predicted_vehicle_id_.reset();
      return;
    }
    dismount_request_sent_ = false;
    if (!player_controller.mounted_controls_vehicle()) {
      predicted_vehicle_id_.reset();
      return;
    }
    const EntityNetID vehicle_id = player_controller.mounted_vehicle_id().value();
    const ReplicatedEntityMirror* mirror = entities.find(vehicle_id);
    if (mirror == nullptr) {
      predicted_vehicle_id_.reset();
      return;
    }
    if (!predicted_vehicle_id_.has_value() ||
        predicted_vehicle_id_.value() != vehicle_id ||
        distance(predicted_vehicle_position_, mirror->position) > 2.0F) {
      predicted_vehicle_id_ = vehicle_id;
      predicted_vehicle_position_ = mirror->position;
      predicted_vehicle_velocity_ = mirror->velocity;
      predicted_vehicle_rotation_ = mirror->rotation;
      predicted_vehicle_yaw_ = yaw_from_rotation(mirror->rotation);
    }

    movement_send_accumulator_seconds_ += tick_seconds;
    const Seconds send_interval =
        1.0 / static_cast<Seconds>(std::max(movement_send_rate_hz_, 1.0F));
    if (movement_send_accumulator_seconds_ < send_interval &&
        !command.jump_pressed && !command.secondary_pressed) {
      return;
    }
    movement_send_accumulator_seconds_ = 0.0;

    const f32 dt = std::max(tick_seconds, 1.0F / 60.0F);
    const f32 throttle = std::clamp(static_cast<f32>(command.move_z), -1.0F, 1.0F);
    const f32 steer = std::clamp(static_cast<f32>(command.move_x), -1.0F, 1.0F);
    const VehicleMovementProfile profile =
        vehicle_movement_profile(*mirror, vehicle_registry, command);
    const f32 max_speed = throttle < 0.0F
                              ? profile.max_reverse_speed
                              : profile.max_forward_speed;
    const f32 target_speed = throttle * max_speed;
    const f32 accel = command.secondary_held ? profile.brake_force : profile.acceleration;
    const f32 previous_yaw = predicted_vehicle_yaw_;
    const f32 desired_yaw =
        std::abs(steer) > 0.01F
            ? wrap_angle_radians(predicted_vehicle_yaw_ + steer * profile.turn_rate)
            : player_controller.camera().yaw_radians();
    predicted_vehicle_yaw_ = approach_angle(
        predicted_vehicle_yaw_,
        desired_yaw,
        profile.turn_rate * dt);
    const Vec3f forward = horizontal_forward(predicted_vehicle_yaw_);
    const f32 current_speed = dot(predicted_vehicle_velocity_, forward);
    const f32 next_speed = approach(current_speed, target_speed, accel * dt);
    predicted_vehicle_velocity_ = forward * next_speed;
    predicted_vehicle_position_ =
        predicted_vehicle_position_ + predicted_vehicle_velocity_ * dt;
    predicted_vehicle_rotation_ = yaw_rotation(predicted_vehicle_yaw_);
    const f32 yaw_delta = wrap_angle_radians(predicted_vehicle_yaw_ - previous_yaw);

    protocol::ServerboundVehicleMove move;
    move.vehicle_id = vehicle_id;
    move.sequence = next_vehicle_movement_sequence_++;
    move.client_tick = tick_index;
    move.dt_seconds = dt;
    move.position = predicted_vehicle_position_;
    move.velocity = predicted_vehicle_velocity_;
    move.rotation = predicted_vehicle_rotation_;
    move.angular_velocity = {0.0F, yaw_delta / dt, 0.0F};
    move.throttle = throttle;
    move.steer = steer;
    move.flags = profile.base_flags;
    if (command.jump_pressed) {
      move.flags |= protocol::VehicleMoveFlags::Jump;
    }
    if (command.secondary_held) {
      move.flags |= protocol::VehicleMoveFlags::Brake;
    }
    if (command.sprint_held) {
      move.flags |= protocol::VehicleMoveFlags::Sprint;
    }
    (void)sender.send_vehicle_move(move);
    return;
  }
  predicted_vehicle_id_.reset();
  dismount_request_sent_ = false;
  player_controller.update_local_movement(command, collision_query, tick_seconds);

  movement_send_accumulator_seconds_ += tick_seconds;
  const Seconds send_interval =
      1.0 / static_cast<Seconds>(std::max(movement_send_rate_hz_, 1.0F));
  const protocol::PlayerActionFlags action_flags = action_flags_from_command(command);
  const bool hotbar_changed =
      !sent_movement_packet_ || command.selected_hotbar_slot != last_sent_hotbar_slot_;
  if (movement_send_accumulator_seconds_ < send_interval && !has_action(action_flags) &&
      !hotbar_changed) {
    return;
  }
  movement_send_accumulator_seconds_ = 0.0;
  const protocol::ServerboundPlayerMove move =
      player_controller.build_movement_packet(
          next_movement_sequence_++,
          tick_index,
          action_flags);
  (void)sender.send_player_move(move);
  last_sent_hotbar_slot_ = command.selected_hotbar_slot;
  sent_movement_packet_ = true;
}

void ClientInputRuntime::set_movement_send_rate(f32 send_rate_hz) noexcept {
  movement_send_rate_hz_ = std::max(send_rate_hz, 1.0F);
}

} // namespace arv::client::detail
