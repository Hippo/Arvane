#include "client_input_runtime.hpp"

#include "client_entity_mirror.hpp"
#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

#include "arv/entity/components.hpp"
#include "arv/physics/collision_resolution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace arv::client::detail {
namespace {

inline constexpr f32 VehicleGravityBlocksPerSecondSquared = 32.0F;
inline constexpr f32 VehicleTerminalVelocityBlocksPerSecond = 55.0F;

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

[[nodiscard]] f32 vehicle_gameplay_yaw_from_rotation(Quatf rotation) noexcept {
  return wrap_angle_radians(yaw_from_rotation(rotation) - ModelForwardYawOffsetRadians);
}

[[nodiscard]] Quatf vehicle_render_rotation_from_gameplay_yaw(f32 yaw_radians) noexcept {
  return model_forward_yaw_rotation(yaw_radians);
}

[[nodiscard]] Vec3f model_visual_forward(Quatf rotation) noexcept {
  return rotate_vector(normalize_quat(rotation), {0.0F, 0.0F, -1.0F});
}

struct VehicleMovementProfile {
  entity::VehiclePhysicsKind physics{entity::VehiclePhysicsKind::LivingMount};
  f32 max_forward_speed{4.0F};
  f32 max_reverse_speed{4.0F};
  f32 acceleration{8.0F};
  f32 brake_force{10.0F};
  f32 turn_rate{1.5F};
  f32 drag{0.9F};
  f32 lateral_grip{1.0F};
  f32 gravity_scale{1.0F};
  f32 step_height{};
  Vec3f collision_size{1.0F, 1.0F, 1.0F};
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

  profile.physics = definition->physics;
  profile.collision_size = definition->collision_size;
  switch (definition->physics) {
    case entity::VehiclePhysicsKind::LivingMount:
      profile.max_forward_speed = command.sprint_held
                                      ? definition->living_mount.sprint_speed
                                      : definition->living_mount.walk_speed;
      profile.max_reverse_speed = definition->living_mount.walk_speed * 0.4F;
      profile.acceleration = definition->living_mount.acceleration;
      profile.brake_force = std::max(definition->living_mount.acceleration, 1.0F);
      profile.turn_rate = definition->living_mount.turn_rate;
      profile.drag = definition->living_mount.friction;
      profile.gravity_scale = definition->living_mount.gravity_scale;
      profile.step_height = definition->living_mount.step_height;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      profile.max_forward_speed = definition->surface_water.max_speed;
      profile.max_reverse_speed = definition->surface_water.max_speed * 0.35F;
      profile.acceleration = definition->surface_water.acceleration;
      profile.brake_force = definition->surface_water.acceleration +
                            std::max(definition->surface_water.water_drag, 0.0F);
      profile.turn_rate = definition->surface_water.turn_rate;
      profile.drag = definition->surface_water.water_drag;
      profile.base_flags = protocol::VehicleMoveFlags::InWater;
      break;
    case entity::VehiclePhysicsKind::GroundVehicle:
      profile.max_forward_speed = definition->ground.max_forward_speed;
      profile.max_reverse_speed = definition->ground.max_reverse_speed;
      profile.acceleration = definition->ground.acceleration;
      profile.brake_force = definition->ground.brake_force;
      profile.turn_rate = definition->ground.turn_rate;
      profile.drag = definition->ground.drag;
      profile.lateral_grip = definition->ground.lateral_grip;
      profile.gravity_scale = definition->ground.gravity_scale;
      profile.step_height = definition->ground.max_step_height;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
  }

