#include "client_input_runtime.hpp"

#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

#include <algorithm>
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
    input.toggle_fly_pressed = false;
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
    const physics::ICollisionQuery* collision_query,
    Seconds tick_seconds) {
  const player::PlayerCommand command = player_controller.consume_command(tick_index);
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
