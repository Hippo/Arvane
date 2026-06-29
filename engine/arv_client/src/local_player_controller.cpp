#include "arv/client/local_player_controller.hpp"

#include <algorithm>
#include <cmath>

namespace arv::client {
namespace {

inline constexpr f32 KnockbackSuppressedInputBlend = 0.25F;
inline constexpr Seconds FlyToggleDoubleTapSeconds = 0.35;

[[nodiscard]] u8 wrap_hotbar_slot(i32 slot) noexcept {
  constexpr i32 Count = player::DefaultHotbarSlotCount;
  const i32 wrapped = ((slot % Count) + Count) % Count;
  return static_cast<u8>(wrapped);
}

[[nodiscard]] bool sprint_fov_active(
    bool sprint_held,
    bool move_forward,
    bool move_backward,
    bool sneak_held) noexcept {
  return sprint_held && move_forward && !move_backward && !sneak_held;
}

[[nodiscard]] physics::PlayerCollisionPose collision_pose_from_state(
    const physics::PlayerKinematicState& state) noexcept {
  if (state.swimming) {
    return physics::PlayerCollisionPose::Swimming;
  }
  return state.sneaking ? physics::PlayerCollisionPose::Sneaking
                        : physics::PlayerCollisionPose::Standing;
}

} // namespace

void LocalPlayerController::set_player_tuning(player::PlayerTuning tuning) noexcept {
  tuning_ = tuning;
  current_eye_height_ = std::clamp(
      current_eye_height_,
      0.0F,
      std::max(tuning_.standing_eye_height, tuning_.sneaking_eye_height));
  target_eye_height_ = tuning_.standing_eye_height;
}

void LocalPlayerController::submit_input(InputFrame input) noexcept {
  move_forward_ = input.move_forward;
  move_backward_ = input.move_backward;
  move_left_ = input.move_left;
  move_right_ = input.move_right;
  jump_held_ = input.jump_held;
  sneak_held_ = input.sneak_held;
  sprint_held_ = input.sprint_held;
  vehicle_roll_left_held_ = input.vehicle_roll_left_held;
  vehicle_roll_right_held_ = input.vehicle_roll_right_held;
  primary_held_ = input.primary_held;
  secondary_held_ = input.secondary_held;
  jump_pressed_ = jump_pressed_ || input.jump_pressed;
  mount_pressed_ = mount_pressed_ || input.mount_pressed;
  primary_pressed_ = primary_pressed_ || input.primary_pressed;
  secondary_pressed_ = secondary_pressed_ || input.secondary_pressed;
  toggle_fly_pressed_ = toggle_fly_pressed_ || input.toggle_fly_pressed;

  if (input.hotbar_select.has_value() &&
      player::is_valid_hotbar_slot(input.hotbar_select.value())) {
    selected_hotbar_slot_ = input.hotbar_select.value();
  }
  if (input.hotbar_scroll_steps != 0) {
    selected_hotbar_slot_ = wrap_hotbar_slot(
        static_cast<i32>(selected_hotbar_slot_) -
        static_cast<i32>(input.hotbar_scroll_steps));
  }

  look_delta_x_ += input.mouse_delta_x;
  look_delta_y_ += input.mouse_delta_y;
  camera_.apply_mouse_delta(
      input.mouse_delta_x * tuning_.mouse_sensitivity_radians,
      -input.mouse_delta_y * tuning_.mouse_sensitivity_radians);
  camera_.set_fov_y_degrees(
      DefaultCameraFovYDegrees +
      (sprint_fov_active(sprint_held_, move_forward_, move_backward_, sneak_held_)
           ? DefaultSprintCameraFovBoostDegrees
           : 0.0F));
}

player::PlayerCommand LocalPlayerController::consume_command(TickIndex tick_index) noexcept {
  update_eye_height();
  update_camera_transform();

  player::PlayerCommand command;
  command.client_tick = tick_index;
  command.move_x = static_cast<i16>((move_right_ ? 1 : 0) - (move_left_ ? 1 : 0));
  command.move_z = static_cast<i16>((move_forward_ ? 1 : 0) - (move_backward_ ? 1 : 0));
  command.look_delta_x = static_cast<i16>(std::clamp(look_delta_x_, -32768.0F, 32767.0F));
  command.look_delta_y = static_cast<i16>(std::clamp(look_delta_y_, -32768.0F, 32767.0F));
  command.jump_pressed = jump_pressed_;
  command.jump_held = jump_held_;
  command.sneak_held = sneak_held_;
  command.sprint_held = sprint_held_;
  command.mount_pressed = mount_pressed_;
  command.vehicle_roll_left_held = vehicle_roll_left_held_;
  command.vehicle_roll_right_held = vehicle_roll_right_held_;
  command.primary_pressed = primary_pressed_;
  command.primary_held = primary_held_;
  command.secondary_pressed = secondary_pressed_;
  command.secondary_held = secondary_held_;
  command.toggle_fly_pressed =
      toggle_fly_pressed_ || consume_fly_toggle_request(tick_index);
  command.selected_hotbar_slot = selected_hotbar_slot_;

  jump_pressed_ = false;
  mount_pressed_ = false;
  primary_pressed_ = false;
  secondary_pressed_ = false;
  toggle_fly_pressed_ = false;
  look_delta_x_ = 0.0F;
  look_delta_y_ = 0.0F;
  return command;
}

void LocalPlayerController::update_local_movement(
    const player::PlayerCommand& command,
    const physics::ICollisionQuery* collision_query,
    Seconds tick_seconds,
    bool void_space_world,
    f32 gravity_scale) {
  if (!player_id_.has_value()) {
    return;
  }

  const Vec3f incoming_velocity = movement_state_.velocity;
  const physics::ICollisionQuery* movement_collision_query =
      void_space_world ? nullptr : collision_query;
  const physics::PlayerMovementStep movement_step =
      physics::simulate_player_movement_tick(
          movement_collision_query,
          movement_state_,
          command,
          tuning_,
          movement_authority(void_space_world),
          tick_seconds,
          gravity_scale);
  movement_state_ = movement_step.state;
  apply_knockback_control_suppression(
      incoming_velocity,
      movement_collision_query,
      tick_seconds);
  body_position_ = movement_state_.position;
  update_target_eye_height();
  camera_.set_rotation(movement_state_.yaw_radians, movement_state_.pitch_radians);
  update_render_smoothing();
  update_camera_transform();
}

void LocalPlayerController::enter_world(
    WorldID world_id,
    PlayerID player_id,
    EntityNetID local_entity_id,
    Vec3f body_position,
    TickIndex initial_server_tick,
    TickRate tick_rate) noexcept {
  player_id_ = player_id;
  local_entity_id_ = local_entity_id;
  mounted_vehicle_id_.reset();
  mounted_seat_id_.clear();
  mounted_controls_vehicle_ = false;
  body_position_ = body_position;
  render_position_ = body_position;
  movement_state_ = {
      body_position,
      {},
      camera_.yaw_radians(),
      camera_.pitch_radians(),
      false,
      false,
      false,
      tuning_.breath_seconds,
      player::MovementMode::Walking};
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  world_initial_tick_ = initial_server_tick;
  server_tick_rate_ = tick_rate == 0U ? DefaultBaseTps : tick_rate;
  last_jump_press_tick_.reset();
  camera_.set_world_id(world_id);
  update_camera_transform();
}

void LocalPlayerController::leave_world() noexcept {
  player_id_.reset();
  local_entity_id_.reset();
  mounted_vehicle_id_.reset();
  mounted_seat_id_.clear();
  mounted_controls_vehicle_ = false;
  movement_state_ = {};
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  last_jump_press_tick_.reset();
  camera_.set_world_id(WorldID{});
}

void LocalPlayerController::apply_player_velocity(
    const protocol::ClientboundPlayerVelocity& velocity) noexcept {
  if (player_id_.has_value() && velocity.player_id != player_id_.value()) {
    return;
  }
  movement_state_.velocity = velocity.velocity;
  knockback_control_suppression_ticks_ = std::max(
      knockback_control_suppression_ticks_,
      velocity.knockback_control_suppression_ticks);
  if (movement_state_.velocity.y > 0.0F) {
    movement_state_.grounded = false;
  }
}

void LocalPlayerController::apply_player_permissions(
    const protocol::ClientboundPlayerPermissions& permissions) {
  game_mode_id_ = permissions.game_mode_id;
  resolved_permissions_.core = permissions.core;
  resolved_permissions_.custom.clear();
  if (!resolved_permissions_.core.can_fly) {
    last_jump_press_tick_.reset();
    if (movement_state_.movement_mode == player::MovementMode::Flying) {
      movement_state_.movement_mode = player::MovementMode::Walking;
    }
  }
}

void LocalPlayerController::apply_player_movement_tuning(
    const protocol::ClientboundPlayerMovementTuning& tuning) noexcept {
  if (std::isfinite(tuning.fly_speed) && tuning.fly_speed > 0.0F) {
    tuning_.fly_speed = tuning.fly_speed;
  }
  if (std::isfinite(tuning.fly_vertical_speed) && tuning.fly_vertical_speed > 0.0F) {
    tuning_.fly_vertical_speed = tuning.fly_vertical_speed;
  }
}

void LocalPlayerController::apply_move_correction(
    const protocol::ClientboundPlayerMoveCorrection& correction) {
  movement_state_.position = correction.position;
  movement_state_.velocity = correction.velocity;
  movement_state_.yaw_radians = correction.yaw_radians;
  movement_state_.pitch_radians = correction.pitch_radians;
  movement_state_.grounded =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::OnGround);
  movement_state_.sneaking =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Sneaking);
  movement_state_.swimming =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Swimming);
  movement_state_.movement_mode =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Flying)
          ? player::MovementMode::Flying
          : player::MovementMode::Walking;
  body_position_ = movement_state_.position;
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  render_position_ = body_position_;
  knockback_control_suppression_ticks_ = 0U;
  update_target_eye_height();
  camera_.set_rotation(movement_state_.yaw_radians, movement_state_.pitch_radians);
  update_camera_transform();
}

