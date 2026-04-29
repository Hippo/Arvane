#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/player_input.hpp"

#include <optional>
#include <string>

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
  bool open_inventory_pressed{};
  bool open_chat_pressed{};
  bool tab_menu_held{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
  std::string text_input{};
  bool text_backspace_pressed{};
  bool text_submit_pressed{};
  bool text_cancel_pressed{};
};

class LocalPlayerController {
public:
  void submit_input(InputFrame input) noexcept;
  [[nodiscard]] player::PlayerCommand consume_command(TickIndex tick_index) noexcept;
  void apply_authoritative_state(const protocol::PlayerCorrection& correction);

  [[nodiscard]] const CameraShell& camera() const noexcept {
    return camera_;
  }

  [[nodiscard]] CameraShell& camera() noexcept {
    return camera_;
  }

  [[nodiscard]] u8 selected_hotbar_slot() const noexcept {
    return selected_hotbar_slot_;
  }

  [[nodiscard]] const std::string& game_mode_id() const noexcept {
    return game_mode_id_;
  }

  [[nodiscard]] const player::ResolvedPermissions& resolved_permissions() const noexcept {
    return resolved_permissions_;
  }

  [[nodiscard]] const Vec3f& player_position() const noexcept {
    return body_position_;
  }

private:
  CameraShell camera_{};
  u8 selected_hotbar_slot_{};
  bool has_unconfirmed_hotbar_selection_{};
  TickIndex last_authoritative_server_tick_{};
  std::string game_mode_id_{std::string{player::SurvivalGameModeID}};
  player::ResolvedPermissions resolved_permissions_{};
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
