#include "arv/client/ui_interactions.hpp"

#include "arv/text/rich_text.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace arv::client {
namespace {

constexpr auto HotbarItemPopupVisibleDuration =
    std::chrono::milliseconds(1500);
constexpr auto HotbarItemPopupFadeDuration =
    std::chrono::milliseconds(350);
constexpr auto MountExitPromptVisibleDuration =
    std::chrono::milliseconds(2400);
constexpr auto MountExitPromptFadeDuration =
    std::chrono::milliseconds(700);

[[nodiscard]] std::string stack_display_name(const ui::UiItemStackView& stack) {
  return stack.display_name.empty() ? stack.item_name : stack.display_name;
}

[[nodiscard]] std::optional<usize> parse_unsigned_prefix_number(
    std::string_view text) noexcept {
  const usize dot = text.find('.');
  if (dot != std::string_view::npos) {
    text = text.substr(0U, dot);
  }
  if (text.empty()) {
    return std::nullopt;
  }

  usize value = 0U;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<usize>(ch - '0');
  }
  return value;
}

} // namespace

std::optional<usize> inventory_slot_from_ui_action(
    const ui::UiAction& action) noexcept {
  if (action.action_id != "inventory_slot") {
    return std::nullopt;
  }

  constexpr std::array<std::string_view, 2> prefixes{{
      "hud.hotbar.slot.",
      "inventory.slot.",
  }};
  for (std::string_view prefix : prefixes) {
    const std::string_view id{action.element_id};
    if (!id.starts_with(prefix)) {
      continue;
    }
    return parse_unsigned_prefix_number(id.substr(prefix.size()));
  }
  return std::nullopt;
}

std::optional<ContainerSlotActionRef> container_slot_from_ui_action(
    const ui::UiAction& action) noexcept {
  if (action.action_id != "inventory_slot") {
    return std::nullopt;
  }

  const std::string_view id{action.element_id};
  constexpr std::string_view container_prefix = "container.slot.";
  if (id.starts_with(container_prefix)) {
    const std::optional<usize> slot =
        parse_unsigned_prefix_number(id.substr(container_prefix.size()));
    if (slot.has_value()) {
      return ContainerSlotActionRef{
          player::ContainerSlotSource::OpenedContainer,
          slot.value()};
    }
  }

  constexpr std::string_view player_prefix = "container.player_slot.";
  if (id.starts_with(player_prefix)) {
    const std::optional<usize> slot =
        parse_unsigned_prefix_number(id.substr(player_prefix.size()));
    if (slot.has_value()) {
      return ContainerSlotActionRef{
          player::ContainerSlotSource::PlayerInventory,
          slot.value()};
    }
  }
  return std::nullopt;
}

