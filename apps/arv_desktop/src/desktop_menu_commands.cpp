#include "desktop_frame_loop.hpp"

#include "desktop_runtime_config.hpp"
#include "desktop_save_helpers.hpp"

#include "arv/client/client_settings.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/core/log.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/player/player_constants.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace arv::desktop {
namespace {

[[nodiscard]] const std::filesystem::path& client_config_path(const io::EngineConfig& config) noexcept {
  return config.paths.client_config_file.empty() ? config.paths.config_file : config.paths.client_config_file;
}

[[nodiscard]] asset::TextureAtlasBuildOptions entity_atlas_options_from(
    const io::RenderConfig::StaticAtlasConfig& source) noexcept {
  return {
      .max_page_size = source.page_size,
      .max_pages = source.max_pages,
      .padding = 2,
      .require_uniform_non_fallback_size = false};
}

[[nodiscard]] bool outline_settings_equal(
    const io::RenderConfig::BlockOutlineConfig& lhs,
    const io::RenderConfig::BlockOutlineConfig& rhs) noexcept {
  return lhs.color_r == rhs.color_r &&
         lhs.color_g == rhs.color_g &&
         lhs.color_b == rhs.color_b &&
         lhs.thickness == rhs.thickness;
}

} // namespace

void DesktopFrameLoop::queue_singleplayer_start(PendingSingleplayerStart start) {
  pending_singleplayer_start_ = std::move(start);
  pending_remote_start_.reset();
  loading_screen_ = DesktopLoadingScreen::LoadingWorld;
  window_.set_cursor_captured(false);
  invalidate_ui_tree_cache();
}

void DesktopFrameLoop::queue_remote_start(PendingRemoteStart start) {
  pending_remote_start_ = std::move(start);
  pending_singleplayer_start_.reset();
  loading_screen_ = DesktopLoadingScreen::LoadingWorld;
  window_.set_cursor_captured(false);
  invalidate_ui_tree_cache();
}

void DesktopFrameLoop::clear_pending_world_start() noexcept {
  pending_singleplayer_start_.reset();
  pending_remote_start_.reset();
}

bool DesktopFrameLoop::begin_closing_game() {
  clear_pending_world_start();
  loading_screen_ = DesktopLoadingScreen::ClosingGame;
  window_.set_cursor_captured(false);
  invalidate_ui_tree_cache();
  if (sessions_.start_pending()) {
    return true;
  }
  if (!sessions_.shutdown_pending() && !sessions_.begin_save_and_shutdown()) {
    return false;
  }
  return true;
}

bool DesktopFrameLoop::process_loading_world_frame() {
  if (!render_loading_screen_frame(DesktopLoadingScreen::LoadingWorld)) {
    return false;
  }
  sessions_.poll_shutdown();
  if (window_.should_close()) {
    return begin_closing_game();
  }
  if (sessions_.shutdown_pending()) {
    return true;
  }
  if (sessions_.shutdown_failed()) {
    clear_pending_world_start();
    loading_screen_ = DesktopLoadingScreen::None;
    return false;
  }
  if (sessions_.start_pending()) {
    const std::optional<bool> start_result = sessions_.poll_start();
    if (!start_result.has_value()) {
      return true;
    }
    return complete_pending_world_start(start_result.value());
  }
  return begin_pending_world_start();
}

bool DesktopFrameLoop::begin_pending_world_start() {
  if (sessions_.active_session() != nullptr) {
    if (!sessions_.begin_save_and_shutdown()) {
      return complete_pending_world_start(false);
    }
    return true;
  }

  if (pending_remote_start_.has_value()) {
    const PendingRemoteStart& start = pending_remote_start_.value();
    if (!ensure_local_assets()) {
      return complete_pending_world_start(false);
    }
    if (!sessions_.begin_start_remote(start.command, start.local_identity)) {
      return complete_pending_world_start(false);
    }
    return true;
  }

  if (pending_singleplayer_start_.has_value()) {
    const PendingSingleplayerStart& start = pending_singleplayer_start_.value();
    if (!ensure_local_assets()) {
      return complete_pending_world_start(false);
    }
    if (!sessions_.begin_start_singleplayer(
            start.server_save_name,
            start.world_name,
            start.create_seed,
            start.create_new,
            start.save_display_name,
            start.local_identity)) {
      return complete_pending_world_start(false);
    }
    return true;
  }

  loading_screen_ = DesktopLoadingScreen::None;
  invalidate_ui_tree_cache();
  return true;
}

