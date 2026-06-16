#pragma once

#include "arv/base/ui/widgets/chat.hpp"
#include "arv/base/ui/widgets/inventory.hpp"
#include "arv/core/time.hpp"
#include "arv/player/container.hpp"
#include "arv/ui/core/ui.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace arv::client {

struct HotbarItemPopupState {
  std::chrono::steady_clock::time_point expires_at{};
  std::string popup_name{};
  std::string observed_item_name{};
  u8 observed_slot{};
  bool observation_initialized{};
};

struct ContainerSlotActionRef {
  player::ContainerSlotSource source{player::ContainerSlotSource::OpenedContainer};
  usize slot{};
};

[[nodiscard]] std::optional<usize> inventory_slot_from_ui_action(
    const ui::UiAction& action) noexcept;

[[nodiscard]] std::optional<ContainerSlotActionRef> container_slot_from_ui_action(
    const ui::UiAction& action) noexcept;

[[nodiscard]] std::optional<std::pair<usize, usize>> chat_run_from_ui_action(
    const ui::UiAction& action);

[[nodiscard]] bool safe_chat_url_scheme(std::string_view url) noexcept;

void reset_hotbar_item_popup(HotbarItemPopupState& state) noexcept;

void update_hotbar_item_popup(
    HotbarItemPopupState& state,
    u8 selected_hotbar_slot,
    std::span<const ui::UiItemStackView> inventory_slots,
    std::chrono::steady_clock::time_point now);

[[nodiscard]] f32 hotbar_item_popup_alpha(
    const HotbarItemPopupState& state,
    std::chrono::steady_clock::time_point now) noexcept;

[[nodiscard]] bool append_inventory_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool inventory_open,
    const std::optional<ui::UiItemStackView>& carried_stack,
    std::span<const ui::UiItemStackView> inventory_slots,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action);

[[nodiscard]] bool append_container_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool container_open,
    const std::optional<ui::UiItemStackView>& carried_stack,
    std::span<const ui::UiItemStackView> container_slots,
    std::span<const ui::UiItemStackView> player_slots,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action);

[[nodiscard]] bool append_chat_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool chat_open,
    std::span<const ui::ChatLineView> chat_lines,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action);

} // namespace arv::client