std::optional<std::pair<usize, usize>> chat_run_from_ui_action(
    const ui::UiAction& action) {
  if (action.action_id != "chat_run") {
    return std::nullopt;
  }
  constexpr std::string_view prefix = "chat.line.";
  constexpr std::string_view middle = ".run.";
  const std::string_view id{action.element_id};
  if (!id.starts_with(prefix)) {
    return std::nullopt;
  }
  const usize middle_pos = id.find(middle, prefix.size());
  if (middle_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::optional<usize> line =
      parse_unsigned_prefix_number(id.substr(prefix.size(), middle_pos - prefix.size()));
  std::optional<usize> run =
      parse_unsigned_prefix_number(id.substr(middle_pos + middle.size()));
  if (!line.has_value() || !run.has_value()) {
    return std::nullopt;
  }
  return std::pair<usize, usize>{line.value(), run.value()};
}

bool safe_chat_url_scheme(std::string_view url) noexcept {
  const usize colon = url.find(':');
  if (colon == std::string_view::npos || colon == 0U) {
    return false;
  }

  std::string scheme;
  scheme.reserve(colon);
  for (usize index = 0; index < colon; ++index) {
    char ch = url[index];
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
    scheme.push_back(ch);
  }
  return scheme == "http" || scheme == "https";
}

void reset_hotbar_item_popup(HotbarItemPopupState& state) noexcept {
  state = {};
}

void update_hotbar_item_popup(
    HotbarItemPopupState& state,
    u8 selected_hotbar_slot,
    std::span<const ui::UiItemStackView> inventory_slots,
    std::chrono::steady_clock::time_point now) {
  std::string selected_item_name;
  if (selected_hotbar_slot < inventory_slots.size()) {
    const ui::UiItemStackView& stack = inventory_slots[selected_hotbar_slot];
    if (!stack.empty()) {
      selected_item_name = stack_display_name(stack);
    }
  }

  if (!state.observation_initialized) {
    state.observed_slot = selected_hotbar_slot;
    state.observed_item_name = std::move(selected_item_name);
    state.observation_initialized = true;
    return;
  }

  if (selected_hotbar_slot == state.observed_slot &&
      selected_item_name == state.observed_item_name) {
    return;
  }

  state.observed_slot = selected_hotbar_slot;
  state.observed_item_name = selected_item_name;
  if (selected_item_name.empty()) {
    state.popup_name.clear();
    state.expires_at = now;
    return;
  }

  state.popup_name = std::move(selected_item_name);
  state.expires_at = now + HotbarItemPopupVisibleDuration;
}

f32 hotbar_item_popup_alpha(
    const HotbarItemPopupState& state,
    std::chrono::steady_clock::time_point now) noexcept {
  if (state.popup_name.empty() || now >= state.expires_at) {
    return 0.0F;
  }

  const auto remaining = state.expires_at - now;
  if (remaining >= HotbarItemPopupFadeDuration) {
    return 1.0F;
  }
  const auto remaining_ms =
      std::chrono::duration<f32, std::milli>(remaining).count();
  const auto fade_ms =
      std::chrono::duration<f32, std::milli>(HotbarItemPopupFadeDuration).count();
  return std::clamp(remaining_ms / fade_ms, 0.0F, 1.0F);
}

void reset_mount_exit_prompt(MountExitPromptState& state) noexcept {
  state = {};
}

void update_mount_exit_prompt(
    MountExitPromptState& state,
    bool mounted,
    std::chrono::steady_clock::time_point now) {
  if (!state.observation_initialized) {
    state.observed_mounted = mounted;
    state.observation_initialized = true;
    return;
  }

  if (mounted && !state.observed_mounted) {
    state.expires_at =
        now + MountExitPromptVisibleDuration + MountExitPromptFadeDuration;
  } else if (!mounted) {
    state.expires_at = now;
  }
  state.observed_mounted = mounted;
}

f32 mount_exit_prompt_alpha(
    const MountExitPromptState& state,
    std::chrono::steady_clock::time_point now) noexcept {
  if (!state.observed_mounted || now >= state.expires_at) {
    return 0.0F;
  }

  const auto remaining = state.expires_at - now;
  if (remaining >= MountExitPromptFadeDuration) {
    return 1.0F;
  }
  const auto remaining_ms =
      std::chrono::duration<f32, std::milli>(remaining).count();
  const auto fade_ms =
      std::chrono::duration<f32, std::milli>(MountExitPromptFadeDuration).count();
  return std::clamp(remaining_ms / fade_ms, 0.0F, 1.0F);
}

bool append_inventory_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool inventory_open,
    const std::optional<ui::UiItemStackView>& carried_stack,
    std::span<const ui::UiItemStackView> inventory_slots,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action) {
  if (!inventory_open ||
      (carried_stack.has_value() && !carried_stack->empty()) ||
      tree.find_by_stable_id("inventory.tooltip") != nullptr ||
      !hover_action.has_value() ||
      !std::string_view{hover_action->element_id}.starts_with("inventory.slot.")) {
    return false;
  }

  const std::optional<usize> slot = inventory_slot_from_ui_action(*hover_action);
  if (!slot.has_value() || slot.value() >= inventory_slots.size()) {
    return false;
  }

  const ui::UiItemStackView& stack = inventory_slots[slot.value()];
  if (stack.empty()) {
    return false;
  }
  std::string tooltip_text = stack_display_name(stack);
  if (tooltip_text.empty()) {
    return false;
  }

  ui::append_item_tooltip(
      tree,
      {
          true,
          std::move(tooltip_text),
          metrics.logical_size,
          cursor_position,
      });
  return true;
}

