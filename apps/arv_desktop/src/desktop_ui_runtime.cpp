#include "desktop_frame_loop.hpp"

#include "ui_model.hpp"

#include "arv/client/command_chat.hpp"
#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/client_ui_routing.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/gameplay_ui_frame.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/player_skin_rendering.hpp"
#include "arv/client/ui_interactions.hpp"

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

constexpr f32 LoadingDotCycleSeconds = 1.15F;
constexpr f32 LoadingDotPhaseStep = 0.58F;
constexpr f32 TwoPi = 6.2831853071795864769F;

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

[[nodiscard]] u64 gameplay_ui_signature(
    const DesktopGameSession& session,
    const platform::InputSnapshot& platform_input,
    u32 displayed_fps,
    std::string_view hotbar_item_popup_name,
    f32 hotbar_item_popup_alpha,
    bool ride_prompt_visible,
    std::string_view ride_prompt_key_label,
    std::string_view ride_prompt_suffix,
    f32 ride_prompt_alpha) noexcept {
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
  hash = ui_cache_hash_append_u64(hash, session.chat_history_scroll_offset);
  hash = ui_cache_hash_append_string(hash, hotbar_item_popup_name);
  hash = ui_cache_hash_append_f32(hash, hotbar_item_popup_alpha);
  hash = ui_cache_hash_append_bool(hash, ride_prompt_visible);
  hash = ui_cache_hash_append_string(hash, ride_prompt_key_label);
  hash = ui_cache_hash_append_string(hash, ride_prompt_suffix);
  hash = ui_cache_hash_append_f32(hash, ride_prompt_alpha);
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
  hash = ui_cache_hash_append_bool(hash, session.client->container_open());
  hash = ui_cache_hash_append_string(hash, session.client->active_container_title());
  hash = ui_cache_hash_append_string(hash, session.client->active_container_layout());
  hash = ui_cache_hash_append_u64(hash, session.container_slots.size());
  for (const ui::UiItemStackView& slot : session.container_slots) {
    hash = ui_cache_hash_append_item_stack(hash, slot);
  }
  hash = ui_cache_hash_append_optional_item_stack(hash, session.carried_inventory_stack);
  if (session.client->inventory_open() || session.client->container_open()) {
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

void DesktopFrameLoop::append_loading_screen_ui(
    render::FramePacket& frame,
    DesktopLoadingScreen screen,
    std::chrono::steady_clock::time_point now) {
  const ui::UiTheme& theme = ui_theme_;
  ui::UiTree loading_tree;
  loading_tree.set_theme(&theme);

  ui::UiElement backdrop;
  backdrop.stable_id = "loading.backdrop";
  backdrop.type = ui::UiElementType::Panel;
  backdrop.layout.width_mode = ui::UiSizeMode::Fill;
  backdrop.layout.height_mode = ui::UiSizeMode::Fill;
  backdrop.style.background = theme.colors.background;
  backdrop.hit_test = false;
  loading_tree.add_child(loading_tree.root_id(), std::move(backdrop));

  ui::UiElement stack;
  stack.stable_id = "loading.stack";
  stack.type = ui::UiElementType::VerticalStack;
  stack.layout.anchor = ui::UiAnchor::Center;
  stack.layout.spacing = theme.layout.spacing_lg;
  stack.hit_test = false;
  const ui::UiNodeID stack_id =
      loading_tree.add_child(loading_tree.root_id(), std::move(stack));

  ui::UiElement title;
  title.stable_id = "loading.title";
  title.type = ui::UiElementType::Text;
  title.text = screen == DesktopLoadingScreen::ClosingGame
                   ? assets_.language.text("ui.loading.closing_game")
                   : assets_.language.text("ui.loading.world");
  title.style.foreground = theme.colors.text;
  title.style.text_role = ui::UiTextRole::Title;
  title.layout.anchor = ui::UiAnchor::TopCenter;
  title.hit_test = false;
  loading_tree.add_child(stack_id, std::move(title));

  ui::UiElement dots;
  dots.stable_id = "loading.dots";
  dots.type = ui::UiElementType::HorizontalStack;
  dots.layout.anchor = ui::UiAnchor::TopCenter;
  dots.layout.spacing = theme.layout.spacing_sm;
  dots.hit_test = false;
  const ui::UiNodeID dots_id =
      loading_tree.add_child(stack_id, std::move(dots));

  const f32 seconds =
      std::chrono::duration<f32>(now - app_start_time_).count();
  const f32 dot_size = std::max(8.0F, theme.layout.spacing_sm + 1.0F);
  for (u32 index = 0; index < 3U; ++index) {
    const f32 wave = 0.5F + 0.5F * std::sin(
        (seconds / LoadingDotCycleSeconds) * TwoPi -
        static_cast<f32>(index) * LoadingDotPhaseStep);
    Vec4f color = theme.colors.accent;
    color.w = 0.34F + wave * 0.66F;

    ui::UiElement dot;
    dot.stable_id = "loading.dot." + std::to_string(index);
    dot.type = ui::UiElementType::Panel;
    dot.layout.width_mode = ui::UiSizeMode::Fixed;
    dot.layout.height_mode = ui::UiSizeMode::Fixed;
    dot.layout.width = dot_size;
    dot.layout.height = dot_size;
    dot.style.background = color;
    dot.style.radius = dot_size * 0.5F;
    dot.hit_test = false;
    loading_tree.add_child(dots_id, std::move(dot));
  }

  loading_tree.layout_with_metrics(ui_metrics_, &ui_text_measurer_);
  const auto sync_font_atlas = [this]() -> bool {
    return sync_ui_font_atlas();
  };
  (void)client::append_ui_draw_commands_with_font_sync(
      loading_tree,
      assets_.font_system,
      assets_.ui_font_stacks,
      frame,
      ui_metrics_,
      seconds,
      sync_font_atlas);
}

bool DesktopFrameLoop::sync_ui_font_atlas() {
  const auto upload_font_atlas =
      [this](const render::TextureArrayUploadData& upload) -> bool {
    renderer_.set_ui_font_atlas(upload);
    return true;
  };
  return client::sync_ui_font_atlas(assets_.font_system, upload_font_atlas);
}

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

  std::optional<render::TextureRegionHandle> skin_region =
      client::menu_player_skin_preview_region(
          player_skin_regions_,
          request->skin_hash,
          request->slim_model);
  if (!skin_region.has_value()) {
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
  player.player_skin_region = skin_region.value();
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
    if (active_session->client == nullptr) {
      client::reset_hotbar_item_popup(hotbar_item_popup_);
      client::reset_mount_exit_prompt(mount_exit_prompt_);
    } else {
      client::update_hotbar_item_popup(
          hotbar_item_popup_,
          active_session->client->selected_hotbar_slot(),
          std::span<const ui::UiItemStackView>{
              active_session->inventory_slots.data(),
              active_session->inventory_slots.size()},
          now);
      client::update_mount_exit_prompt(
          mount_exit_prompt_,
          client::local_player_is_mounted(*active_session->client),
          now);
    }
  } else {
    client::reset_hotbar_item_popup(hotbar_item_popup_);
    client::reset_mount_exit_prompt(mount_exit_prompt_);
  }

  menu_context.viewport = ui_metrics_.logical_size;
  menu_context.screens = &screens;

  const bool ride_prompt_client_ready =
      has_active_session && active_session != nullptr &&
      active_session->client != nullptr;
  const std::string ride_prompt_key_label =
      ride_prompt_client_ready
          ? client::first_action_binding_label(
                input_bindings_,
                platform::LogicalAction::Mount)
          : std::string{};
  const auto has_targeted_rideable = [this, active_session]() -> bool {
    return active_session != nullptr &&
           targeted_rideable_entity(*active_session).has_value();
  };
  ui::RidePromptModel ride_prompt =
      client::build_ride_prompt_model(
          {
              .interactions_available =
                  ride_prompt_client_ready && !debug_menu_.open,
              .gameplay_blocked = screens.blocks_gameplay_input(),
              .inventory_open =
                  ride_prompt_client_ready &&
                  active_session->client->inventory_open(),
              .chat_open =
                  ride_prompt_client_ready &&
                  active_session->client->chat_open(),
              .local_player_mounted =
                  ride_prompt_client_ready &&
                  client::local_player_is_mounted(*active_session->client),
              .mount_exit_prompt_alpha =
                  client::mount_exit_prompt_alpha(mount_exit_prompt_, now),
              .key_label = ride_prompt_key_label,
              .viewport = ui_metrics_.logical_size,
              .has_targeted_rideable = has_targeted_rideable,
          });

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
                hotbar_item_popup_.popup_name,
                client::hotbar_item_popup_alpha(hotbar_item_popup_, now),
                ride_prompt.visible,
                ride_prompt.key_label,
                ride_prompt.suffix,
                ride_prompt.alpha)
          : 0U};
  const bool rebuild_ui_tree =
      !ui_tree_cache_valid_ || !(next_cache_key == ui_tree_cache_key_);
  if (rebuild_ui_tree) {
    if (has_active_session) {
      if (screens.has_active_screen()) {
        const auto append_active_screens = [&]() {
          screens.append_to(menu_context);
        };
        build_gameplay_ui(
            *active_session,
            platform_input,
            now,
            std::move(ride_prompt),
            append_active_screens);
      } else {
        build_gameplay_ui(
            *active_session,
            platform_input,
            now,
            std::move(ride_prompt),
            {});
      }
    } else {
      ui_tree_.clear();
      if (screens.has_active_screen()) {
        screens.append_to(menu_context);
      }
      ui_tree_.layout_with_metrics(ui_metrics_, &ui_text_measurer_);
    }
    ui_tree_cache_key_ = next_cache_key;
    ui_tree_cache_valid_ = true;
  }
  const auto sync_font_atlas = [this]() -> bool {
    return sync_ui_font_atlas();
  };
  const auto append_before_ui_draw = [&]() {
    append_menu_player_skin_preview(frame, menu_context);
  };
  (void)client::append_gameplay_ui_frame(
      {
          .tree = &ui_tree_,
          .metrics = &ui_metrics_,
          .text_measurer = &ui_text_measurer_,
          .cursor_position = {platform_input.cursor_x, platform_input.cursor_y},
          .hover = {
              .inventory_open =
                  has_active_session &&
                  active_session != nullptr &&
                  active_session->client != nullptr &&
                  active_session->client->inventory_open(),
              .container_open =
                  has_active_session &&
                  active_session != nullptr &&
                  active_session->client != nullptr &&
                  active_session->client->container_open(),
              .chat_open =
                  has_active_session &&
                  active_session != nullptr &&
                  active_session->client != nullptr &&
                  active_session->client->chat_open(),
              .carried_stack =
                  active_session != nullptr
                      ? active_session->carried_inventory_stack
                      : std::optional<ui::UiItemStackView>{},
              .inventory_slots =
                  active_session != nullptr
                      ? std::span<const ui::UiItemStackView>{
                            active_session->inventory_slots.data(),
                            active_session->inventory_slots.size()}
                      : std::span<const ui::UiItemStackView>{},
              .container_slots =
                  active_session != nullptr
                      ? std::span<const ui::UiItemStackView>{
                            active_session->container_slots.data(),
                            active_session->container_slots.size()}
                      : std::span<const ui::UiItemStackView>{},
              .chat_lines =
                  active_session != nullptr
                      ? std::span<const ui::ChatLineView>{
                            active_session->chat_lines.data(),
                            active_session->chat_lines.size()}
                      : std::span<const ui::ChatLineView>{},
          },
          .before_draw_append = append_before_ui_draw,
          .font_system = &assets_.font_system,
          .font_stacks = &assets_.ui_font_stacks,
          .frame = &frame,
          .time_seconds =
              std::chrono::duration<f32>(now - app_start_time_).count(),
          .sync_font_atlas = sync_font_atlas,
      });
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

  client::refresh_gameplay_chat_lines(
      active_session.chat_lines,
      *active_session.client,
      config_.ui.chat,
      now,
      active_session.chat_history_scroll_offset);
}