bool DesktopFrameLoop::complete_pending_world_start(bool started) {
  if (pending_remote_start_.has_value()) {
    PendingRemoteStart start = std::move(pending_remote_start_.value());
    clear_pending_world_start();
    const ui::JoinServerCommand& command = start.command;
    if (started) {
      gameplay_menu_stack_active_ = false;
      gameplay_screen_manager_.clear();
      screen_manager_.replace(ui::make_multiplayer_connecting_screen(
          ui::ServerListEntry{command.name, command.address}));
    } else if (sessions_.active_session() == nullptr) {
      screen_manager_.replace(ui::make_multiplayer_disconnect_screen(
          assets_.language.text("ui.menu.disconnect.server_offline"),
          ui::ServerListEntry{command.name, command.address}));
    }
    loading_screen_ = DesktopLoadingScreen::None;
    invalidate_ui_tree_cache();
    return !sessions_.shutdown_failed();
  }

  if (pending_singleplayer_start_.has_value()) {
    PendingSingleplayerStart start = std::move(pending_singleplayer_start_.value());
    clear_pending_world_start();
    if (started) {
      gameplay_menu_stack_active_ = true;
      reset_gameplay_menu_stack();
      if (start.refresh_worlds_after_start) {
        menu_state_.refresh_worlds();
      }
      reset_main_menu_stack();
    } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
      screen_manager_.push(ui::make_single_player_menu_screen());
    }
    loading_screen_ = DesktopLoadingScreen::None;
    invalidate_ui_tree_cache();
    return !sessions_.shutdown_failed();
  }

  loading_screen_ = DesktopLoadingScreen::None;
  invalidate_ui_tree_cache();
  return true;
}

bool DesktopFrameLoop::process_menu_commands() {
  if (const std::optional<ui::SetRenderSettingsCommand> command =
          menu_state_.take_render_settings_request();
      command.has_value()) {
    apply_render_settings(command.value());
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::SetControlsSettingsCommand> command =
          menu_state_.take_controls_settings_request();
      command.has_value()) {
    apply_controls_settings(command.value());
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::SetInterfaceSettingsCommand> command =
          menu_state_.take_interface_settings_request();
      command.has_value()) {
    apply_interface_settings(command.value());
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::SetKeyBindCommand> command =
          menu_state_.take_key_bind_request();
      command.has_value()) {
    apply_key_bind(command.value());
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::ResetKeyBindCommand> command =
          menu_state_.take_key_bind_reset_request();
      command.has_value()) {
    apply_key_bind_reset(command.value());
    invalidate_ui_tree_cache();
  }
  if (menu_state_.take_cancel_server_connect_request()) {
    if (!sessions_.begin_save_and_shutdown()) {
      return false;
    }
    gameplay_menu_stack_active_ = false;
    gameplay_screen_manager_.clear();
    screen_manager_.replace(ui::make_multi_player_menu_screen());
    invalidate_ui_tree_cache();
  }
  if (menu_state_.take_back_to_main_menu_request()) {
    if (!sessions_.begin_save_and_shutdown()) {
      return false;
    }
    gameplay_menu_stack_active_ = false;
    gameplay_screen_manager_.clear();
    reset_main_menu_stack();
    menu_state_.refresh_worlds();
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::DeleteSinglePlayerSaveCommand> command =
          menu_state_.take_delete_save_request();
      command.has_value()) {
    sessions_.delete_singleplayer_save(command.value());
    menu_state_.refresh_worlds();
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::JoinServerCommand> command =
          menu_state_.take_join_server_request();
      command.has_value()) {
    const client::LocalPlayerIdentity* local_identity = menu_state_.active_local_profile();
    if (local_identity == nullptr) {
      core::error("identity", "cannot join server without a selected local profile");
    } else {
      queue_remote_start({command.value(), *local_identity});
    }
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::CreateWorldCommand> command =
          menu_state_.take_create_world_request();
      command.has_value()) {
    const std::string server_save_name =
        unique_server_save_name(config_, command->name);
    const u64 seed = parse_or_generate_seed(command->seed);
    const client::LocalPlayerIdentity* local_identity = menu_state_.active_local_profile();
    if (local_identity == nullptr) {
      core::error("identity", "cannot create world without a selected local profile");
    } else {
      queue_singleplayer_start({
          server_save_name,
          "default",
          seed,
          true,
          true,
          command->name,
          *local_identity});
    }
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::StartSinglePlayerCommand> command =
          menu_state_.take_start_singleplayer_request();
      command.has_value()) {
    const client::LocalPlayerIdentity* local_identity = menu_state_.active_local_profile();
    if (local_identity == nullptr) {
      core::error("identity", "cannot start singleplayer without a selected local profile");
    } else {
      queue_singleplayer_start({
          command->server_save_name,
          command->world_name,
          std::nullopt,
          false,
          false,
          {},
          *local_identity});
    }
    invalidate_ui_tree_cache();
  }
  if (menu_state_.quit_requested) {
    menu_state_.quit_requested = false;
    if (!begin_closing_game()) {
      return false;
    }
  }
  return true;
}