void LocalPlayerController::apply_block_break_progress(
    const protocol::ClientboundPlayerBlockBreakProgress& progress) noexcept {
  if (player_id_.has_value() && progress.player_id != player_id_.value()) {
    return;
  }
  block_break_progress_ = progress.block_break;
}

void LocalPlayerController::apply_ride_started(
    const protocol::ClientboundRideStarted& event) {
  if (!local_entity_id_.has_value() || event.passenger_id != local_entity_id_.value()) {
    return;
  }
  mounted_vehicle_id_ = event.rideable_id;
  mounted_seat_id_ = event.seat_id;
  mounted_controls_vehicle_ = event.controls_vehicle;
  mount_pressed_ = false;
  movement_state_.velocity = {};
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
}

void LocalPlayerController::apply_ride_ended(
    const protocol::ClientboundRideEnded& event) {
  if (!local_entity_id_.has_value() || event.passenger_id != local_entity_id_.value()) {
    return;
  }
  mounted_vehicle_id_.reset();
  mounted_seat_id_.clear();
  mounted_controls_vehicle_ = false;
  movement_state_.position = event.position;
  movement_state_.velocity = {};
  movement_state_.grounded = false;
  body_position_ = event.position;
  render_position_ = event.position;
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  update_camera_transform();
}