bool DesktopFrameLoop::route_chat_mouse_input(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input) {
  if (active_session.client == nullptr) {
    return false;
  }
  const auto open_url_unavailable = [](std::string_view) {
    return false;
  };
  const auto copy_to_clipboard_unavailable = [](std::string_view) {
    return false;
  };
  const auto run_command_click =
      [&](std::string_view command_text) {
        const bool remote_command =
            active_session.integrated_server == nullptr;
        const command::CommandResult result =
            execute_chat_command(active_session, std::string{command_text});
        if (!remote_command || !result.succeeded()) {
          client::append_command_result_to_chat(
              *active_session.client,
              result,
              assets_.language);
        }
      };
  const client::ChatMouseRoutingCallbacks callbacks{
      .open_url = open_url_unavailable,
      .copy_to_clipboard = copy_to_clipboard_unavailable,
      .run_command = run_command_click,
      .blocked_unsafe_url_message =
          assets_.language.text("ui.chat.blocked_unsafe_url"),
      .copy_unavailable_message =
          assets_.language.text("ui.chat.copy_unavailable"),
  };
  return client::route_chat_mouse_input(
      ui_tree_,
      *active_session.client,
      platform_input,
      std::span<const ui::ChatLineView>{
          active_session.chat_lines.data(),
          active_session.chat_lines.size()},
      active_session.client->chat_history().size(),
      config_.ui.chat.open_max_lines,
      active_session.chat_history_scroll_offset,
      callbacks);
}

