#include "arv/client/client_input_mapping.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(ClientInputMappingTests, ConvertsPlatformSnapshotToInputFrame) {
  arv::platform::InputSnapshot snapshot;
  snapshot.move_forward = true;
  snapshot.move_left = true;
  snapshot.jump_pressed = true;
  snapshot.jump_held = true;
  snapshot.sneak_held = true;
  snapshot.sprint_held = true;
  snapshot.toggle_fly_pressed = true;
  snapshot.mount_pressed = true;
  snapshot.vehicle_roll_left_held = true;
  snapshot.primary_pressed = true;
  snapshot.primary_held = true;
  snapshot.secondary_pressed = true;
  snapshot.secondary_held = true;
  snapshot.drop_item_pressed = true;
  snapshot.drop_item_held = true;
  snapshot.open_inventory_pressed = true;
  snapshot.open_chat_pressed = true;
  snapshot.tab_menu_held = true;
  snapshot.hotbar_select = 4U;
  snapshot.hotbar_scroll_steps = -1;
  snapshot.mouse_delta_x = 12.5F;
  snapshot.mouse_delta_y = -4.0F;
  snapshot.text_input = "hello";
  snapshot.text_backspace_pressed = true;
  snapshot.text_submit_pressed = true;
  snapshot.text_cancel_pressed = true;
  snapshot.text_edit_events.push_back(
      {arv::platform::TextEditCommand::Paste, true, "clip"});

  const arv::client::InputFrame frame =
      arv::client::input_frame_from_platform_snapshot(snapshot);

  EXPECT_TRUE(frame.move_forward);
  EXPECT_TRUE(frame.move_left);
  EXPECT_TRUE(frame.jump_pressed);
  EXPECT_TRUE(frame.jump_held);
  EXPECT_TRUE(frame.sneak_held);
  EXPECT_TRUE(frame.sprint_held);
  EXPECT_TRUE(frame.toggle_fly_pressed);
  EXPECT_TRUE(frame.mount_pressed);
  EXPECT_TRUE(frame.vehicle_roll_left_held);
  EXPECT_TRUE(frame.primary_pressed);
  EXPECT_TRUE(frame.primary_held);
  EXPECT_TRUE(frame.secondary_pressed);
  EXPECT_TRUE(frame.secondary_held);
  EXPECT_TRUE(frame.drop_item_pressed);
  EXPECT_TRUE(frame.drop_item_held);
  EXPECT_TRUE(frame.open_inventory_pressed);
  EXPECT_TRUE(frame.open_chat_pressed);
  EXPECT_TRUE(frame.tab_menu_held);
  ASSERT_TRUE(frame.hotbar_select.has_value());
  EXPECT_EQ(frame.hotbar_select.value(), 4U);
  EXPECT_EQ(frame.hotbar_scroll_steps, -1);
  EXPECT_FLOAT_EQ(frame.mouse_delta_x, 12.5F);
  EXPECT_FLOAT_EQ(frame.mouse_delta_y, -4.0F);
  EXPECT_EQ(frame.text_input, "hello");
  EXPECT_TRUE(frame.text_backspace_pressed);
  EXPECT_TRUE(frame.text_submit_pressed);
  EXPECT_TRUE(frame.text_cancel_pressed);
  ASSERT_EQ(frame.text_edit_events.size(), 1U);
  EXPECT_EQ(
      frame.text_edit_events.front().command,
      arv::core::TextEditCommand::Paste);
  EXPECT_TRUE(frame.text_edit_events.front().extend_selection);
  EXPECT_EQ(frame.text_edit_events.front().clipboard_text, "clip");
}

TEST(ClientInputMappingTests, CancelEdgeAlsoRequestsInventory) {
  arv::platform::InputSnapshot snapshot;
  snapshot.text_cancel_pressed = true;

  const arv::client::InputFrame frame =
      arv::client::input_frame_from_platform_snapshot(snapshot);

  EXPECT_TRUE(frame.open_inventory_pressed);
  EXPECT_TRUE(frame.text_cancel_pressed);
}

