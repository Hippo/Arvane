#include "desktop_frame_loop.hpp"

#include "asset_helpers.hpp"
#include "desktop_runtime_config.hpp"
#include "desktop_save_helpers.hpp"

#include "arv/core/log.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/net/connection_state.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace arv::desktop {
namespace {

inline constexpr i32 MaxConfigurableChunkDistance = 32;

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
    const ui::RenderSettingsView& rhs) noexcept {
  return lhs.color_r == rhs.outline_red &&
         lhs.color_g == rhs.outline_green &&
         lhs.color_b == rhs.outline_blue &&
         lhs.thickness == rhs.outline_thickness;
}

[[nodiscard]] ui::RenderSettingsView normalized_chunk_distances(
    ui::RenderSettingsView settings) noexcept {
  settings.load_distance =
      std::clamp(settings.load_distance, 1, MaxConfigurableChunkDistance);
  settings.simulation_distance =
      std::clamp(settings.simulation_distance, 0, settings.load_distance);
  settings.stream_distance =
      std::clamp(settings.stream_distance, 1, settings.load_distance);
  settings.view_distance =
      std::clamp(settings.view_distance, 1, settings.stream_distance);
  return settings;
}

[[nodiscard]] platform::InputDeviceType platform_device_from(ui::InputBindingDevice type) noexcept {
  switch (type) {
    case ui::InputBindingDevice::Keyboard:
      return platform::InputDeviceType::Keyboard;
    case ui::InputBindingDevice::MouseButton:
      return platform::InputDeviceType::MouseButton;
    case ui::InputBindingDevice::MouseWheel:
      return platform::InputDeviceType::MouseWheel;
  }
  return platform::InputDeviceType::Keyboard;
}

[[nodiscard]] std::optional<platform::InputBinding> platform_binding_from(
    const ui::InputBindingView& binding) {
  const platform::InputDeviceType device = platform_device_from(binding.device);
  const std::optional<std::string> canonical =
      platform::canonical_input_code(device, binding.input_code);
  if (!canonical.has_value()) {
    return std::nullopt;
  }
  return platform::InputBinding{device, canonical.value(), binding.modifiers};
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
  return finish_pending_world_start();
}

