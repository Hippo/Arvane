#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/player_input.hpp"

#include <optional>

namespace arv::client {

struct InputFrame {
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool jump_pressed{};
  bool jump_held{};
  bool sneak_held{};
  bool sprint_held{};
  bool primary_pressed{};
  bool primary_held{};
  bool secondary_pressed{};
  bool secondary_held{};
  bool toggle_fly_pressed{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
};

class LocalPlayerController {
public:
  void submit_input(InputFrame input) noexcept;
  [[nodiscard]] player::PlayerCommand consume_command(TickIndex tick_index) noexcept;
  void apply_authoritative_state(const protocol::PlayerCorrection& correction) noexcept;

  [[nodiscard]] const CameraShell& camera() const noexcept {
    return camera_;
  }

  [[nodiscard]] CameraShell& camera() noexcept {
    return camera_;
  }

private:
  CameraShell camera_{};
  u8 selected_hotbar_slot_{};
  bool move_forward_{};
  bool move_backward_{};
  bool move_left_{};
  bool move_right_{};
  bool jump_held_{};
  bool sneak_held_{};
  bool sprint_held_{};
  bool primary_held_{};
  bool secondary_held_{};
  bool jump_pressed_{};
  bool primary_pressed_{};
  bool secondary_pressed_{};
  bool toggle_fly_pressed_{};
  f32 look_delta_x_{};
  f32 look_delta_y_{};
  Vec3f body_position_{8.0F, 20.0F, -20.0F};
  f32 current_eye_height_{player::DefaultStandingEyeHeight};
  f32 target_eye_height_{player::DefaultStandingEyeHeight};
};

} // namespace arv::client
