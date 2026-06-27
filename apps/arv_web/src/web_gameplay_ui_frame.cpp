#include "web_gameplay_ui_frame.hpp"

#include "arv/base/ui/widgets/gameplay.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/client_settings.hpp"
#include "arv/client/gameplay_ui_frame.hpp"
#include "arv/client/gameplay_ui_model.hpp"
#include "arv/client/item_ui_model.hpp"
#include "arv/net/transport.hpp"
#include "arv/ui/core/ui.hpp"
#include "web_application.hpp"
#include "web_ui_host.hpp"

#include <chrono>
#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::web {

void append_web_gameplay_ui_to_frame(
    arv::render::FramePacket& frame,
    arv::Seconds time_seconds,
    arv::ui::RidePromptModel ride_prompt) {
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame ||
      !ensure_web_ui_ready()) {
    return;
  }

  const bool item_ui_ready = g_app.entity_assets.ready;
  arv::client::InventoryUiProjection inventory_projection =
      arv::client::project_inventory_ui(
          {
              .has_authoritative_inventory =
                  item_ui_ready && g_app.session->has_authoritative_inventory(),
              .container_open = g_app.session->container_open(),
              .inventory = g_app.session->inventory(),
              .container_inventory = g_app.session->active_container_inventory(),
              .carried_stack = g_app.session->inventory_cursor_stack(),
              .item_registry = g_app.entity_assets.loaded.item_registry,
              .item_atlas = g_app.entity_assets.item_atlas,
              .block_atlas = g_app.block_assets.loaded.block_atlas,
          });
  const auto ui_now = std::chrono::steady_clock::now();
  if (item_ui_ready && g_app.session->has_authoritative_inventory()) {
    arv::client::update_hotbar_item_popup(
        g_app.ui.hotbar_item_popup,
        g_app.session->selected_hotbar_slot(),
        std::span<const arv::ui::UiItemStackView>{
            inventory_projection.inventory_slots.data(),
            inventory_projection.inventory_slots.size()},
        ui_now);
  } else {
    arv::client::reset_hotbar_item_popup(g_app.ui.hotbar_item_popup);
  }
  const arv::f32 item_popup_alpha =
      arv::client::hotbar_item_popup_alpha(g_app.ui.hotbar_item_popup, ui_now);
  arv::io::UiConfig::ChatConfig chat_config = web_chat_config();
  arv::client::refresh_gameplay_chat_lines(
      g_app.ui.chat_lines,
      *g_app.session,
      chat_config,
      ui_now,
      g_app.ui.chat_history_scroll_offset);
  const arv::ui::UiDisplayMetrics metrics = web_ui_metrics();
  const arv::Vec2f viewport = metrics.logical_size;
  const arv::Vec2f cursor_position =
      g_app.input.cursor_initialized
          ? arv::Vec2f{
                g_app.input.snapshot.cursor_x,
                g_app.input.snapshot.cursor_y}
          : arv::Vec2f{viewport.x * 0.5F, viewport.y * 0.5F};
  const std::vector<arv::ui::KeyBindEntryView> key_bind_entries =
      arv::client::key_bind_entries_from_input_bindings(g_app.input_bindings);

  arv::ui::GameplayUiModel model =
      arv::client::build_gameplay_ui_model_from_snapshots(
          {
              .client = *g_app.session,
              .chat_config = chat_config,
              .inventory_slots = std::span<const arv::ui::UiItemStackView>{
                  inventory_projection.inventory_slots.data(),
                  inventory_projection.inventory_slots.size()},
              .container_slots = std::span<const arv::ui::UiItemStackView>{
                  inventory_projection.container_slots.data(),
                  inventory_projection.container_slots.size()},
              .carried_inventory_stack = inventory_projection.carried_stack,
              .chat_lines = std::span<const arv::ui::ChatLineView>{
                  g_app.ui.chat_lines.data(),
                  g_app.ui.chat_lines.size()},
              .chat_history_scroll_offset = g_app.ui.chat_history_scroll_offset,
              .inventory_title =
                  g_app.ui.language.text("ui.inventory.title", "Inventory"),
              .player_menu_title =
                  g_app.ui.language.text("ui.inventory.title", "Inventory"),
              .tab_title =
                  g_app.ui.language.text("ui.tab.players", "Players"),
              .container_layouts = &g_app.block_assets.loaded.container_layouts,
              .crafting_registry = &g_app.entity_assets.crafting_registry,
              .item_registry = &g_app.entity_assets.loaded.item_registry,
              .item_atlas = &g_app.entity_assets.item_atlas,
              .block_atlas = &g_app.block_assets.loaded.block_atlas,
              .render_settings = web_render_settings(),
              .controls_settings = web_controls_settings(),
              .interface_settings = web_interface_settings(),
              .key_bind_entries = key_bind_entries,
              .show_quit = true,
              .viewport = viewport,
              .cursor_position = cursor_position,
              .hotbar_item_popup_name = g_app.ui.hotbar_item_popup.popup_name,
              .hotbar_item_popup_alpha = item_popup_alpha,
              .crosshair = {
                  .visible = g_app.entity_assets.crosshair_image.valid,
                  .viewport = viewport,
                  .image = g_app.entity_assets.crosshair_image,
              },
              .ride_prompt = std::move(ride_prompt),
              .language = &g_app.ui.language,
              .debug_fps_visible = false,
          });

  const arv::ui::IUiTextMeasurer* text_measurer =
      g_app.ui.text_measurer.has_value() ? &g_app.ui.text_measurer.value() : nullptr;
  if (g_app.ui.screen_manager.has_active_screen()) {
    const auto append_active_screens = [&]() {
      arv::ui::UIContext context = make_web_ui_context();
      context.viewport = viewport;
      g_app.ui.screen_manager.append_to(context);
    };
    (void)arv::client::build_gameplay_ui_tree(
        {
            .tree = &g_app.ui.tree,
            .theme = &g_app.ui.theme,
            .model = std::move(model),
            .append_active_screens = append_active_screens,
            .metrics = &metrics,
            .text_measurer = text_measurer,
        });
  } else {
    (void)arv::client::build_gameplay_ui_tree(
        {
            .tree = &g_app.ui.tree,
            .theme = &g_app.ui.theme,
            .model = std::move(model),
            .metrics = &metrics,
            .text_measurer = text_measurer,
        });
  }
  const auto sync_font_atlas = []() -> bool {
    return sync_web_ui_font_atlas();
  };
  const arv::client::GameplayUiFrameAppendResult append_result =
      arv::client::append_gameplay_ui_frame(
          {
              .tree = &g_app.ui.tree,
              .metrics = &metrics,
              .text_measurer = text_measurer,
              .cursor_position = cursor_position,
              .hover = {
                  .inventory_open = g_app.session->inventory_open(),
                  .container_open = g_app.session->container_open(),
                  .chat_open = g_app.session->chat_open(),
                  .carried_stack = inventory_projection.carried_stack,
                  .inventory_slots =
                      std::span<const arv::ui::UiItemStackView>{
                          inventory_projection.inventory_slots.data(),
                          inventory_projection.inventory_slots.size()},
                  .container_slots =
                      std::span<const arv::ui::UiItemStackView>{
                          inventory_projection.container_slots.data(),
                          inventory_projection.container_slots.size()},
                  .chat_lines =
                      std::span<const arv::ui::ChatLineView>{
                          g_app.ui.chat_lines.data(),
                          g_app.ui.chat_lines.size()},
              },
              .font_system = &g_app.ui.fonts,
              .font_stacks = &g_app.ui.font_stacks,
              .frame = &frame,
              .time_seconds = time_seconds,
              .sync_font_atlas = sync_font_atlas,
          });
  if (append_result.draw_commands.rolled_back) {
    return;
  }
  const arv::client::ClientWorldTransitionStatus& transition =
      g_app.session->world_transition_status();
  if (transition.active && transition.fade_alpha > 0.0F) {
    arv::render::UIRectDrawItem fade;
    fade.min = {0.0F, 0.0F};
    fade.max = metrics.framebuffer_size;
    fade.color = {
        0.0F,
        0.0F,
        0.0F,
        std::clamp(transition.fade_alpha, 0.0F, 1.0F) * 0.88F};
    fade.layer = 240U;
    frame.ui_rects.push_back(fade);
  }
}

} // namespace arv::web
