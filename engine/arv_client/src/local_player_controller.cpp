#include "arv/client/local_player_controller.hpp"

#include <algorithm>

namespace arv::client {
namespace {

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
  primary_held_ = input.primary_held;
  secondary_held_ = input.secondary_held;
  jump_pressed_ = jump_pressed_ || input.jump_pressed;
  primary_pressed_ = primary_pressed_ || input.primary_pressed;
  secondary_pressed_ = secondary_pressed_ || input.secondary_pressed;
  toggle_fly_pressed_ = toggle_fly_pressed_ || input.toggle_fly_pressed;

  if (input.hotbar_select.has_value() && player::is_valid_hotbar_slot(input.hotbar_select.value())) {
    if (selected_hotbar_slot_ != input.hotbar_select.value()) {
      selected_hotbar_slot_ = input.hotbar_select.value();
      has_unconfirmed_hotbar_selection_ = true;
    }
  }
  if (input.hotbar_scroll_steps != 0) {
    const u8 next_slot = wrap_hotbar_slot(
        static_cast<i32>(selected_hotbar_slot_) - static_cast<i32>(input.hotbar_scroll_steps));
    if (selected_hotbar_slot_ != next_slot) {
      selected_hotbar_slot_ = next_slot;
      has_unconfirmed_hotbar_selection_ = true;
    }
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
  const f32 eye_step = tuning_.eye_height_lerp_per_second / 60.0F;
  if (current_eye_height_ < target_eye_height_) {
    current_eye_height_ = std::min(current_eye_height_ + eye_step, target_eye_height_);
  } else {
    current_eye_height_ = std::max(current_eye_height_ - eye_step, target_eye_height_);
  }
  camera_.set_position(
      {body_position_.x,
       body_position_.y + current_eye_height_,
       body_position_.z});

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
  command.primary_pressed = primary_pressed_;
  command.primary_held = primary_held_;
  command.secondary_pressed = secondary_pressed_;
  command.secondary_held = secondary_held_;
  command.toggle_fly_pressed = toggle_fly_pressed_;
  command.selected_hotbar_slot = selected_hotbar_slot_;

  jump_pressed_ = false;
  primary_pressed_ = false;
  secondary_pressed_ = false;
  toggle_fly_pressed_ = false;
  look_delta_x_ = 0.0F;
  look_delta_y_ = 0.0F;
  return command;
}

void LocalPlayerController::enter_world(WorldID world_id, Vec3f body_position) noexcept {
  body_position_ = body_position;
  camera_.set_world_id(world_id);
  camera_.set_position(
      {body_position.x,
       body_position.y + current_eye_height_,
       body_position.z});
}

void LocalPlayerController::apply_authoritative_state(const protocol::PlayerCorrection& correction) {
  if (correction.server_tick < last_authoritative_server_tick_) {
    return;
  }
  last_authoritative_server_tick_ = correction.server_tick;
  if (!has_unconfirmed_hotbar_selection_ || correction.selected_hotbar_slot == selected_hotbar_slot_) {
    selected_hotbar_slot_ = correction.selected_hotbar_slot;
    has_unconfirmed_hotbar_selection_ = false;
  }
  body_position_ = correction.position;
  game_mode_id_ = correction.game_mode_id;
  resolved_permissions_.core = correction.resolved_core_permissions;
  block_break_progress_ = correction.block_break;
  target_eye_height_ =
      correction.swimming
          ? tuning_.swimming_eye_height
          : (correction.sneaking ? tuning_.sneaking_eye_height
                                 : tuning_.standing_eye_height);
  camera_.set_world_id(correction.world_id);
  camera_.set_rotation(correction.yaw_radians, correction.pitch_radians);
  camera_.set_position(
      {correction.position.x,
       correction.position.y + current_eye_height_,
       correction.position.z});
}

} // namespace arv::client
