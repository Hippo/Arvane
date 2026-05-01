#pragma once

#include "arv/core/types.hpp"
#include "arv/player/inventory.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/ui/ui.hpp"

#include <optional>

namespace arv::client {
class ClientRuntime;
} // namespace arv::client

namespace arv::desktop {

[[nodiscard]] std::optional<usize> inventory_slot_from_ui_action(
    const ui::UiAction& action) noexcept;

[[nodiscard]] bool apply_inventory_action(
    client::ClientRuntime& client,
    const std::optional<ui::UiAction>& action,
    player::InventoryClickButton button);

[[nodiscard]] bool route_inventory_mouse_input(
    ui::UiTree& ui_tree,
    client::ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    bool has_carried_stack);

[[nodiscard]] bool route_screen_mouse_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input);

void route_ui_mouse_focus(
    ui::UiTree& ui_tree,
    const platform::InputSnapshot& platform_input);

} // namespace arv::desktop