TEST(ClientInputMappingTests, ConvertsCapturedPlatformBindingToUiView) {
  const arv::platform::RawInputEvent event{
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::Keyboard,
          "E",
          arv::platform::input_modifier_mask(arv::platform::InputModifier::Shift)}};

  const arv::ui::CapturedInputView view =
      arv::client::captured_input_from_platform(event);

  EXPECT_EQ(view.device, arv::ui::InputBindingDevice::Keyboard);
  EXPECT_EQ(view.input_code, "E");
  EXPECT_FALSE(view.label.empty());
  EXPECT_EQ(
      view.modifiers,
      arv::platform::input_modifier_mask(arv::platform::InputModifier::Shift));
}

TEST(ClientInputMappingTests, FirstActionBindingLabelUsesFirstConfiguredBinding) {
  arv::platform::InputBindings bindings = arv::platform::default_input_bindings();
  arv::platform::ActionBindingList& mount_bindings =
      arv::platform::bindings_for_action(
          bindings,
          arv::platform::LogicalAction::Mount);
  mount_bindings.insert(
      mount_bindings.begin(),
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::MouseButton,
          "Right",
          0U});

  EXPECT_EQ(
      arv::client::first_action_binding_label(
          bindings,
          arv::platform::LogicalAction::Mount),
      arv::client::input_binding_label(mount_bindings.front()));
}

TEST(ClientInputMappingTests, FirstActionBindingLabelReturnsEmptyWhenUnbound) {
  arv::platform::InputBindings bindings = arv::platform::default_input_bindings();
  arv::platform::bindings_for_action(
      bindings,
      arv::platform::LogicalAction::Mount)
      .clear();

  EXPECT_TRUE(
      arv::client::first_action_binding_label(
          bindings,
          arv::platform::LogicalAction::Mount)
          .empty());
}

TEST(ClientInputMappingTests, ClearsPerFramePlatformInputEdges) {
  arv::platform::InputSnapshot input;
  input.close_requested = true;
  input.move_forward = true;
  input.jump_pressed = true;
  input.jump_held = true;
  input.toggle_fly_pressed = true;
  input.mount_pressed = true;
  input.primary_pressed = true;
  input.primary_held = true;
  input.secondary_pressed = true;
  input.secondary_held = true;
  input.drop_item_pressed = true;
  input.drop_item_held = true;
  input.open_inventory_pressed = true;
  input.open_chat_pressed = true;
  input.tab_menu_held = true;
  input.shift_modifier_held = true;
  input.debug_menu_toggle_pressed = true;
  input.hotbar_select = 3U;
  input.hotbar_scroll_steps = -2;
  input.mouse_left_pressed = true;
  input.mouse_left_released = true;
  input.mouse_left_held = true;
  input.mouse_right_pressed = true;
  input.mouse_right_released = true;
  input.mouse_right_held = true;
  input.cursor_x = 12.0F;
  input.cursor_y = 34.0F;
  input.mouse_delta_x = 5.0F;
  input.mouse_delta_y = -6.0F;
  input.mouse_wheel_y = 1.0F;
  input.text_input = "typed";
  input.text_backspace_pressed = true;
  input.text_submit_pressed = true;
  input.text_cancel_pressed = true;
  input.text_edit_events.push_back(
      {arv::platform::TextEditCommand::Copy, false, {}});
  input.raw_input = arv::platform::RawInputEvent{
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::MouseButton,
          "Left",
          0U}};

  arv::client::clear_platform_input_frame_edges(input);

  EXPECT_TRUE(input.close_requested);
  EXPECT_TRUE(input.move_forward);
  EXPECT_FALSE(input.jump_pressed);
  EXPECT_TRUE(input.jump_held);
  EXPECT_FALSE(input.toggle_fly_pressed);
  EXPECT_FALSE(input.mount_pressed);
  EXPECT_FALSE(input.primary_pressed);
  EXPECT_TRUE(input.primary_held);
  EXPECT_FALSE(input.secondary_pressed);
  EXPECT_TRUE(input.secondary_held);
  EXPECT_FALSE(input.drop_item_pressed);
  EXPECT_TRUE(input.drop_item_held);
  EXPECT_FALSE(input.open_inventory_pressed);
  EXPECT_FALSE(input.open_chat_pressed);
  EXPECT_TRUE(input.tab_menu_held);
  EXPECT_TRUE(input.shift_modifier_held);
  EXPECT_FALSE(input.debug_menu_toggle_pressed);
  EXPECT_FALSE(input.hotbar_select.has_value());
  EXPECT_EQ(input.hotbar_scroll_steps, 0);
  EXPECT_FALSE(input.mouse_left_pressed);
  EXPECT_FALSE(input.mouse_left_released);
  EXPECT_TRUE(input.mouse_left_held);
  EXPECT_FALSE(input.mouse_right_pressed);
  EXPECT_FALSE(input.mouse_right_released);
  EXPECT_TRUE(input.mouse_right_held);
  EXPECT_FLOAT_EQ(input.cursor_x, 12.0F);
  EXPECT_FLOAT_EQ(input.cursor_y, 34.0F);
  EXPECT_FLOAT_EQ(input.mouse_delta_x, 0.0F);
  EXPECT_FLOAT_EQ(input.mouse_delta_y, 0.0F);
  EXPECT_FLOAT_EQ(input.mouse_wheel_y, 0.0F);
  EXPECT_TRUE(input.text_input.empty());
  EXPECT_FALSE(input.text_backspace_pressed);
  EXPECT_FALSE(input.text_submit_pressed);
  EXPECT_FALSE(input.text_cancel_pressed);
  EXPECT_TRUE(input.text_edit_events.empty());
  EXPECT_FALSE(input.raw_input.has_value());
}

