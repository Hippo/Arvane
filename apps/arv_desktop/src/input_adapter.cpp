#include "input_adapter.hpp"

namespace arv::desktop {

client::InputFrame to_client_input(const platform::InputSnapshot& input) {
  return {
      input.move_forward,
      input.move_backward,
      input.move_left,
      input.move_right,
      input.jump_pressed,
      input.jump_held,
      input.sneak_held,
      input.sprint_held,
      input.primary_pressed,
      input.primary_held,
      input.secondary_pressed,
      input.secondary_held,
      input.toggle_fly_pressed,
      input.open_inventory_pressed,
      input.open_chat_pressed,
      input.tab_menu_held,
      input.hotbar_select,
      input.hotbar_scroll_steps,
      input.mouse_delta_x,
      input.mouse_delta_y,
      input.text_input,
      input.text_backspace_pressed,
      input.text_submit_pressed,
      input.text_cancel_pressed};
}

} // namespace arv::desktop