void LocalPlayerController::apply_vehicle_correction(
    const protocol::ClientboundVehicleCorrection& correction) {
  if (!mounted_vehicle_id_.has_value() ||
      correction.vehicle_id != mounted_vehicle_id_.value()) {
    return;
  }
  movement_state_.velocity = correction.velocity;
  body_position_ = correction.position;
  render_position_ = correction.position;
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  update_camera_transform();
}

void LocalPlayerController::apply_mounted_vehicle_transform(
    EntityNetID rideable_id,
    Vec3f vehicle_position,
    Quatf vehicle_rotation,
    Vec3f seat_local_position,
    Quatf seat_local_rotation,
    Vec3f vehicle_velocity) noexcept {
  (void)seat_local_rotation;
  if (!mounted_vehicle_id_.has_value() || mounted_vehicle_id_.value() != rideable_id) {
    return;
  }
  const Quatf normalized_vehicle_rotation = normalize_quat(vehicle_rotation);
  const Vec3f seat_position =
      vehicle_position + rotate_vector(normalized_vehicle_rotation, seat_local_position);
  movement_state_.position = seat_position;
  movement_state_.velocity = vehicle_velocity;
  body_position_ = seat_position;
  render_position_ = seat_position;
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  update_camera_transform();
}

void LocalPlayerController::clear_mount_if_rideable_despawned(
    EntityNetID rideable_id) noexcept {
  if (!mounted_vehicle_id_.has_value() || mounted_vehicle_id_.value() != rideable_id) {
    return;
  }
  mounted_vehicle_id_.reset();
  mounted_seat_id_.clear();
  mounted_controls_vehicle_ = false;
  movement_state_.velocity = {};
  render_smoothing_offset_ = {};
  render_smoothing_ticks_remaining_ = 0;
  render_smoothing_ticks_total_ = 0;
  update_camera_transform();
}

