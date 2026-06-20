#include "arv/client/gameplay_ui_model.hpp"

#include "arv/player/player_constants.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>

namespace arv::client {
namespace {

[[nodiscard]] std::optional<f32> passive_chat_opacity(
    const ChatHistoryEntry& entry,
    std::chrono::steady_clock::time_point now,
    const io::UiConfig::ChatConfig& config) noexcept {
  const f32 age_seconds = std::max(
      0.0F,
      std::chrono::duration<f32>(now - entry.received_at).count());
  if (age_seconds <= config.passive_visible_seconds) {
    return 1.0F;
  }
  if (config.passive_fade_seconds <= 0.0F) {
    return std::nullopt;
  }
  const f32 fade_progress =
      (age_seconds - config.passive_visible_seconds) / config.passive_fade_seconds;
  if (fade_progress >= 1.0F) {
    return std::nullopt;
  }
  return std::clamp(1.0F - fade_progress, 0.0F, 1.0F);
}

[[nodiscard]] ui::ChatLineView chat_line_view_from_entry(
    const ChatHistoryEntry& entry,
    f32 opacity) {
  ui::ChatLineView line;
  line.text = arv::text::to_plain_text(entry.message);
  line.opacity = opacity;
  line.runs = arv::text::flatten_rich_text(entry.message);
  return line;
}

[[nodiscard]] ui::HealthHudModel health_model_for_client(
    const ClientRuntime& client,
    Vec2f viewport) {
  ui::HealthHudModel health;
  health.viewport = viewport;
  if (const std::optional<EntityNetID> local_entity = client.local_entity_id();
      local_entity.has_value()) {
    const ReplicatedEntityMirror* mirror =
        client.find_replicated_entity(local_entity.value());
    if (mirror != nullptr && mirror->has_health) {
      health.visible = true;
      health.current = mirror->health_current;
      health.max = mirror->health_max;
      health.section_value = 1.0F;
    }
  }
  return health;
}

} // namespace

usize gameplay_chat_history_max_scroll_offset(
    usize history_size,
    usize max_visible_messages) noexcept {
  const usize visible = std::max<usize>(1U, max_visible_messages);
  return history_size > visible ? history_size - visible : 0U;
}

usize gameplay_chat_scroll_steps(f32 wheel_y) noexcept {
  if (wheel_y == 0.0F || !std::isfinite(wheel_y)) {
    return 0U;
  }
  return static_cast<usize>(std::max(1.0F, std::ceil(std::abs(wheel_y))));
}

void refresh_gameplay_chat_lines(
    std::vector<ui::ChatLineView>& chat_lines,
    const ClientRuntime& client,
    const io::UiConfig::ChatConfig& chat_config,
    std::chrono::steady_clock::time_point now,
    usize& history_scroll_offset) {
  chat_lines.clear();
  const std::span<const ChatHistoryEntry> chat_history = client.chat_history();
  if (client.chat_open()) {
    const usize max_lines = std::max<usize>(1U, chat_config.open_max_lines);
    history_scroll_offset = std::min(
        history_scroll_offset,
        gameplay_chat_history_max_scroll_offset(chat_history.size(), max_lines));
    chat_lines.reserve(chat_history.size());
    for (usize index = 0U; index < chat_history.size(); ++index) {
      chat_lines.emplace_back(chat_line_view_from_entry(chat_history[index], 1.0F));
    }
    return;
  }

  history_scroll_offset = 0U;
  if (chat_config.passive_max_lines == 0U) {
    return;
  }
  for (usize reverse_index = chat_history.size(); reverse_index > 0U; --reverse_index) {
    const ChatHistoryEntry& entry = chat_history[reverse_index - 1U];
    const std::optional<f32> opacity =
        passive_chat_opacity(entry, now, chat_config);
    if (!opacity.has_value()) {
      continue;
    }
    chat_lines.emplace_back(chat_line_view_from_entry(entry, opacity.value()));
    if (chat_lines.size() >= chat_config.passive_max_lines) {
      break;
    }
  }
  std::reverse(chat_lines.begin(), chat_lines.end());
}

ui::RidePromptModel build_ride_prompt_model(RidePromptModelInput input) {
  ui::RidePromptModel model;
  model.viewport = input.viewport;
  if (!input.interactions_available ||
      input.gameplay_blocked ||
      input.inventory_open ||
      input.chat_open ||
      input.local_player_mounted ||
      input.key_label.empty() ||
      !input.has_targeted_rideable) {
    return model;
  }
  if (!input.has_targeted_rideable()) {
    return model;
  }
  model.visible = true;
  model.key_label = std::string{input.key_label};
  return model;
}

ui::GameplayUiModel build_gameplay_ui_model(GameplayUiModelInput input) {
  const bool chat_open = input.client.chat_open();
  ui::ChatUiModel chat_model{
      true,
      chat_open,
      input.chat_lines,
      input.client.chat_draft(),
      input.client.chat_draft_cursor(),
      input.client.chat_draft_selection_start(),
      input.client.chat_draft_selection_end(),
      input.client.pending_chat_url_confirmation(),
      chat_open ? input.chat_config.open_max_lines : input.chat_config.passive_max_lines,
      input.language,
  };
  chat_model.history_scroll_offset =
      chat_open ? input.chat_history_scroll_offset : 0U;

  const usize hotbar_slot_count =
      std::min<usize>(input.inventory_slots.size(), player::DefaultHotbarSlotCount);

  const bool container_open = input.client.container_open() &&
                              input.container_layout != nullptr;
  ui::CrosshairHudModel crosshair = std::move(input.crosshair);
  crosshair.viewport = input.viewport;
  crosshair.visible = crosshair.visible && !input.client.inventory_open() &&
                      !chat_open && !container_open;

  return {
      {
          input.client.selected_hotbar_slot(),
          player::DefaultHotbarSlotCount,
          input.viewport,
          std::span<const ui::UiItemStackView>{
              input.inventory_slots.data(),
              hotbar_slot_count},
          std::string{input.hotbar_item_popup_name},
          input.hotbar_item_popup_alpha,
          !input.client.inventory_open() && !container_open,
      },
      {
          input.client.inventory_open() && !container_open,
          std::string{input.inventory_title},
          input.client.selected_hotbar_slot(),
          player::DefaultHotbarSlotCount,
          input.viewport,
          input.inventory_slots,
          input.carried_inventory_stack,
          input.cursor_position,
      },
      std::move(chat_model),
      {
          input.client.tab_menu_visible(),
          std::string{input.tab_title},
          "",
          input.tab_players,
      },
      {},
      {input.debug_fps_visible, input.displayed_fps},
      health_model_for_client(input.client, input.viewport),
      std::move(input.ride_prompt),
      {
          container_open,
          std::string{input.container_title},
          input.container_layout,
          input.client.selected_hotbar_slot(),
          player::DefaultHotbarSlotCount,
          input.viewport,
          input.container_slots,
          input.inventory_slots,
          input.carried_inventory_stack,
          input.cursor_position,
      },
      std::move(crosshair),
  };
}

ui::GameplayUiModel build_gameplay_ui_model_from_snapshots(
    GameplayUiSnapshotModelInput input) {
  const player::ContainerUiLayoutDefinition* container_layout =
      input.client.container_open() && input.container_layouts != nullptr
          ? input.container_layouts->find(input.client.active_container_layout())
          : nullptr;

  std::string inventory_title;
  if (!input.inventory_title.empty()) {
    inventory_title = std::move(input.inventory_title);
  } else if (input.language != nullptr) {
    inventory_title = input.language->text("ui.inventory.title", "Inventory");
  } else {
    inventory_title = "Inventory";
  }

  std::string tab_title;
  if (!input.tab_title.empty()) {
    tab_title = std::move(input.tab_title);
  } else if (input.language != nullptr) {
    tab_title = input.language->text("ui.tab.players", "Players");
  } else {
    tab_title = "Players";
  }

  input.ride_prompt.viewport = input.viewport;
  const std::string_view hotbar_item_popup_name =
      input.hotbar_item_popup_alpha > 0.0F
          ? input.hotbar_item_popup_name
          : std::string_view{};

  return build_gameplay_ui_model(
      {
          .client = input.client,
          .chat_config = input.chat_config,
          .inventory_slots = input.inventory_slots,
          .container_slots = input.container_slots,
          .carried_inventory_stack = std::move(input.carried_inventory_stack),
          .chat_lines = input.chat_lines,
          .chat_history_scroll_offset = input.chat_history_scroll_offset,
          .tab_players = input.tab_players,
          .inventory_title = inventory_title,
          .container_title = input.client.active_container_title(),
          .container_layout = container_layout,
          .tab_title = tab_title,
          .viewport = input.viewport,
          .cursor_position = input.cursor_position,
          .displayed_fps = input.displayed_fps,
          .hotbar_item_popup_name = hotbar_item_popup_name,
          .hotbar_item_popup_alpha = input.hotbar_item_popup_alpha,
          .crosshair = std::move(input.crosshair),
          .ride_prompt = std::move(input.ride_prompt),
          .language = input.language,
          .debug_fps_visible = input.debug_fps_visible,
      });
}

} // namespace arv::client