TEST(ClientInputMappingTests, ClearsCapturedBindingPlatformInputEdges) {
  arv::platform::InputSnapshot input;
  input.jump_pressed = true;
  input.primary_pressed = true;
  input.drop_item_pressed = true;
  input.open_inventory_pressed = true;
  input.open_chat_pressed = true;
  input.debug_menu_toggle_pressed = true;
  input.hotbar_select = 8U;
  input.hotbar_scroll_steps = 1;
  input.mouse_left_pressed = true;
  input.mouse_left_released = true;
  input.mouse_left_held = true;
  input.mouse_delta_x = 3.0F;
  input.mouse_wheel_y = -1.0F;
  input.text_input = "keep";
  input.text_backspace_pressed = true;
  input.text_submit_pressed = true;
  input.text_cancel_pressed = true;
  input.text_edit_events.push_back(
      {arv::platform::TextEditCommand::Paste, false, "clip"});
  input.raw_input = arv::platform::RawInputEvent{
      arv::platform::InputBinding{
          arv::platform::InputDeviceType::Keyboard,
          "K",
          0U}};

  arv::client::clear_platform_input_captured_binding_edges(input);

  EXPECT_TRUE(input.jump_pressed);
  EXPECT_TRUE(input.primary_pressed);
  EXPECT_TRUE(input.drop_item_pressed);
  EXPECT_FALSE(input.open_inventory_pressed);
  EXPECT_FALSE(input.open_chat_pressed);
  EXPECT_FALSE(input.debug_menu_toggle_pressed);
  EXPECT_FALSE(input.hotbar_select.has_value());
  EXPECT_EQ(input.hotbar_scroll_steps, 0);
  EXPECT_TRUE(input.mouse_left_pressed);
  EXPECT_TRUE(input.mouse_left_released);
  EXPECT_TRUE(input.mouse_left_held);
  EXPECT_FLOAT_EQ(input.mouse_delta_x, 3.0F);
  EXPECT_FLOAT_EQ(input.mouse_wheel_y, 0.0F);
  EXPECT_EQ(input.text_input, "keep");
  EXPECT_FALSE(input.text_backspace_pressed);
  EXPECT_FALSE(input.text_submit_pressed);
  EXPECT_FALSE(input.text_cancel_pressed);
  ASSERT_EQ(input.text_edit_events.size(), 1U);
  EXPECT_FALSE(input.raw_input.has_value());
}

} // namespace
