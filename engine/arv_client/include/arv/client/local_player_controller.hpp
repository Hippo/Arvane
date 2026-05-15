#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/core/text_edit.hpp"
#include "arv/physics/player_movement.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/player_input.hpp"

#include <optional>
#include <string>
#include <vector>

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
  std::vector<arv::core::TextEditInput> text_edit_events{};
};

class LocalPlayerController {
public:
  void set_player_tuning(player::PlayerTuning tuning) noexcept;
  void submit_input(InputFrame input) noexcept;
  [[nodiscard]] player::PlayerCommand consume_command(TickIndex tick_index) noexcept;
  void update_local_movement(
      const player::PlayerCommand& command,
      const physics::ICollisionQuery* collision_query,
      Seconds tick_seconds);
  void enter_world(
      WorldID world_id,
      PlayerID player_id,
      EntityNetID local_entity_id,
      Vec3f body_position,
      TickIndex initial_server_tick = 0,
      TickRate tick_rate = DefaultBaseTps) noexcept;
  void leave_world() noexcept;
  void apply_move_correction(const protocol::ClientboundPlayerMoveCorrection& correction);
  void apply_player_velocity(const protocol::ClientboundPlayerVelocity& velocity) noexcept;
  void apply_player_permissions(
      const protocol::ClientboundPlayerPermissions& permissions);
  void apply_player_movement_tuning(
      const protocol::ClientboundPlayerMovementTuning& tuning) noexcept;
  void apply_block_break_progress(
      const protocol::ClientboundPlayerBlockBreakProgress& progress) noexcept;
  [[nodiscard]] protocol::ServerboundPlayerMove build_movement_packet(
      u32 sequence,
      TickIndex client_tick,
      protocol::PlayerActionFlags action_flags) const noexcept;

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

  [[nodiscard]] const Vec3f& rendered_player_position() const noexcept {
    return render_position_;
  }

  [[nodiscard]] std::optional<PlayerID> player_id() const noexcept {
    return player_id_;
  }

  [[nodiscard]] std::optional<EntityNetID> local_entity_id() const noexcept {
    return local_entity_id_;
  }

  [[nodiscard]] const protocol::BlockBreakProgress& block_break_progress() const noexcept {
    return block_break_progress_;
  }

  [[nodiscard]] TickIndex world_initial_tick() const noexcept {
    return world_initial_tick_;
  }

  [[nodiscard]] TickRate server_tick_rate() const noexcept {
    return server_tick_rate_;
  }

private:
  void update_eye_height() noexcept;
  void update_target_eye_height() noexcept;
  void apply_knockback_control_suppression(
      Vec3f incoming_velocity,
      const physics::ICollisionQuery* collision_query,
      Seconds tick_seconds) noexcept;
  void update_render_smoothing() noexcept;
  void update_camera_transform() noexcept;
  [[nodiscard]] bool consume_fly_toggle_request(TickIndex tick_index) noexcept;
  [[nodiscard]] physics::PlayerMovementAuthority movement_authority() const noexcept;
  [[nodiscard]] protocol::PlayerMoveFlags movement_flags() const noexcept;

  CameraShell camera_{};
  std::optional<PlayerID> player_id_{};
  std::optional<EntityNetID> local_entity_id_{};
  physics::PlayerKinematicState movement_state_{};
  player::PlayerTuning tuning_{player::default_player_tuning()};
  u8 selected_hotbar_slot_{};
  TickIndex world_initial_tick_{};
  TickRate server_tick_rate_{DefaultBaseTps};
  std::string game_mode_id_{std::string{player::SurvivalGameModeID}};
  player::ResolvedPermissions resolved_permissions_{};
  protocol::BlockBreakProgress block_break_progress_{};
  u32 knockback_control_suppression_ticks_{};
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
  std::optional<TickIndex> last_jump_press_tick_{};
  f32 look_delta_x_{};
  f32 look_delta_y_{};
  Vec3f body_position_{8.0F, 20.0F, -20.0F};
  Vec3f render_position_{8.0F, 20.0F, -20.0F};
  Vec3f render_smoothing_offset_{};
  u32 render_smoothing_ticks_remaining_{};
  u32 render_smoothing_ticks_total_{};
  f32 current_eye_height_{player::DefaultStandingEyeHeight};
  f32 target_eye_height_{player::DefaultStandingEyeHeight};
};

} // namespace arv::client
