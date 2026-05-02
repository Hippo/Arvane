#include "client_input_runtime.hpp"

#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

#include <utility>

namespace arv::client::detail {

void ClientInputRuntime::submit_input(
    InputFrame input,
    ClientUiState& ui,
    LocalPlayerController& player_controller) {
  ui.set_tab_menu_visible(input.tab_menu_held);
  const bool was_chat_open = ui.chat_open();

  if (input.text_cancel_pressed && (ui.inventory_open() || ui.chat_open())) {
    ui.close_ui();
  } else if (!ui.chat_open() && input.open_inventory_pressed) {
    ui.toggle_inventory_if_allowed(player_controller.resolved_permissions());
  }
  if (!was_chat_open && input.open_chat_pressed) {
    ui.open_chat();
  }

  ui.handle_chat_input(input, was_chat_open);

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
    ClientProtocolSender& sender) {
  const player::PlayerCommand command = player_controller.consume_command(tick_index);
  (void)sender.send_input_batch(tick_index, command);
}

} // namespace arv::client::detail
