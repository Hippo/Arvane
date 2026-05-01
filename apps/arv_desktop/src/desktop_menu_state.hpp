#pragma once

#include "arv/ui/ui.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace arv::desktop {

class DesktopMenuState final : public ui::UIScreenDataSource,
                               public ui::UICommandSink {
public:
  DesktopMenuState(std::filesystem::path saves_root, std::filesystem::path server_list_file);

  [[nodiscard]] std::vector<ui::SinglePlayerSaveEntry> singleplayer_saves() const override;
  [[nodiscard]] std::vector<ui::ServerListEntry> multiplayer_servers() const override;

  void submit(ui::UICommand command) override;

  void refresh_worlds();

  [[nodiscard]] std::optional<ui::StartSinglePlayerCommand>
  take_start_singleplayer_request() noexcept;
  [[nodiscard]] std::optional<ui::CreateWorldCommand>
  take_create_world_request() noexcept;
  [[nodiscard]] std::optional<ui::DeleteSinglePlayerSaveCommand>
  take_delete_save_request() noexcept;
  [[nodiscard]] std::optional<ui::JoinServerCommand>
  take_join_server_request() noexcept;
  [[nodiscard]] bool take_back_to_main_menu_request() noexcept;

  std::vector<ui::SinglePlayerSaveEntry> saves{};
  std::vector<ui::ServerListEntry> servers{};
  bool quit_requested{};

private:
  void handle(const ui::StartSinglePlayerCommand& command);
  void handle(const ui::CreateWorldCommand& command);
  void handle(const ui::DeleteSinglePlayerSaveCommand& command);
  void handle(const ui::JoinServerCommand& command);
  void handle(const ui::SaveServerEntryCommand& command);
  void handle(const ui::RemoveServerEntryCommand& command);
  void handle(const ui::ResumeGameCommand& command);
  void handle(const ui::BackToMainMenuCommand& command);
  void handle(const ui::QuitGameCommand& command);

  void load_servers();
  void persist_servers() const;

  std::filesystem::path saves_root_{};
  std::filesystem::path server_list_file_{};
  std::optional<ui::StartSinglePlayerCommand> start_singleplayer_request_{};
  std::optional<ui::CreateWorldCommand> create_world_request_{};
  std::optional<ui::DeleteSinglePlayerSaveCommand> delete_save_request_{};
  std::optional<ui::JoinServerCommand> join_server_request_{};
  bool back_to_main_menu_requested_{};
};

} // namespace arv::desktop