void DesktopFrameLoop::build_gameplay_ui(
    DesktopGameSession& active_session,
    const platform::InputSnapshot& platform_input,
    std::chrono::steady_clock::time_point now,
    ui::RidePromptModel ride_prompt,
    FunctionRef<void()> append_active_screens) {
  const f32 item_popup_alpha =
      client::hotbar_item_popup_alpha(hotbar_item_popup_, now);
  (void)client::build_gameplay_ui_tree(
      {
          .tree = &ui_tree_,
          .theme = &ui_theme_,
          .model = client::build_gameplay_ui_model_from_snapshots(
          {
              .client = *active_session.client,
              .chat_config = config_.ui.chat,
              .inventory_slots = std::span<const ui::UiItemStackView>{
                  active_session.inventory_slots.data(),
                  active_session.inventory_slots.size()},
              .container_slots = std::span<const ui::UiItemStackView>{
                  active_session.container_slots.data(),
                  active_session.container_slots.size()},
              .carried_inventory_stack = active_session.carried_inventory_stack,
              .chat_lines = std::span<const ui::ChatLineView>{
                  active_session.chat_lines.data(),
                  active_session.chat_lines.size()},
              .chat_history_scroll_offset = active_session.chat_history_scroll_offset,
              .tab_players = std::span<const ui::PlayerListEntryView>{
                  active_session.tab_players.data(),
                  active_session.tab_players.size()},
              .inventory_title = assets_.language.text("ui.inventory.title"),
              .tab_title = assets_.language.text("ui.tab.players"),
              .container_layouts = &assets_.container_layouts,
              .viewport = ui_metrics_.logical_size,
              .cursor_position = {
                  platform_input.cursor_x,
                  platform_input.cursor_y,
              },
              .displayed_fps = displayed_fps_,
              .hotbar_item_popup_name = hotbar_item_popup_.popup_name,
              .hotbar_item_popup_alpha = item_popup_alpha,
              .crosshair = {
                  assets_.crosshair_image.valid,
                  ui_metrics_.logical_size,
                  assets_.crosshair_image,
              },
              .ride_prompt = std::move(ride_prompt),
              .language = &assets_.language,
          }),
          .append_active_screens = append_active_screens,
          .metrics = &ui_metrics_,
          .text_measurer = &ui_text_measurer_,
      });
}

} // namespace arv::desktop
