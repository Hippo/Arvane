#include "desktop_frame_loop.hpp"

#include "asset_helpers.hpp"
#include "desktop_skin_textures.hpp"
#include "ui_model.hpp"

#include "arv/player/player_constants.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
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

[[nodiscard]] u64 gameplay_ui_signature(
    const DesktopGameSession& session,
    const platform::InputSnapshot& platform_input,
    u32 displayed_fps) noexcept {
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
  const ui::UIScreen* screen = screen_manager_.current();
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

  render::EntityPreviewDrawItem preview;
  preview.viewport_min = {preview_element->rect.x, preview_element->rect.y};
  preview.viewport_size = {preview_element->rect.width, preview_element->rect.height};
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
    DesktopGameSession* active_session,
    bool has_active_session,
    bool has_active_integrated_session,
    bool fixed_tick_ran) {
  ui_tree_.set_theme(&ui_theme_);
  if (has_active_session) {
    update_gameplay_ui_snapshots(
        *active_session,
        has_active_integrated_session,
        fixed_tick_ran);
  }

  menu_context.viewport = {
      static_cast<f32>(framebuffer_.width),
      static_cast<f32>(framebuffer_.height),
  };
  menu_context.screens = &screen_manager_;

  const UiBuildCacheKey next_cache_key{
      framebuffer_.width,
      framebuffer_.height,
      has_active_session,
      screen_manager_.has_active_screen(),
      screen_manager_.revision(),
      has_active_session
          ? gameplay_ui_signature(*active_session, platform_input, displayed_fps_)
          : 0U};
  const bool rebuild_ui_tree =
      !ui_tree_cache_valid_ || !(next_cache_key == ui_tree_cache_key_);
  if (rebuild_ui_tree) {
    if (has_active_session) {
      build_gameplay_ui(*active_session, platform_input);
    } else {
      ui_tree_.clear();
    }

    if (screen_manager_.has_active_screen()) {
      screen_manager_.append_to(menu_context);
    }
    ui_tree_.layout(
        {
            static_cast<f32>(framebuffer_.width),
            static_cast<f32>(framebuffer_.height),
        },
        &ui_text_measurer_);
    ui_tree_cache_key_ = next_cache_key;
    ui_tree_cache_valid_ = true;
  }
  (void)ui_tree_.route_mouse_move({platform_input.cursor_x, platform_input.cursor_y});
  append_menu_player_skin_preview(frame, menu_context);
  ui::append_ui_draw_commands(
      ui_tree_,
      assets_.font_system,
      assets_.ui_font_stacks,
      frame);
  if (assets_.font_system.atlas_dirty()) {
    renderer_.set_ui_font_atlas(
        texture_array_upload_from_image(assets_.font_system.atlas_image()));
    assets_.font_system.clear_atlas_dirty();
  }
}

void DesktopFrameLoop::update_gameplay_ui_snapshots(
    DesktopGameSession& active_session,
    bool has_active_integrated_session,
    bool fixed_tick_ran) {
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
  for (const std::string& message : active_session.client->chat_messages()) {
    active_session.chat_lines.emplace_back(message, 1.0F);
  }
}

void DesktopFrameLoop::build_gameplay_ui(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input) {
  ui::HealthHudModel health;
  health.viewport = {
      static_cast<f32>(framebuffer_.width),
      static_cast<f32>(framebuffer_.height),
  };
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
              {
                  static_cast<f32>(framebuffer_.width),
                  static_cast<f32>(framebuffer_.height),
              },
              std::span<const ui::UiItemStackView>{
                  active_session.inventory_slots.data(),
                  std::min<usize>(
                      active_session.inventory_slots.size(),
                      player::DefaultHotbarSlotCount)},
          },
          {
              active_session.client->inventory_open(),
              "Inventory",
              active_session.client->selected_hotbar_slot(),
              player::DefaultHotbarSlotCount,
              {
                  static_cast<f32>(framebuffer_.width),
                  static_cast<f32>(framebuffer_.height),
              },
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
          },
          {
              active_session.client->tab_menu_visible(),
              "Players",
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