void DesktopFrameLoop::apply_render_settings(const ui::SetRenderSettingsCommand& command) {
  const ui::RenderSettingsView settings =
      client::normalized_render_settings(command.settings);
  const io::RenderConfig::SkyConfig sky_settings =
      client::sky_config_from_render_settings(settings);
  const io::RenderConfig::BlockOutlineConfig outline_settings =
      client::block_outline_config_from_render_settings(settings);
  const bool outline_changed =
      !outline_settings_equal(config_.render.block_outline, outline_settings);

  config_.window.vsync = settings.frame_sync == ui::SettingsFrameSyncMode::VSync;
  config_.render.frame_pacing_enabled =
      settings.frame_sync == ui::SettingsFrameSyncMode::FpsCap;
  config_.render.frame_pacing_target_fps = settings.fps_cap;
  config_.chunks.load_distance = settings.load_distance;
  config_.chunks.simulation_distance = settings.simulation_distance;
  config_.chunks.render_distance = settings.view_distance;
  config_.chunks.stream_distance = settings.stream_distance;
  config_.render.max_chunk_mesh_uploads_per_frame = settings.mesh_upload_budget;
  config_.render.show_local_player_body = settings.show_local_player_body;
  config_.render.shadows.enabled = settings.shadows_enabled;
  config_.render.sky = sky_settings;
  config_.render.block_outline = outline_settings;

  render_thread_.set_vsync(config_.window.vsync && !config_.render.frame_pacing_enabled);
  renderer_.set_chunk_mesh_upload_budget(config_.render.max_chunk_mesh_uploads_per_frame);
  if (DesktopGameSession* session = sessions_.active_session();
      session != nullptr && session->client != nullptr) {
    if (session->integrated_server != nullptr) {
      session->integrated_server->configure_chunk_streaming(
          make_chunk_streaming_config(config_));
    }
    session->client->set_chunk_view_distance(config_.chunks.stream_distance);
    request_chunk_view_mesh_rescan(session->chunk_view);
  }
  if (outline_changed) {
    rebuild_outline_render_assets();
  }
  persist_client_settings();
}

void DesktopFrameLoop::apply_controls_settings(const ui::SetControlsSettingsCommand& command) {
  config_.physics.mouse_sensitivity_radians =
      client::mouse_sensitivity_radians_from_percent(
          command.settings.mouse_sensitivity_percent,
          player::DefaultMouseSensitivityRadians);
  refresh_runtime_player_tuning();
  persist_client_settings();
}

void DesktopFrameLoop::apply_interface_settings(const ui::SetInterfaceSettingsCommand& command) {
  const ui::InterfaceSettingsView settings =
      client::normalized_interface_settings(command.settings);
  config_.ui.chat = client::chat_config_from_interface_settings(settings);
  if (DesktopGameSession* session = sessions_.active_session();
      session != nullptr && session->client != nullptr) {
    session->client->configure_chat({config_.ui.chat.max_history_messages});
  }
  persist_client_settings();
}

void DesktopFrameLoop::apply_key_bind(const ui::SetKeyBindCommand& command) {
  if (!client::apply_key_bind(input_bindings_, command)) {
    return;
  }
  window_.set_input_bindings(input_bindings_);
  persist_input_bindings();
}

