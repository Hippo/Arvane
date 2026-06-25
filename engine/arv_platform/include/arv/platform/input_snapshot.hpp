#pragma once

#include "arv/platform/input_bindings.hpp"

#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace arv::platform {

struct RawInputEvent {
  InputBinding binding{};
};

enum class TextEditCommand : u8 {
  MoveLeft,
  MoveRight,
  MoveUp,
  MoveDown,
  DeleteForward,
  Copy,
  Paste,
  Cut,
  SelectAll,
};

struct TextEditEvent {
  TextEditCommand command{TextEditCommand::MoveLeft};
  bool extend_selection{};
  std::string clipboard_text{};
};

struct InputSnapshot {
  bool close_requested{};
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool jump_pressed{};
  bool jump_held{};
  bool sneak_held{};
  bool sprint_held{};
  bool toggle_fly_pressed{};
  bool mount_pressed{};
  bool vehicle_roll_left_held{};
  bool vehicle_roll_right_held{};
  bool primary_pressed{};
  bool primary_held{};
  bool secondary_pressed{};
  bool secondary_held{};
  bool drop_item_pressed{};
  bool drop_item_held{};
  bool open_inventory_pressed{};
  bool open_chat_pressed{};
  bool tab_menu_held{};
  bool shift_modifier_held{};
  bool debug_menu_toggle_pressed{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
  bool mouse_left_pressed{};
  bool mouse_left_released{};
  bool mouse_left_held{};
  bool mouse_right_pressed{};
  bool mouse_right_released{};
  bool mouse_right_held{};
  f32 cursor_x{};
  f32 cursor_y{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
  f32 mouse_wheel_y{};
  std::string text_input{};
  bool text_backspace_pressed{};
  bool text_submit_pressed{};
  bool text_cancel_pressed{};
  std::vector<TextEditEvent> text_edit_events{};
  std::optional<RawInputEvent> raw_input{};
};

} // namespace arv::platform