void LocalPlayerController::apply_knockback_control_suppression(
    Vec3f incoming_velocity,
    const physics::ICollisionQuery* collision_query,
    Seconds tick_seconds) noexcept {
  if (knockback_control_suppression_ticks_ == 0U || tick_seconds <= 0.0) {
    return;
  }

  const Vec3f simulated_horizontal{
      movement_state_.velocity.x,
      0.0F,
      movement_state_.velocity.z};
  const Vec3f incoming_horizontal{
      incoming_velocity.x,
      0.0F,
      incoming_velocity.z};
  Vec3f blended_horizontal =
      incoming_horizontal * (1.0F - KnockbackSuppressedInputBlend) +
      simulated_horizontal * KnockbackSuppressedInputBlend;
  Vec3f displacement{
      (blended_horizontal.x - simulated_horizontal.x) * static_cast<f32>(tick_seconds),
      0.0F,
      (blended_horizontal.z - simulated_horizontal.z) * static_cast<f32>(tick_seconds)};

  const physics::PlayerMovementAuthority authority = movement_authority();
  if (authority.has_collision && collision_query != nullptr) {
    const physics::KinematicConfig physics_config =
        physics::kinematic_config_from_tuning(tuning_);
    const physics::MovementResolution movement = physics::resolve_movement(
        *collision_query,
        physics::make_body_bounds(
            movement_state_.position,
            physics_config,
            collision_pose_from_state(movement_state_)),
        displacement,
        physics_config.collision_epsilon);
    displacement = movement.applied_delta;
    if (movement.hit_x) {
      blended_horizontal.x = 0.0F;
    }
    if (movement.hit_z) {
      blended_horizontal.z = 0.0F;
    }
  }

  movement_state_.position = movement_state_.position + displacement;
  movement_state_.velocity.x = blended_horizontal.x;
  movement_state_.velocity.z = blended_horizontal.z;
  --knockback_control_suppression_ticks_;
}

protocol::ServerboundPlayerMove LocalPlayerController::build_movement_packet(
    u32 sequence,
    TickIndex client_tick,
    protocol::PlayerActionFlags action_flags) const noexcept {
  return {
      sequence,
      client_tick,
      movement_state_.position,
      movement_state_.velocity,
      movement_state_.yaw_radians,
      movement_state_.pitch_radians,
      movement_flags(),
      action_flags,
      selected_hotbar_slot_};
}

