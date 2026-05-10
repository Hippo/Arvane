#include "desktop_frame_loop.hpp"

#include "desktop_save_helpers.hpp"

#include "arv/core/log.hpp"
#include "arv/net/connection_state.hpp"

#include <optional>
#include <string>

namespace arv::desktop {

bool DesktopFrameLoop::process_menu_commands() {
  if (menu_state_.take_cancel_server_connect_request()) {
    if (!sessions_.save_and_shutdown()) {
      return false;
    }
    screen_manager_.replace(ui::make_multi_player_menu_screen());
    invalidate_ui_tree_cache();
  }
  if (menu_state_.take_back_to_main_menu_request()) {
    if (!sessions_.save_and_shutdown()) {
      return false;
    }
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
    } else if (sessions_.start_remote(command.value(), *local_identity)) {
      screen_manager_.replace(ui::make_multiplayer_connecting_screen(
          ui::ServerListEntry{command->name, command->address}));
    } else if (sessions_.active_session() == nullptr) {
      screen_manager_.replace(ui::make_multiplayer_disconnect_screen(
          "Server is offline or unreachable.",
          ui::ServerListEntry{command->name, command->address}));
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
    } else if (sessions_.start_singleplayer(
            server_save_name,
            "default",
            seed,
            true,
            command->name,
            *local_identity)) {
      menu_state_.refresh_worlds();
      screen_manager_.clear();
    } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
      screen_manager_.push(ui::make_single_player_menu_screen());
    }
    invalidate_ui_tree_cache();
  }
  if (const std::optional<ui::StartSinglePlayerCommand> command =
          menu_state_.take_start_singleplayer_request();
      command.has_value()) {
    const client::LocalPlayerIdentity* local_identity = menu_state_.active_local_profile();
    if (local_identity == nullptr) {
      core::error("identity", "cannot start singleplayer without a selected local profile");
    } else if (sessions_.start_singleplayer(
            command->server_save_name,
            command->world_name,
            std::nullopt,
            false,
            {},
            *local_identity)) {
      screen_manager_.clear();
    } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
      screen_manager_.push(ui::make_single_player_menu_screen());
    }
    invalidate_ui_tree_cache();
  }
  if (menu_state_.quit_requested) {
    window_.request_close();
  }
  return true;
}

bool DesktopFrameLoop::handle_remote_disconnect(DesktopGameSession* active_session) {
  if (active_session == nullptr || active_session->client == nullptr ||
      !active_session->remote_server.has_value()) {
    return false;
  }
  const std::optional<std::string> reason = active_session->client->disconnect_reason();
  if (!reason.has_value()) {
    return false;
  }
  const ui::ServerListEntry reconnect{
      active_session->remote_server->name,
      active_session->remote_server->address};
  if (!sessions_.save_and_shutdown()) {
    return false;
  }
  screen_manager_.replace(ui::make_multiplayer_disconnect_screen(
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
    screen_manager_.clear();
    invalidate_ui_tree_cache();
  }
}

} // namespace arv::desktop