void DesktopFrameLoop::apply_key_bind_reset(const ui::ResetKeyBindCommand& command) {
  if (!client::reset_key_bind(input_bindings_, command)) {
    return;
  }
  window_.set_input_bindings(input_bindings_);
  persist_input_bindings();
}

void DesktopFrameLoop::persist_client_settings() const {
  const auto result = io::save_client_config_file(client_config_path(config_), config_);
  if (!result) {
    core::warn("config", result.error().message);
  }
}

void DesktopFrameLoop::persist_input_bindings() const {
  const auto result = io::save_input_bindings_file(
      config_.paths.input_bindings_file,
      input_bindings_);
  if (!result) {
    core::warn("input", result.error().message);
  }
}

void DesktopFrameLoop::refresh_runtime_player_tuning() {
  player_tuning_ = make_player_tuning(config_.physics);
  if (DesktopGameSession* session = sessions_.active_session();
      session != nullptr && session->client != nullptr) {
    session->client->set_player_tuning(player_tuning_);
  }
}

void DesktopFrameLoop::rebuild_outline_render_assets() {
  client::EntityRenderAssetsResult result = client::build_entity_render_assets(
      resources_,
      assets_.entity_registry,
      assets_.item_registry,
      assets_.block_registry,
      assets_.block_atlas.render_data,
      {},
      {
          {config_.render.block_outline.color_r,
           config_.render.block_outline.color_g,
           config_.render.block_outline.color_b},
          config_.render.block_outline.thickness},
      entity_atlas_options_from(config_.render.atlases.entities));
  if (!result) {
    core::warn(
        "assets",
        "failed to rebuild outline render assets: {}",
        result.error().message);
    return;
  }

  assets_.entity_render_assets = std::move(result.value());
  renderer_.set_entity_texture_atlas(
      client::texture_array_upload_from_atlas(
          assets_.entity_render_assets.texture_atlas));
  renderer_.clear_entity_models();
  renderer_.clear_entity_animations();
  const auto upload_model = [this](const render::EntityModelAsset& model) {
    renderer_.set_entity_model(model);
  };
  const auto upload_animation =
      [this](const render::EntityAnimationAsset& animation) {
    renderer_.set_entity_animation(animation);
  };
  (void)client::upload_entity_render_asset_records(
      assets_.entity_render_assets.models,
      assets_.entity_render_assets.animations,
      {.upload_model =
           FunctionRef<void(const render::EntityModelAsset&)>{upload_model},
       .upload_animation =
           FunctionRef<void(const render::EntityAnimationAsset&)>{
               upload_animation}});
}

bool DesktopFrameLoop::handle_session_disconnect(DesktopGameSession* active_session) {
  if (active_session == nullptr || active_session->client == nullptr) {
    return false;
  }
  const std::optional<std::string> reason = active_session->client->disconnect_reason();
  if (!reason.has_value()) {
    return false;
  }
  std::optional<ui::ServerListEntry> reconnect;
  if (active_session->remote_server.has_value()) {
    reconnect = ui::ServerListEntry{
        active_session->remote_server->name,
        active_session->remote_server->address};
  }
  if (!sessions_.begin_save_and_shutdown()) {
    return false;
  }
  gameplay_menu_stack_active_ = false;
  gameplay_screen_manager_.clear();
  reset_main_menu_stack();
  screen_manager_.push(ui::make_multiplayer_disconnect_screen(
      reason.value(),
      reconnect));
  invalidate_ui_tree_cache();
  return true;
}

void DesktopFrameLoop::handle_remote_connection_ready(DesktopGameSession* active_session) {
  if (active_session == nullptr || active_session->client == nullptr ||
      !active_session->remote_server.has_value() ||
      active_session->client->connection_state() != net::ConnectionState::InGame) {
    return;
  }
  if (!ensure_remote_assets(*active_session)) {
    active_session->client->disconnect();
    return;
  }
  const ui::UIScreen* screen = screen_manager_.current();
  if (screen != nullptr &&
      screen->type() == ui::MenuScreenType::MultiplayerConnecting) {
    gameplay_menu_stack_active_ = true;
    reset_gameplay_menu_stack();
    reset_main_menu_stack();
    invalidate_ui_tree_cache();
  }
}

} // namespace arv::desktop