void LocalPlayerController::update_eye_height() noexcept {
  const f32 eye_step = tuning_.eye_height_lerp_per_second /
                       static_cast<f32>(std::max<TickRate>(server_tick_rate_, 1U));
  if (current_eye_height_ < target_eye_height_) {
    current_eye_height_ = std::min(current_eye_height_ + eye_step, target_eye_height_);
  } else {
    current_eye_height_ = std::max(current_eye_height_ - eye_step, target_eye_height_);
  }
}

void LocalPlayerController::update_target_eye_height() noexcept {
  target_eye_height_ =
      movement_state_.swimming
          ? tuning_.swimming_eye_height
          : (movement_state_.sneaking ? tuning_.sneaking_eye_height
                                      : tuning_.standing_eye_height);
}

void LocalPlayerController::update_render_smoothing() noexcept {
  if (render_smoothing_ticks_remaining_ == 0U) {
    render_position_ = body_position_;
    render_smoothing_offset_ = {};
    render_smoothing_ticks_total_ = 0;
    return;
  }

  --render_smoothing_ticks_remaining_;
  if (render_smoothing_ticks_remaining_ == 0U) {
    render_position_ = body_position_;
    render_smoothing_offset_ = {};
    render_smoothing_ticks_total_ = 0;
    return;
  }

  const f32 smoothing_scale =
      static_cast<f32>(render_smoothing_ticks_remaining_) /
      static_cast<f32>(std::max<u32>(render_smoothing_ticks_total_, 1U));
  render_position_ = body_position_ + render_smoothing_offset_ * smoothing_scale;
}

void LocalPlayerController::update_camera_transform() noexcept {
  camera_.set_position(
      {render_position_.x,
       render_position_.y + current_eye_height_,
       render_position_.z});
}

bool LocalPlayerController::consume_fly_toggle_request(TickIndex tick_index) noexcept {
  if (!jump_pressed_) {
    return false;
  }
  if (!resolved_permissions_.core.can_fly) {
    last_jump_press_tick_.reset();
    return false;
  }

  const TickIndex threshold_ticks = std::max<TickIndex>(
      1U,
      static_cast<TickIndex>(
          FlyToggleDoubleTapSeconds *
          static_cast<Seconds>(std::max<TickRate>(server_tick_rate_, 1U))));
  if (last_jump_press_tick_.has_value() &&
      tick_index >= last_jump_press_tick_.value() &&
      tick_index - last_jump_press_tick_.value() <= threshold_ticks) {
    last_jump_press_tick_.reset();
    return true;
  }

  last_jump_press_tick_ = tick_index;
  return false;
}

protocol::PlayerActionFlags action_flags_from_player_command(
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

physics::PlayerMovementAuthority LocalPlayerController::movement_authority(
    bool void_space_world) const noexcept {
  return {
      resolved_permissions_.core.can_fly || void_space_world,
      void_space_world ? false : resolved_permissions_.core.has_collision};
}

protocol::PlayerMoveFlags LocalPlayerController::movement_flags() const noexcept {
  protocol::PlayerMoveFlags flags = protocol::PlayerMoveFlags::None;
  if (movement_state_.grounded) {
    flags |= protocol::PlayerMoveFlags::OnGround;
  }
  if (jump_held_) {
    flags |= protocol::PlayerMoveFlags::Jumping;
  }
  if (movement_state_.sneaking) {
    flags |= protocol::PlayerMoveFlags::Sneaking;
  }
  if (sprint_held_) {
    flags |= protocol::PlayerMoveFlags::Sprinting;
  }
  if (movement_state_.swimming) {
    flags |= protocol::PlayerMoveFlags::Swimming;
  }
  if (movement_state_.movement_mode == player::MovementMode::Flying) {
    flags |= protocol::PlayerMoveFlags::Flying;
  }
  return flags;
}

} // namespace arv::client