bool append_container_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool container_open,
    const std::optional<ui::UiItemStackView>& carried_stack,
    std::span<const ui::UiItemStackView> container_slots,
    std::span<const ui::UiItemStackView> player_slots,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action) {
  if (!container_open ||
      (carried_stack.has_value() && !carried_stack->empty()) ||
      tree.find_by_stable_id("inventory.tooltip") != nullptr ||
      !hover_action.has_value()) {
    return false;
  }

  const std::optional<ContainerSlotActionRef> slot =
      container_slot_from_ui_action(hover_action.value());
  if (!slot.has_value()) {
    return false;
  }
  const std::span<const ui::UiItemStackView> slots =
      slot->source == player::ContainerSlotSource::OpenedContainer
          ? container_slots
          : player_slots;
  if (slot->slot >= slots.size()) {
    return false;
  }

  const ui::UiItemStackView& stack = slots[slot->slot];
  if (stack.empty()) {
    return false;
  }
  std::string tooltip_text = stack_display_name(stack);
  if (tooltip_text.empty()) {
    return false;
  }

  ui::append_item_tooltip(
      tree,
      {
          true,
          std::move(tooltip_text),
          metrics.logical_size,
          cursor_position,
      });
  return true;
}

bool append_chat_hover_tooltip_if_needed(
    ui::UiTree& tree,
    bool chat_open,
    std::span<const ui::ChatLineView> chat_lines,
    const ui::UiDisplayMetrics& metrics,
    Vec2f cursor_position,
    const std::optional<ui::UiAction>& hover_action) {
  if (!chat_open ||
      tree.find_by_stable_id("chat.tooltip") != nullptr ||
      !hover_action.has_value()) {
    return false;
  }

  const std::optional<std::pair<usize, usize>> run_id =
      chat_run_from_ui_action(hover_action.value());
  if (!run_id.has_value() ||
      run_id->first >= chat_lines.size() ||
      run_id->second >= chat_lines[run_id->first].runs.size()) {
    return false;
  }

  const arv::text::RichTextRun& run =
      chat_lines[run_id->first].runs[run_id->second];
  if (!run.hover_event.has_value() ||
      run.hover_event->action != arv::text::HoverAction::ShowText ||
      run.hover_event->show_text == nullptr) {
    return false;
  }
  const std::string tooltip = arv::text::to_plain_text(*run.hover_event->show_text);
  if (tooltip.empty()) {
    return false;
  }

  ui::append_chat_tooltip(
      tree,
      {
          true,
          tooltip,
          metrics.logical_size,
          cursor_position,
      });
  return true;
}

GameplayHoverTooltipResult update_gameplay_hover_tooltips(
    GameplayHoverTooltipInput input) {
  if (input.tree == nullptr || input.metrics == nullptr) {
    return {};
  }

  ui::UiTree& tree = *input.tree;
  const ui::UiDisplayMetrics& metrics = *input.metrics;
  const std::optional<ui::UiAction> hover_action =
      tree.route_mouse_move(input.cursor_position);
  GameplayHoverTooltipResult result;
  result.inventory_added = append_inventory_hover_tooltip_if_needed(
      tree,
      input.inventory_open,
      input.carried_stack,
      input.inventory_slots,
      metrics,
      input.cursor_position,
      hover_action);
  result.container_added = append_container_hover_tooltip_if_needed(
      tree,
      input.container_open,
      input.carried_stack,
      input.container_slots,
      input.inventory_slots,
      metrics,
      input.cursor_position,
      hover_action);
  result.chat_added = append_chat_hover_tooltip_if_needed(
      tree,
      input.chat_open,
      input.chat_lines,
      metrics,
      input.cursor_position,
      hover_action);

  if (result.inventory_added || result.container_added || result.chat_added) {
    tree.layout_with_metrics(metrics, input.text_measurer);
    (void)tree.route_mouse_move(input.cursor_position);
    result.relaid_out = true;
  }
  return result;
}

} // namespace arv::client