  profile.max_forward_speed = std::max(profile.max_forward_speed, 0.0F);
  profile.max_reverse_speed = std::max(profile.max_reverse_speed, 0.0F);
  profile.acceleration = std::max(profile.acceleration, 0.0F);
  profile.brake_force = std::max(profile.brake_force, profile.acceleration);
  profile.turn_rate = std::max(profile.turn_rate, 0.0F);
  profile.drag = std::clamp(profile.drag, 0.0F, 1.0F);
  profile.lateral_grip = std::clamp(profile.lateral_grip, 0.0F, 1.0F);
  profile.gravity_scale = std::max(profile.gravity_scale, 0.0F);
  profile.step_height = std::max(profile.step_height, 0.0F);
  profile.collision_size = {
      std::max(profile.collision_size.x, 0.01F),
      std::max(profile.collision_size.y, 0.01F),
      std::max(profile.collision_size.z, 0.01F)};
  return profile;
}

[[nodiscard]] std::string_view vehicle_motion_animation_name(
    const VehicleMovementProfile& profile,
    Quatf rotation,
    Vec3f velocity) noexcept {
  if (profile.physics != entity::VehiclePhysicsKind::GroundVehicle) {
    return {};
  }
  constexpr f32 AnimationSpeedEpsilonBlocksPerSecond = 0.05F;
  const f32 signed_speed = dot(velocity, model_visual_forward(rotation));
  if (signed_speed > AnimationSpeedEpsilonBlocksPerSecond) {
    return "driving";
  }
  if (signed_speed < -AnimationSpeedEpsilonBlocksPerSecond) {
    return "reverse";
  }
  return {};
}

[[nodiscard]] Aabb3f vehicle_bounds(Vec3f position, Vec3f size) noexcept {
  const Vec3f half{size.x * 0.5F, 0.0F, size.z * 0.5F};
  return {
      {position.x - half.x, position.y, position.z - half.z},
      {position.x + half.x, position.y + size.y, position.z + half.z}};
}

[[nodiscard]] bool vehicle_has_ground_support(
    const physics::ICollisionQuery& collision_query,
    Vec3f position,
    Vec3f size) noexcept {
  return physics::has_support_below(
      collision_query,
      vehicle_bounds(position, size),
      std::max(physics::CollisionEpsilon * 2.0F, 0.05F));
}

[[nodiscard]] const entity::VehicleSeatDefinition* find_vehicle_seat(
    const entity::VehicleDefinition* definition,
    std::string_view seat_id) noexcept {
  if (definition == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(
      definition->seats.begin(),
      definition->seats.end(),
      [&](const entity::VehicleSeatDefinition& seat) {
        return seat.id == seat_id;
      });
  return found == definition->seats.end() ? nullptr : &*found;
}

[[nodiscard]] bool should_reset_vehicle_prediction(
    EntityNetID vehicle_id,
    const ReplicatedEntityMirror& mirror,
    const std::optional<EntityNetID>& predicted_vehicle_id,
    Vec3f predicted_position,
    Vec3f predicted_velocity) noexcept {
  if (!predicted_vehicle_id.has_value() || predicted_vehicle_id.value() != vehicle_id) {
    return true;
  }
  if (distance(predicted_position, mirror.position) > 2.0F) {
    return true;
  }
  return distance(predicted_velocity, mirror.velocity) > 6.0F;
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
    ClientEntityMirrorStore& entities,
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
    const entity::VehicleDefinition* definition =
        vehicle_registry == nullptr ? nullptr : vehicle_registry->find(mirror->entity_type_name);
    const VehicleMovementProfile profile =
        vehicle_movement_profile(*mirror, vehicle_registry, command);
    if (should_reset_vehicle_prediction(
            vehicle_id,
            *mirror,
            predicted_vehicle_id_,
            predicted_vehicle_position_,
            predicted_vehicle_velocity_)) {
      predicted_vehicle_id_ = vehicle_id;
      predicted_vehicle_position_ = mirror->position;
      predicted_vehicle_velocity_ = mirror->velocity;
      predicted_vehicle_rotation_ = mirror->rotation;
      predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(mirror->rotation);
      predicted_vehicle_animation_name_.clear();
      predicted_vehicle_animation_time_ = 0.0;
    }

    movement_send_accumulator_seconds_ += tick_seconds;
    const Seconds send_interval =
        1.0 / static_cast<Seconds>(std::max(movement_send_rate_hz_, 1.0F));
    const bool send_this_tick =
        movement_send_accumulator_seconds_ >= send_interval ||
        command.jump_pressed || command.secondary_pressed;

    const f32 dt = std::clamp(static_cast<f32>(tick_seconds), 1.0F / 240.0F, 0.1F);
    const f32 throttle = std::clamp(static_cast<f32>(command.move_z), -1.0F, 1.0F);
    const f32 steer = std::clamp(static_cast<f32>(command.move_x), -1.0F, 1.0F);
    const f32 max_speed = throttle < 0.0F
                              ? profile.max_reverse_speed
                              : profile.max_forward_speed;
    const f32 target_speed = throttle * max_speed;
    const f32 previous_yaw = predicted_vehicle_yaw_;
    const bool ground_vehicle =
        profile.physics == entity::VehiclePhysicsKind::GroundVehicle;
    bool vehicle_grounded =
        protocol::has_vehicle_move_flag(profile.base_flags, protocol::VehicleMoveFlags::OnGround);
    if (ground_vehicle && collision_query != nullptr) {
      vehicle_grounded = vehicle_has_ground_support(
          *collision_query,
          predicted_vehicle_position_,
          profile.collision_size);
    }
    if (ground_vehicle) {
      const f32 speed_fraction =
          profile.max_forward_speed <= 0.0F
              ? 0.0F
              : std::clamp(
                    horizontal_length(predicted_vehicle_velocity_) /
                        profile.max_forward_speed,
                    0.0F,
                    1.0F);
      const f32 traction = vehicle_grounded ? 1.0F : 0.1F;
      const f32 steering_response =
          std::abs(throttle) > 0.01F || speed_fraction > 0.05F
              ? std::max(speed_fraction, 0.25F) * traction
              : 0.0F;
      predicted_vehicle_yaw_ = wrap_angle_radians(
          predicted_vehicle_yaw_ + steer * profile.turn_rate * steering_response * dt);
    } else {
      const f32 desired_yaw =
          std::abs(steer) > 0.01F
              ? wrap_angle_radians(predicted_vehicle_yaw_ + steer * profile.turn_rate)
              : player_controller.camera().yaw_radians();
      predicted_vehicle_yaw_ = approach_angle(
          predicted_vehicle_yaw_,
          desired_yaw,
          profile.turn_rate * dt);
    }
    const Vec3f forward = horizontal_forward(predicted_vehicle_yaw_);
    const f32 current_speed = dot(predicted_vehicle_velocity_, forward);
    f32 next_speed = current_speed;
    const bool has_ground_traction = !ground_vehicle || vehicle_grounded;
    if (command.secondary_held && has_ground_traction) {
      next_speed = approach(current_speed, 0.0F, profile.brake_force * dt);
    } else if (std::abs(throttle) > 0.01F && has_ground_traction) {
      const f32 accel =
          (current_speed * target_speed < 0.0F) ? profile.brake_force : profile.acceleration;
      next_speed = approach(current_speed, target_speed, accel * dt);
    } else {
      const f32 drag_factor =
          std::pow(profile.drag, std::max(dt, 0.0F) * 60.0F);
      next_speed = current_speed * drag_factor;
      if (std::abs(next_speed) < 0.02F) {
        next_speed = 0.0F;
      }
    }
    f32 next_vertical_speed = predicted_vehicle_velocity_.y;
    if (ground_vehicle) {
      if (vehicle_grounded) {
        next_vertical_speed = std::max(next_vertical_speed, 0.0F);
      } else {
        next_vertical_speed = std::max(
            next_vertical_speed -
                VehicleGravityBlocksPerSecondSquared * profile.gravity_scale * dt,
            -VehicleTerminalVelocityBlocksPerSecond);
      }
    }
    predicted_vehicle_velocity_ = forward * next_speed;
    predicted_vehicle_velocity_.y = next_vertical_speed;
    const Vec3f attempted_delta = predicted_vehicle_velocity_ * dt;
    if (collision_query != nullptr) {
      const Aabb3f bounds = vehicle_bounds(predicted_vehicle_position_, profile.collision_size);
      const physics::MovementResolution resolved =
          physics::resolve_movement(*collision_query, bounds, attempted_delta);
      predicted_vehicle_position_ =
          predicted_vehicle_position_ + resolved.applied_delta;
      if (resolved.hit_x) {
        predicted_vehicle_velocity_.x = 0.0F;
      }
      if (resolved.hit_y) {
        predicted_vehicle_velocity_.y = 0.0F;
      }
      if (resolved.hit_z) {
        predicted_vehicle_velocity_.z = 0.0F;
      }
      if (ground_vehicle) {
        vehicle_grounded = resolved.grounded;
      }
    } else {
      predicted_vehicle_position_ =
          predicted_vehicle_position_ + attempted_delta;
    }
    predicted_vehicle_rotation_ =
        vehicle_render_rotation_from_gameplay_yaw(predicted_vehicle_yaw_);
    const f32 yaw_delta = wrap_angle_radians(predicted_vehicle_yaw_ - previous_yaw);

    const std::string_view animation_name =
        vehicle_motion_animation_name(
            profile,
            predicted_vehicle_rotation_,
            predicted_vehicle_velocity_);
    if (animation_name.empty()) {
      predicted_vehicle_animation_name_.clear();
      predicted_vehicle_animation_time_ = 0.0;
    } else if (predicted_vehicle_animation_name_ != animation_name) {
      predicted_vehicle_animation_name_ = std::string{animation_name};
      predicted_vehicle_animation_time_ = 0.0;
    } else {
      predicted_vehicle_animation_time_ += dt;
    }
    std::array<protocol::EntityAnimationPlayback, 1> active_animations{};
    std::span<const protocol::EntityAnimationPlayback> active_animation_span{};
    if (!predicted_vehicle_animation_name_.empty()) {
      active_animations[0] = {
          predicted_vehicle_animation_name_,
          predicted_vehicle_animation_time_};
      active_animation_span = std::span<const protocol::EntityAnimationPlayback>{
          active_animations.data(),
          1U};
    }

    const entity::VehicleSeatDefinition* seat =
        find_vehicle_seat(definition, player_controller.mounted_seat_id());
    player_controller.apply_mounted_vehicle_transform(
        vehicle_id,
        predicted_vehicle_position_,
        predicted_vehicle_rotation_,
        seat == nullptr ? Vec3f{} : seat->local_position,
        seat == nullptr ? Quatf{} : seat->local_rotation,
        predicted_vehicle_velocity_);
    entities.apply_local_vehicle_prediction(
        vehicle_id,
        predicted_vehicle_position_,
        predicted_vehicle_velocity_,
        predicted_vehicle_rotation_,
        active_animation_span);

    if (!send_this_tick) {
      return;
    }
    movement_send_accumulator_seconds_ = 0.0;

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
    move.flags = protocol::VehicleMoveFlags::None;
    if (profile.physics == entity::VehiclePhysicsKind::SurfaceWaterVehicle) {
      move.flags |= protocol::VehicleMoveFlags::InWater;
    } else if (!ground_vehicle ||
               vehicle_grounded) {
      move.flags |= protocol::VehicleMoveFlags::OnGround;
    }
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

void ClientInputRuntime::apply_vehicle_correction(
    const protocol::ClientboundVehicleCorrection& correction) noexcept {
  predicted_vehicle_id_ = correction.vehicle_id;
  predicted_vehicle_position_ = correction.position;
  predicted_vehicle_velocity_ = correction.velocity;
  predicted_vehicle_rotation_ = normalize_quat(correction.rotation);
  predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(correction.rotation);
  predicted_vehicle_animation_name_.clear();
  predicted_vehicle_animation_time_ = 0.0;
  movement_send_accumulator_seconds_ = 0.0;
}

void ClientInputRuntime::clear_vehicle_prediction() noexcept {
  predicted_vehicle_id_.reset();
  predicted_vehicle_position_ = {};
  predicted_vehicle_velocity_ = {};
  predicted_vehicle_rotation_ = {};
  predicted_vehicle_yaw_ = 0.0F;
  predicted_vehicle_animation_name_.clear();
  predicted_vehicle_animation_time_ = 0.0;
}

} // namespace arv::client::detail
