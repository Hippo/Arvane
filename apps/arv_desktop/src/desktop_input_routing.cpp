#include "desktop_input_routing.hpp"

#include "arv/client/client_runtime.hpp"
#include "arv/client/ui_interactions.hpp"

namespace arv::desktop {

std::optional<usize> inventory_slot_from_ui_action(const ui::UiAction& action) noexcept {
  return client::inventory_slot_from_ui_action(action);
}

bool apply_inventory_action(
    client::ClientRuntime& client,
    const std::optional<ui::UiAction>& action,
    player::InventoryClickButton button) {
  if (!action.has_value()) {
    return false;
  }
  if (const std::optional<usize> slot = inventory_slot_from_ui_action(action.value());
      slot.has_value()) {
    (void)client.request_inventory_slot_click(slot.value(), button);
    return true;
  }
  return false;
}

bool route_inventory_mouse_input(
    ui::UiTree& ui_tree,
    client::ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    bool has_carried_stack) {
  bool changed = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    changed = apply_inventory_action(
                  client,
                  ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left),
                  player::InventoryClickButton::Primary) ||
              changed;
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(false);
      changed = true;
    }
  }
  if (platform_input.mouse_right_pressed) {
    const std::optional<ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    changed = apply_inventory_action(
                  client,
                  ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Right),
                  player::InventoryClickButton::Secondary) ||
              changed;
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(true);
      changed = true;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return changed;
}

bool route_screen_mouse_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input) {
  bool handled = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_context.tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiAction> action =
        ui_context.tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
    if (action.has_value()) {
      handled = screen_manager.route_action(action.value(), ui_context) || handled;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_context.tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_context.tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_context.tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return handled;
}

void route_ui_mouse_focus(
    ui::UiTree& ui_tree,
    const platform::InputSnapshot& platform_input) {
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    (void)ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_tree.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
}

} // namespace arv::desktop