bool DesktopFrameLoop::finish_pending_world_start() {
  if (pending_remote_start_.has_value()) {
    PendingRemoteStart start = std::move(pending_remote_start_.value());
    clear_pending_world_start();
    const ui::JoinServerCommand& command = start.command;
    if (sessions_.start_remote(command, start.local_identity)) {
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
    if (sessions_.start_singleplayer(
            std::move(start.server_save_name),
            std::move(start.world_name),
            start.create_seed,
            start.create_new,
            std::move(start.save_display_name),
            start.local_identity)) {
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
  const ui::RenderSettingsView settings = normalized_chunk_distances(command.settings);
  const bool outline_changed =
      !outline_settings_equal(config_.render.block_outline, settings);

  config_.window.vsync = settings.frame_sync == ui::SettingsFrameSyncMode::VSync;
  config_.render.frame_pacing_enabled =
      settings.frame_sync == ui::SettingsFrameSyncMode::FpsCap;
  config_.render.frame_pacing_target_fps =
      std::clamp<u32>(settings.fps_cap, 30U, 240U);
  config_.chunks.load_distance = settings.load_distance;
  config_.chunks.simulation_distance = settings.simulation_distance;
  config_.chunks.render_distance = settings.view_distance;
  config_.chunks.stream_distance = settings.stream_distance;
  config_.render.max_chunk_mesh_uploads_per_frame =
      std::clamp<u32>(settings.mesh_upload_budget, 1U, 32U);
  config_.render.show_local_player_body = settings.show_local_player_body;
  config_.render.sky.star_intensity = std::clamp(settings.star_intensity, 0.0F, 3.0F);
  config_.render.sky.cloud_intensity = std::clamp(settings.cloud_intensity, 0.0F, 3.0F);
  config_.render.sky.cirrus_amount = std::clamp(settings.cirrus_amount, 0.0F, 1.0F);
  config_.render.sky.cumulus_amount = std::clamp(settings.cumulus_amount, 0.0F, 1.0F);
  config_.render.block_outline.color_r = std::clamp(settings.outline_red, 0.0F, 1.0F);
  config_.render.block_outline.color_g = std::clamp(settings.outline_green, 0.0F, 1.0F);
  config_.render.block_outline.color_b = std::clamp(settings.outline_blue, 0.0F, 1.0F);
  config_.render.block_outline.thickness =
      std::clamp(settings.outline_thickness, 0.1F, 2.0F);

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
  constexpr f32 DefaultSensitivity = 0.0025F;
  const f32 percent =
      std::clamp(command.settings.mouse_sensitivity_percent, 10.0F, 300.0F);
  config_.physics.mouse_sensitivity_radians = DefaultSensitivity * (percent / 100.0F);
  refresh_runtime_player_tuning();
  persist_client_settings();
}

void DesktopFrameLoop::apply_interface_settings(const ui::SetInterfaceSettingsCommand& command) {
  const ui::InterfaceSettingsView settings = command.settings;
  config_.ui.chat.max_history_messages =
      std::clamp<u32>(settings.chat_history_max, 100U, 5000U);
  config_.ui.chat.open_max_lines = std::clamp<u32>(settings.chat_open_lines, 1U, 16U);
  config_.ui.chat.passive_max_lines =
      std::clamp<u32>(settings.chat_passive_lines, 0U, 16U);
  config_.ui.chat.passive_visible_seconds =
      std::clamp(settings.chat_passive_visible_seconds, 0.0F, 30.0F);
  config_.ui.chat.passive_fade_seconds =
      std::clamp(settings.chat_passive_fade_seconds, 0.0F, 30.0F);
  if (DesktopGameSession* session = sessions_.active_session();
      session != nullptr && session->client != nullptr) {
    session->client->configure_chat({config_.ui.chat.max_history_messages});
  }
  persist_client_settings();
}

void DesktopFrameLoop::apply_key_bind(const ui::SetKeyBindCommand& command) {
  const std::optional<platform::LogicalAction> action =
      platform::logical_action_from_name(command.action_name);
  if (!action.has_value() || command.slot > 1U) {
    return;
  }

  auto& action_bindings = platform::bindings_for_action(input_bindings_, action.value());
  if (command.binding.has_value()) {
    const std::optional<platform::InputBinding> binding =
        platform_binding_from(command.binding.value());
    if (!binding.has_value()) {
      return;
    }
    if (action_bindings.size() <= command.slot) {
      const platform::InputBindings defaults = platform::default_input_bindings();
      const auto& default_bindings =
          platform::bindings_for_action(defaults, action.value());
      while (action_bindings.size() <= command.slot) {
        if (action_bindings.empty() && !default_bindings.empty()) {
          action_bindings.push_back(default_bindings.front());
        } else {
          action_bindings.emplace_back(binding.value());
        }
      }
    }
    action_bindings[command.slot] = binding.value();
  } else if (command.slot == 1U && action_bindings.size() > 1U) {
    action_bindings.erase(action_bindings.begin() + 1);
  } else {
    return;
  }

  if (action_bindings.size() > 2U) {
    action_bindings.resize(2U);
  }
  window_.set_input_bindings(input_bindings_);
  persist_input_bindings();
}

void DesktopFrameLoop::apply_key_bind_reset(const ui::ResetKeyBindCommand& command) {
  const std::optional<platform::LogicalAction> action =
      platform::logical_action_from_name(command.action_name);
  if (!action.has_value()) {
    return;
  }
  const platform::InputBindings defaults = platform::default_input_bindings();
  auto& action_bindings = platform::bindings_for_action(input_bindings_, action.value());
  action_bindings = platform::bindings_for_action(defaults, action.value());
  if (action_bindings.size() > 2U) {
    action_bindings.resize(2U);
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
  EntityRenderAssetsResult result = build_entity_render_assets(
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
      texture_array_upload_from_atlas(assets_.entity_render_assets.texture_atlas));
  renderer_.clear_entity_models();
  renderer_.clear_entity_animations();
  for (const render::EntityModelAsset& model : assets_.entity_render_assets.models) {
    renderer_.set_entity_model(model);
  }
  for (const render::EntityAnimationAsset& animation : assets_.entity_render_assets.animations) {
    renderer_.set_entity_animation(animation);
  }
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
