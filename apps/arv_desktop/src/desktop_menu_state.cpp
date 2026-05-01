#include "desktop_menu_state.hpp"

#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/persistence/save.hpp"

#include <utility>
#include <variant>

namespace arv::desktop {

DesktopMenuState::DesktopMenuState(
    std::filesystem::path saves_root,
    std::filesystem::path server_list_file) : saves_root_(std::move(saves_root)),
                                              server_list_file_(std::move(server_list_file)) {
  refresh_worlds();
  load_servers();
}

std::vector<ui::SinglePlayerSaveEntry> DesktopMenuState::singleplayer_saves() const {
  return saves;
}

std::vector<ui::ServerListEntry> DesktopMenuState::multiplayer_servers() const {
  return servers;
}

void DesktopMenuState::submit(ui::UICommand command) {
  std::visit([this](auto&& concrete) { handle(concrete); }, std::move(command));
}

void DesktopMenuState::refresh_worlds() {
  saves.clear();
  for (const persistence::SavedServerSummary& save :
       persistence::discover_saved_servers(saves_root_)) {
    saves.push_back({save.name, save.server_save_name, save.primary_world_name, {}});
  }
}

std::optional<ui::StartSinglePlayerCommand>
DesktopMenuState::take_start_singleplayer_request() noexcept {
  return std::exchange(start_singleplayer_request_, std::nullopt);
}

std::optional<ui::CreateWorldCommand>
DesktopMenuState::take_create_world_request() noexcept {
  return std::exchange(create_world_request_, std::nullopt);
}

std::optional<ui::DeleteSinglePlayerSaveCommand>
DesktopMenuState::take_delete_save_request() noexcept {
  return std::exchange(delete_save_request_, std::nullopt);
}

std::optional<ui::JoinServerCommand>
DesktopMenuState::take_join_server_request() noexcept {
  return std::exchange(join_server_request_, std::nullopt);
}

bool DesktopMenuState::take_back_to_main_menu_request() noexcept {
  return std::exchange(back_to_main_menu_requested_, false);
}

void DesktopMenuState::handle(const ui::StartSinglePlayerCommand& command) {
  start_singleplayer_request_ = command;
}

void DesktopMenuState::handle(const ui::CreateWorldCommand& command) {
  create_world_request_ = command;
}

void DesktopMenuState::handle(const ui::DeleteSinglePlayerSaveCommand& command) {
  delete_save_request_ = command;
}

void DesktopMenuState::handle(const ui::JoinServerCommand& command) {
  join_server_request_ = command;
}

void DesktopMenuState::handle(const ui::SaveServerEntryCommand& command) {
  if (command.edit_index.has_value() && command.edit_index.value() < servers.size()) {
    servers[command.edit_index.value()] = command.server;
  } else {
    servers.push_back(command.server);
  }
  persist_servers();
}

void DesktopMenuState::handle(const ui::RemoveServerEntryCommand& command) {
  if (command.index < servers.size()) {
    servers.erase(servers.begin() + static_cast<std::ptrdiff_t>(command.index));
    persist_servers();
  }
}

void DesktopMenuState::handle(const ui::ResumeGameCommand&) {
}

void DesktopMenuState::handle(const ui::BackToMainMenuCommand&) {
  back_to_main_menu_requested_ = true;
}

void DesktopMenuState::handle(const ui::QuitGameCommand&) {
  quit_requested = true;
}

void DesktopMenuState::load_servers() {
  servers.clear();
  auto loaded = io::load_server_list_file(server_list_file_);
  if (!loaded) {
    core::warn("config", loaded.error().message);
    return;
  }

  servers.reserve(loaded.value().size());
  for (const io::SavedServerListEntry& server : loaded.value()) {
    servers.push_back({server.name, server.address});
  }
}

void DesktopMenuState::persist_servers() const {
  std::vector<io::SavedServerListEntry> saved_servers;
  saved_servers.reserve(servers.size());
  for (const ui::ServerListEntry& server : servers) {
    saved_servers.push_back({server.name, server.address});
  }

  auto saved = io::save_server_list_file(server_list_file_, saved_servers);
  if (!saved) {
    core::warn("config", saved.error().message);
  }
}

} // namespace arv::desktop
