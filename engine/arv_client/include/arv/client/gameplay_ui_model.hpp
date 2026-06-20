#pragma once

#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/player/container.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::client {

struct RidePromptModelInput {
  bool interactions_available{};
  bool gameplay_blocked{};
  bool inventory_open{};
  bool chat_open{};
  bool local_player_mounted{};
  std::string_view key_label{};
  Vec2f viewport{};
  FunctionRef<bool()> has_targeted_rideable{};
};

struct GameplayUiModelInput {
  const ClientRuntime& client;
  const io::UiConfig::ChatConfig& chat_config;
  std::span<const ui::UiItemStackView> inventory_slots{};
  std::span<const ui::UiItemStackView> container_slots{};
  std::optional<ui::UiItemStackView> carried_inventory_stack{};
  std::span<const ui::ChatLineView> chat_lines{};
  usize chat_history_scroll_offset{};
  std::span<const ui::PlayerListEntryView> tab_players{};
  std::string_view inventory_title{};
  std::string_view container_title{};
  const player::ContainerUiLayoutDefinition* container_layout{};
  std::string_view tab_title{};
  Vec2f viewport{};
  Vec2f cursor_position{};
  u32 displayed_fps{};
  std::string_view hotbar_item_popup_name{};
  f32 hotbar_item_popup_alpha{};
  ui::CrosshairHudModel crosshair{};
  ui::RidePromptModel ride_prompt{};
  const arv::text::LanguageCatalog* language{};
  bool debug_fps_visible{true};
};

struct GameplayUiSnapshotModelInput {
  const ClientRuntime& client;
  const io::UiConfig::ChatConfig& chat_config;
  std::span<const ui::UiItemStackView> inventory_slots{};
  std::span<const ui::UiItemStackView> container_slots{};
  std::optional<ui::UiItemStackView> carried_inventory_stack{};
  std::span<const ui::ChatLineView> chat_lines{};
  usize chat_history_scroll_offset{};
  std::span<const ui::PlayerListEntryView> tab_players{};
  std::string inventory_title{};
  std::string tab_title{};
  const player::ContainerUiLayoutRegistry* container_layouts{};
  Vec2f viewport{};
  Vec2f cursor_position{};
  u32 displayed_fps{};
  std::string_view hotbar_item_popup_name{};
  f32 hotbar_item_popup_alpha{};
  ui::CrosshairHudModel crosshair{};
  ui::RidePromptModel ride_prompt{};
  const arv::text::LanguageCatalog* language{};
  bool debug_fps_visible{true};
};

[[nodiscard]] usize gameplay_chat_history_max_scroll_offset(
    usize history_size,
    usize max_visible_messages) noexcept;

[[nodiscard]] usize gameplay_chat_scroll_steps(f32 wheel_y) noexcept;

void refresh_gameplay_chat_lines(
    std::vector<ui::ChatLineView>& chat_lines,
    const ClientRuntime& client,
    const io::UiConfig::ChatConfig& chat_config,
    std::chrono::steady_clock::time_point now,
    usize& history_scroll_offset);

[[nodiscard]] ui::RidePromptModel build_ride_prompt_model(
    RidePromptModelInput input);

[[nodiscard]] ui::GameplayUiModel build_gameplay_ui_model(
    GameplayUiModelInput input);

[[nodiscard]] ui::GameplayUiModel build_gameplay_ui_model_from_snapshots(
    GameplayUiSnapshotModelInput input);

} // namespace arv::client
