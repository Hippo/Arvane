#include "desktop_frame_loop.hpp"

#include "asset_helpers.hpp"
#include "desktop_input_routing.hpp"
#include "desktop_skin_textures.hpp"
#include "ui_model.hpp"

#include "arv/client/command_chat.hpp"
#include "arv/player/player_constants.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace arv::desktop {
namespace {

[[nodiscard]] u64 ui_cache_hash_append_byte(u64 hash, u8 value) noexcept {
  constexpr u64 Prime = 1099511628211ULL;
  hash ^= value;
  hash *= Prime;
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_u64(u64 hash, u64 value) noexcept {
  for (usize index = 0; index < sizeof(value); ++index) {
    hash = ui_cache_hash_append_byte(
        hash,
        static_cast<u8>((value >> (index * 8U)) & 0xFFU));
  }
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_bool(u64 hash, bool value) noexcept {
  return ui_cache_hash_append_byte(hash, value ? 1U : 0U);
}

[[nodiscard]] u64 ui_cache_hash_append_color(u64 hash, arv::text::RgbColor color) noexcept {
  hash = ui_cache_hash_append_byte(hash, color.r);
  hash = ui_cache_hash_append_byte(hash, color.g);
  hash = ui_cache_hash_append_byte(hash, color.b);
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_string(u64 hash, std::string_view value) noexcept {
  hash = ui_cache_hash_append_u64(hash, value.size());
  for (char ch : value) {
    hash = ui_cache_hash_append_byte(hash, static_cast<u8>(ch));
  }
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_f32(u64 hash, f32 value) noexcept {
  const auto quantized =
      static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 100.0));
  return ui_cache_hash_append_u64(hash, static_cast<u64>(quantized));
}

[[nodiscard]] u32 ui_cache_dimension(f32 value) noexcept {
  const auto rounded = std::llround(static_cast<double>(value));
  return rounded <= 0 ? 0U : static_cast<u32>(rounded);
}

[[nodiscard]] u64 ui_cache_hash_append_item_stack(
    u64 hash,
    const ui::UiItemStackView& stack) noexcept {
  hash = ui_cache_hash_append_string(hash, stack.item_name);
  hash = ui_cache_hash_append_string(hash, stack.display_name);
  hash = ui_cache_hash_append_u64(hash, stack.count);
  hash = ui_cache_hash_append_u64(hash, stack.durability);
  hash = ui_cache_hash_append_u64(hash, stack.max_durability);
  hash = ui_cache_hash_append_u64(hash, static_cast<u64>(stack.render_type));
  hash = ui_cache_hash_append_bool(hash, stack.texture_icon.valid);
  hash = ui_cache_hash_append_u64(hash, stack.texture_icon.page);
  hash = ui_cache_hash_append_u64(hash, stack.block_model_quads.size());
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_optional_item_stack(
    u64 hash,
    const std::optional<ui::UiItemStackView>& stack) noexcept {
  hash = ui_cache_hash_append_bool(hash, stack.has_value());
  return stack.has_value() ? ui_cache_hash_append_item_stack(hash, stack.value()) : hash;
}

[[nodiscard]] std::optional<std::pair<usize, usize>> chat_run_from_ui_action(
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
  auto parse_number = [](std::string_view text) -> std::optional<usize> {
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
  };
  std::optional<usize> line =
      parse_number(id.substr(prefix.size(), middle_pos - prefix.size()));
  std::optional<usize> run =
      parse_number(id.substr(middle_pos + middle.size()));
  if (!line.has_value() || !run.has_value()) {
    return std::nullopt;
  }
  return std::pair<usize, usize>{line.value(), run.value()};
}

[[nodiscard]] bool safe_url_scheme(std::string_view url) noexcept {
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

[[nodiscard]] std::optional<f32> passive_chat_opacity(
    const client::ChatHistoryEntry& entry,
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
    const client::ChatHistoryEntry& entry,
    f32 opacity) {
  ui::ChatLineView line;
  line.text = arv::text::to_plain_text(entry.message);
  line.opacity = opacity;
  line.runs = arv::text::flatten_rich_text(entry.message);
  return line;
}

[[nodiscard]] u64 gameplay_ui_signature(
    const DesktopGameSession& session,
    const platform::InputSnapshot& platform_input,
    u32 displayed_fps,
    std::string_view hotbar_item_popup_name,
    f32 hotbar_item_popup_alpha) noexcept {
  constexpr u64 OffsetBasis = 1469598103934665603ULL;
  u64 hash = OffsetBasis;
  hash = ui_cache_hash_append_u64(
      hash,
      static_cast<u64>(reinterpret_cast<std::uintptr_t>(session.client.get())));
  if (session.client == nullptr) {
    return hash;
  }

  hash = ui_cache_hash_append_u64(hash, session.client->selected_hotbar_slot());
  hash = ui_cache_hash_append_bool(hash, session.client->inventory_open());
  hash = ui_cache_hash_append_bool(hash, session.client->chat_open());
  hash = ui_cache_hash_append_bool(hash, session.client->tab_menu_visible());
  hash = ui_cache_hash_append_string(hash, session.client->chat_draft());
  hash = ui_cache_hash_append_u64(hash, session.client->chat_draft_cursor());
  hash = ui_cache_hash_append_u64(hash, session.client->chat_draft_selection_start());
  hash = ui_cache_hash_append_u64(hash, session.client->chat_draft_selection_end());
  hash = ui_cache_hash_append_string(hash, hotbar_item_popup_name);
  hash = ui_cache_hash_append_f32(hash, hotbar_item_popup_alpha);
  hash = ui_cache_hash_append_u64(hash, displayed_fps);
  if (const std::optional<EntityNetID> local_entity = session.client->local_entity_id();
      local_entity.has_value()) {
    const client::ReplicatedEntityMirror* mirror =
        session.client->find_replicated_entity(local_entity.value());
    const bool has_health = mirror != nullptr && mirror->has_health;
    hash = ui_cache_hash_append_bool(hash, has_health);
    if (has_health) {
      hash = ui_cache_hash_append_f32(hash, mirror->health_current);
      hash = ui_cache_hash_append_f32(hash, mirror->health_max);
      hash = ui_cache_hash_append_bool(hash, mirror->dead);
    }
  } else {
    hash = ui_cache_hash_append_bool(hash, false);
  }

  hash = ui_cache_hash_append_u64(hash, session.inventory_slots.size());
  for (const ui::UiItemStackView& slot : session.inventory_slots) {
    hash = ui_cache_hash_append_item_stack(hash, slot);
  }
  hash = ui_cache_hash_append_optional_item_stack(hash, session.carried_inventory_stack);
  if (session.client->inventory_open()) {
    hash = ui_cache_hash_append_f32(hash, platform_input.cursor_x);
    hash = ui_cache_hash_append_f32(hash, platform_input.cursor_y);
  }

  hash = ui_cache_hash_append_u64(hash, session.chat_lines.size());
  for (const ui::ChatLineView& line : session.chat_lines) {
    hash = ui_cache_hash_append_string(hash, line.text);
    hash = ui_cache_hash_append_f32(hash, line.opacity);
    hash = ui_cache_hash_append_u64(hash, line.runs.size());
    for (const arv::text::RichTextRun& run : line.runs) {
      hash = ui_cache_hash_append_string(hash, run.text);
      hash = ui_cache_hash_append_color(hash, run.style.color);
      hash = ui_cache_hash_append_bool(hash, run.style.bold);
      hash = ui_cache_hash_append_bool(hash, run.style.italic);
      hash = ui_cache_hash_append_bool(hash, run.style.underlined);
      hash = ui_cache_hash_append_bool(hash, run.style.strikethrough);
      hash = ui_cache_hash_append_bool(hash, run.click_event.has_value());
      hash = ui_cache_hash_append_bool(hash, run.hover_event.has_value());
    }
  }
  hash = ui_cache_hash_append_bool(
      hash,
      session.client->pending_chat_url_confirmation().has_value());
  if (session.client->pending_chat_url_confirmation().has_value()) {
    hash = ui_cache_hash_append_string(
        hash,
        session.client->pending_chat_url_confirmation().value());
  }

  hash = ui_cache_hash_append_u64(hash, session.tab_players.size());
  for (const ui::PlayerListEntryView& player : session.tab_players) {
    hash = ui_cache_hash_append_string(hash, player.name);
    hash = ui_cache_hash_append_bool(hash, player.latency_ms.has_value());
    if (player.latency_ms.has_value()) {
      hash = ui_cache_hash_append_u64(hash, player.latency_ms.value());
    }
  }
  return hash;
}

[[nodiscard]] render::RenderView menu_player_preview_view() noexcept {
  render::RenderView view;
  view.position = {0.0F, 1.04F, -4.15F};
  view.forward = normalize_or_zero(Vec3f{0.0F, -0.015F, 1.0F});
  view.right = normalize_or_zero(cross(Vec3f{0.0F, 1.0F, 0.0F}, view.forward));
  view.up = normalize_or_zero(cross(view.forward, view.right));
  view.near_plane = 0.05F;
  view.far_plane = 16.0F;
  view.fov_y_degrees = 30.0F;
  return view;
}

[[nodiscard]] render::SkyUniforms menu_player_preview_sky() noexcept {
  render::SkyUniforms sky;
  sky.daylight_color = {1.0F, 1.0F, 1.0F};
  sky.daylight_strength = 1.0F;
  sky.ambient_color = {1.0F, 1.0F, 1.0F};
  return sky;
}

} // namespace

void DesktopFrameLoop::append_menu_player_skin_preview(
    render::FramePacket& frame,
    const ui::UIContext& menu_context) {
  const ui::UIScreen* screen =
      menu_context.screens == nullptr ? nullptr : menu_context.screens->current();
  if (screen == nullptr) {
    return;
  }
  std::optional<ui::PlayerSkinPreviewRequest> request =
      screen->player_skin_preview(menu_context);
  if (!request.has_value()) {
    return;
  }
  const render::EntityModelID preview_model =
      request->slim_model &&
              assets_.entity_render_assets.player_slim_skin_model.value != 0U
          ? assets_.entity_render_assets.player_slim_skin_model
          : assets_.entity_render_assets.player_skin_model;
  if (preview_model.value == 0U) {
    return;
  }
  const ui::UiElement* preview_element =
      ui_tree_.find_by_stable_id(request->element_id);
  if (preview_element == nullptr ||
      preview_element->rect.width <= 0.0F ||
      preview_element->rect.height <= 0.0F) {
    return;
  }

  std::string skin_hash = request->skin_hash;
  const std::string default_skin_hash =
      request->slim_model ? std::string{DefaultMenuPlayerSlimSkinKey}
                          : std::string{DefaultMenuPlayerSkinKey};
  if (skin_hash.empty()) {
    skin_hash = default_skin_hash;
  }
  auto skin_region = player_skin_regions_.find(skin_hash);
  if (skin_region == player_skin_regions_.end() || !skin_region->second.valid()) {
    skin_region = player_skin_regions_.find(default_skin_hash);
  }
  if (skin_region == player_skin_regions_.end() || !skin_region->second.valid()) {
    skin_region = player_skin_regions_.find(std::string{DefaultMenuPlayerSkinKey});
  }
  if (skin_region == player_skin_regions_.end() || !skin_region->second.valid()) {
    return;
  }

  const ui::UiRect preview_rect =
      ui_metrics_.framebuffer_from_logical(preview_element->rect);
  render::EntityPreviewDrawItem preview;
  preview.viewport_min = {preview_rect.x, preview_rect.y};
  preview.viewport_size = {preview_rect.width, preview_rect.height};
  preview.view = menu_player_preview_view();
  preview.sky = menu_player_preview_sky();
  preview.clear_color = {0.018F, 0.023F, 0.030F, 1.0F};

  render::EntityDrawItem player;
  player.model_id = preview_model;
  player.position = {0.0F, -0.02F, 0.0F};
  player.rotation = model_forward_yaw_rotation(request->yaw_radians + PiRadians);
  player.scale = 1.0F;
  player.player_skin_region = skin_region->second;
  preview.entities.push_back(std::move(player));
  frame.entity_previews.push_back(std::move(preview));
}

void DesktopFrameLoop::build_ui(
    render::FramePacket& frame,
    const platform::InputSnapshot& platform_input,
    ui::UIContext& menu_context,
    ui::UIScreenManager& screens,
    DesktopGameSession* active_session,
    bool has_active_session,
    bool has_active_integrated_session,
    bool fixed_tick_ran,
    std::chrono::steady_clock::time_point now) {
  ui_tree_.set_theme(&ui_theme_);
  if (has_active_session) {
    update_gameplay_ui_snapshots(
        *active_session,
        has_active_integrated_session,
        fixed_tick_ran,
        now);
    update_hotbar_item_popup(*active_session, now);
  } else {
    hotbar_item_observation_initialized_ = false;
    hotbar_item_popup_name_.clear();
  }

  menu_context.viewport = ui_metrics_.logical_size;
  menu_context.screens = &screens;

  const UiBuildCacheKey next_cache_key{
      ui_cache_dimension(ui_metrics_.logical_size.x),
      ui_cache_dimension(ui_metrics_.logical_size.y),
      has_active_session,
      gameplay_menu_stack_active_,
      screens.has_active_screen(),
      screens.revision(),
      has_active_session
          ? gameplay_ui_signature(
                *active_session,
                platform_input,
                displayed_fps_,
                hotbar_item_popup_name_,
                hotbar_item_popup_alpha(now))
          : 0U};
  const bool rebuild_ui_tree =
      !ui_tree_cache_valid_ || !(next_cache_key == ui_tree_cache_key_);
  if (rebuild_ui_tree) {
    if (has_active_session) {
      build_gameplay_ui(*active_session, platform_input, now);
    } else {
      ui_tree_.clear();
    }

    if (screens.has_active_screen()) {
      screens.append_to(menu_context);
    }
    ui_tree_.layout_with_metrics(ui_metrics_, &ui_text_measurer_);
    ui_tree_cache_key_ = next_cache_key;
    ui_tree_cache_valid_ = true;
  }
  const std::optional<ui::UiAction> hover_action =
      ui_tree_.route_mouse_move({platform_input.cursor_x, platform_input.cursor_y});
  const bool added_inventory_tooltip =
      has_active_session &&
      append_inventory_hover_tooltip_if_needed(
          *active_session,
          platform_input,
          hover_action);
  const bool added_chat_tooltip =
      has_active_session &&
      append_chat_hover_tooltip_if_needed(
          *active_session,
          platform_input,
          hover_action);
  if (added_inventory_tooltip || added_chat_tooltip) {
    ui_tree_.layout_with_metrics(ui_metrics_, &ui_text_measurer_);
    (void)ui_tree_.route_mouse_move(
        {platform_input.cursor_x, platform_input.cursor_y});
  }
  append_menu_player_skin_preview(frame, menu_context);
  ui::append_ui_draw_commands(
      ui_tree_,
      assets_.font_system,
      assets_.ui_font_stacks,
      frame,
      ui_metrics_,
      std::chrono::duration<f32>(now - app_start_time_).count());
  if (assets_.font_system.atlas_dirty()) {
    renderer_.set_ui_font_atlas(
        texture_array_upload_from_image(assets_.font_system.atlas_image()));
    assets_.font_system.clear_atlas_dirty();
  }
}

void DesktopFrameLoop::update_gameplay_ui_snapshots(
    DesktopGameSession& active_session,
    bool has_active_integrated_session,
    bool fixed_tick_ran,
    std::chrono::steady_clock::time_point now) {
  if (!active_session.ui_snapshot_initialized || fixed_tick_ran) {
    sessions_.refresh_inventory_slots(active_session);
    if (has_active_integrated_session && active_session.client->tab_menu_visible()) {
      active_session.tab_players =
          player_list_for_ui(*active_session.integrated_server);
    } else {
      active_session.tab_players.clear();
    }
    active_session.ui_snapshot_initialized = true;
  } else if (has_active_integrated_session &&
             active_session.client->tab_menu_visible() &&
             active_session.tab_players.empty()) {
    active_session.tab_players =
        player_list_for_ui(*active_session.integrated_server);
  } else if (!active_session.client->tab_menu_visible() &&
             !active_session.tab_players.empty()) {
    active_session.tab_players.clear();
  }

  active_session.chat_lines.clear();
  const std::span<const client::ChatHistoryEntry> chat_history =
      active_session.client->chat_history();
  const io::UiConfig::ChatConfig& chat_config = config_.ui.chat;
  if (active_session.client->chat_open()) {
    const usize max_lines = std::max<usize>(1U, chat_config.open_max_lines);
    const usize start =
        chat_history.size() > max_lines ? chat_history.size() - max_lines : 0U;
    for (usize index = start; index < chat_history.size(); ++index) {
      active_session.chat_lines.emplace_back(
          chat_line_view_from_entry(chat_history[index], 1.0F));
    }
  } else if (chat_config.passive_max_lines > 0U) {
    for (usize reverse_index = chat_history.size(); reverse_index > 0U; --reverse_index) {
      const client::ChatHistoryEntry& entry = chat_history[reverse_index - 1U];
      const std::optional<f32> opacity =
          passive_chat_opacity(entry, now, chat_config);
      if (!opacity.has_value()) {
        continue;
      }
      active_session.chat_lines.emplace_back(
          chat_line_view_from_entry(entry, opacity.value()));
      if (active_session.chat_lines.size() >= chat_config.passive_max_lines) {
        break;
      }
    }
    std::reverse(active_session.chat_lines.begin(), active_session.chat_lines.end());
  }
}

void DesktopFrameLoop::update_hotbar_item_popup(
    DesktopGameSession& active_session,
    std::chrono::steady_clock::time_point now) {
  if (active_session.client == nullptr) {
    hotbar_item_observation_initialized_ = false;
    hotbar_item_popup_name_.clear();
    return;
  }

  const u8 selected_slot = active_session.client->selected_hotbar_slot();
  std::string selected_item_name;
  if (selected_slot < active_session.inventory_slots.size()) {
    const ui::UiItemStackView& stack = active_session.inventory_slots[selected_slot];
    if (!stack.empty()) {
      selected_item_name =
          stack.display_name.empty() ? stack.item_name : stack.display_name;
    }
  }

  if (!hotbar_item_observation_initialized_) {
    observed_hotbar_slot_ = selected_slot;
    observed_hotbar_item_name_ = std::move(selected_item_name);
    hotbar_item_observation_initialized_ = true;
    return;
  }

  if (selected_slot == observed_hotbar_slot_ &&
      selected_item_name == observed_hotbar_item_name_) {
    return;
  }

  observed_hotbar_slot_ = selected_slot;
  observed_hotbar_item_name_ = selected_item_name;
  if (selected_item_name.empty()) {
    hotbar_item_popup_name_.clear();
    hotbar_item_popup_expires_at_ = now;
    return;
  }

  hotbar_item_popup_name_ = std::move(selected_item_name);
  hotbar_item_popup_expires_at_ = now + HotbarItemPopupVisibleDuration;
}

f32 DesktopFrameLoop::hotbar_item_popup_alpha(
    std::chrono::steady_clock::time_point now) const noexcept {
  if (hotbar_item_popup_name_.empty() || now >= hotbar_item_popup_expires_at_) {
    return 0.0F;
  }

  const auto remaining = hotbar_item_popup_expires_at_ - now;
  if (remaining >= HotbarItemPopupFadeDuration) {
    return 1.0F;
  }
  const auto remaining_ms =
      std::chrono::duration<f32, std::milli>(remaining).count();
  const auto fade_ms =
      std::chrono::duration<f32, std::milli>(HotbarItemPopupFadeDuration).count();
  return std::clamp(remaining_ms / fade_ms, 0.0F, 1.0F);
}

bool DesktopFrameLoop::append_inventory_hover_tooltip_if_needed(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input,
    const std::optional<ui::UiAction>& hover_action) {
  if (active_session.client == nullptr ||
      !active_session.client->inventory_open() ||
      (active_session.carried_inventory_stack.has_value() &&
       !active_session.carried_inventory_stack->empty()) ||
      ui_tree_.find_by_stable_id("inventory.tooltip") != nullptr ||
      !hover_action.has_value() ||
      !std::string_view{hover_action->element_id}.starts_with("inventory.slot.")) {
    return false;
  }

  const std::optional<usize> slot = inventory_slot_from_ui_action(*hover_action);
  if (!slot.has_value() || slot.value() >= active_session.inventory_slots.size()) {
    return false;
  }

  const ui::UiItemStackView& stack = active_session.inventory_slots[slot.value()];
  if (stack.empty()) {
    return false;
  }
  std::string tooltip_text =
      stack.display_name.empty() ? stack.item_name : stack.display_name;
  if (tooltip_text.empty()) {
    return false;
  }

  ui::append_item_tooltip(
      ui_tree_,
      {
          true,
          std::move(tooltip_text),
          ui_metrics_.logical_size,
          {
              platform_input.cursor_x,
              platform_input.cursor_y,
          },
      });
  return true;
}

bool DesktopFrameLoop::append_chat_hover_tooltip_if_needed(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input,
    const std::optional<ui::UiAction>& hover_action) {
  if (active_session.client == nullptr ||
      !active_session.client->chat_open() ||
      ui_tree_.find_by_stable_id("chat.tooltip") != nullptr ||
      !hover_action.has_value()) {
    return false;
  }
  const std::optional<std::pair<usize, usize>> run_id =
      chat_run_from_ui_action(hover_action.value());
  if (!run_id.has_value() ||
      run_id->first >= active_session.chat_lines.size() ||
      run_id->second >= active_session.chat_lines[run_id->first].runs.size()) {
    return false;
  }
  const arv::text::RichTextRun& run =
      active_session.chat_lines[run_id->first].runs[run_id->second];
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
      ui_tree_,
      {
          true,
          tooltip,
          ui_metrics_.logical_size,
          {
              platform_input.cursor_x,
              platform_input.cursor_y,
          },
      });
  return true;
}

bool DesktopFrameLoop::route_chat_mouse_input(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input) {
  if (active_session.client == nullptr) {
    return false;
  }

  bool handled = false;
  const Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree_.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<ui::UiAction> action =
        ui_tree_.route_mouse_down(cursor, ui::UiMouseButton::Left);
    if (action.has_value() && action->action_id == "chat_url_cancel") {
      active_session.client->cancel_chat_url_confirmation();
      handled = true;
    } else if (action.has_value() && action->action_id == "chat_url_confirm") {
      // TODO(arvane): Open the confirmed URL through a platform browser abstraction
      // once one exists. Confirmation is intentionally wired before any native open.
      active_session.client->acknowledge_chat_url_open_unavailable();
      handled = true;
    } else if (action.has_value()) {
      const std::optional<std::pair<usize, usize>> run_id =
          chat_run_from_ui_action(action.value());
      if (run_id.has_value() &&
          run_id->first < active_session.chat_lines.size() &&
          run_id->second < active_session.chat_lines[run_id->first].runs.size()) {
        const arv::text::RichTextRun& run =
            active_session.chat_lines[run_id->first].runs[run_id->second];
        if (run.click_event.has_value()) {
          const arv::text::ClickEvent& click = run.click_event.value();
          switch (click.action) {
            case arv::text::ClickAction::SuggestCommand:
              active_session.client->suggest_chat_command(click.value);
              handled = true;
              break;
            case arv::text::ClickAction::OpenUrl:
              if (safe_url_scheme(click.value)) {
                active_session.client->request_chat_url_confirmation(click.value);
              } else {
                active_session.client->add_chat_message(
                    assets_.language.text("ui.chat.blocked_unsafe_url"));
              }
              handled = true;
              break;
            case arv::text::ClickAction::RunCommand:
              client::append_command_result_to_chat(
                  *active_session.client,
                  execute_chat_command(active_session, click.value),
                  assets_.language);
              handled = true;
              break;
            case arv::text::ClickAction::CopyToClipboard:
              // TODO(arvane): Route through a platform clipboard abstraction when
              // one exists outside debug ImGui tooling.
              active_session.client->add_chat_message(
                  assets_.language.text("ui.chat.copy_unavailable"));
              handled = true;
              break;
          }
        }
      }
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree_.route_mouse_up(cursor, ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_tree_.route_mouse_down(cursor, ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree_.route_mouse_up(cursor, ui::UiMouseButton::Right);
  }
  return handled;
}

void DesktopFrameLoop::build_gameplay_ui(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input,
    std::chrono::steady_clock::time_point now) {
  const f32 item_popup_alpha = hotbar_item_popup_alpha(now);
  ui::HealthHudModel health;
  health.viewport = ui_metrics_.logical_size;
  if (const std::optional<EntityNetID> local_entity =
          active_session.client->local_entity_id();
      local_entity.has_value()) {
    const client::ReplicatedEntityMirror* mirror =
        active_session.client->find_replicated_entity(local_entity.value());
    if (mirror != nullptr && mirror->has_health) {
      health.visible = true;
      health.current = mirror->health_current;
      health.max = mirror->health_max;
      health.section_value = 1.0F;
    }
  }

  ui::build_gameplay_ui(
      ui_tree_,
      {
          {
              active_session.client->selected_hotbar_slot(),
              player::DefaultHotbarSlotCount,
              ui_metrics_.logical_size,
              std::span<const ui::UiItemStackView>{
                  active_session.inventory_slots.data(),
                  std::min<usize>(
                      active_session.inventory_slots.size(),
                      player::DefaultHotbarSlotCount)},
              item_popup_alpha > 0.0F ? hotbar_item_popup_name_ : std::string{},
              item_popup_alpha,
          },
          {
              active_session.client->inventory_open(),
              assets_.language.text("ui.inventory.title"),
              active_session.client->selected_hotbar_slot(),
              player::DefaultHotbarSlotCount,
              ui_metrics_.logical_size,
              std::span<const ui::UiItemStackView>{
                  active_session.inventory_slots.data(),
                  active_session.inventory_slots.size()},
              active_session.carried_inventory_stack,
              {
                  platform_input.cursor_x,
                  platform_input.cursor_y,
              },
          },
          {
              true,
              active_session.client->chat_open(),
              std::span<const ui::ChatLineView>{
                  active_session.chat_lines.data(),
                  active_session.chat_lines.size()},
              active_session.client->chat_draft(),
              active_session.client->chat_draft_cursor(),
              active_session.client->chat_draft_selection_start(),
              active_session.client->chat_draft_selection_end(),
              active_session.client->pending_chat_url_confirmation(),
              active_session.client->chat_open() ? config_.ui.chat.open_max_lines
                                                 : config_.ui.chat.passive_max_lines,
              &assets_.language,
          },
          {
              active_session.client->tab_menu_visible(),
              assets_.language.text("ui.tab.players"),
              "",
              std::span<const ui::PlayerListEntryView>{
                  active_session.tab_players.data(),
                  active_session.tab_players.size()},
          },
          {},
          {true, displayed_fps_},
          health,
      });
}

} // namespace arv::desktop
