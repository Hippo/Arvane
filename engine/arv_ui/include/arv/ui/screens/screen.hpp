#pragma once

#include "arv/ui/core/ui.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace arv::ui {
struct SinglePlayerSaveEntry {
  std::string name{};
  std::string server_save_name{};
  std::string world_name{};
  std::string detail{};
};

struct ServerListEntry {
  std::string name{};
  std::string address{};
};

struct StartSinglePlayerCommand {
  std::string server_save_name{};
  std::string world_name{};
};

struct CreateWorldCommand {
  std::string name{};
  std::string seed{};
};

struct DeleteSinglePlayerSaveCommand {
  std::string server_save_name{};
  std::string world_name{};
};

struct JoinServerCommand {
  std::string name{};
  std::string address{};
};

struct SaveServerEntryCommand {
  std::optional<usize> edit_index{};
  ServerListEntry server{};
};

struct RemoveServerEntryCommand {
  usize index{};
  ServerListEntry server{};
};

struct ResumeGameCommand {};
struct BackToMainMenuCommand {};
struct QuitGameCommand {};

using UICommand = std::variant<
    StartSinglePlayerCommand,
    CreateWorldCommand,
    DeleteSinglePlayerSaveCommand,
    JoinServerCommand,
    SaveServerEntryCommand,
    RemoveServerEntryCommand,
    ResumeGameCommand,
    BackToMainMenuCommand,
    QuitGameCommand>;

class UICommandSink {
public:
  virtual ~UICommandSink() = default;
  virtual void submit(UICommand command) = 0;
};

class UIScreenDataSource {
public:
  virtual ~UIScreenDataSource() = default;
  [[nodiscard]] virtual std::vector<SinglePlayerSaveEntry> singleplayer_saves() const {
    return {};
  }
  [[nodiscard]] virtual std::vector<ServerListEntry> multiplayer_servers() const {
    return {};
  }
};

class UIScreenManager;

struct UIContext {
  UiTree& tree;
  Vec2f viewport{};
  UICommandSink* command_sink{};
  const UIScreenDataSource* data_source{};
  UIScreenManager* screens{};
};

enum class MenuScreenType : u8 {
  MainMenu,
  SinglePlayerMenu,
  CreateWorld,
  MultiPlayerMenu,
  AddServer,
  EditServer,
  SettingsMenu,
  RenderSettings,
  KeyBinds,
  OtherSettings,
  PauseMenu,
};

class UIScreen {
public:
  virtual ~UIScreen() = default;

  [[nodiscard]] virtual MenuScreenType type() const noexcept = 0;
  [[nodiscard]] virtual std::string_view screen_id() const noexcept = 0;
  virtual void build(UIContext& ctx) = 0;

  virtual void on_enter() {
  }
  virtual void on_exit() {
  }

  virtual bool handle_action(const UiAction& action, UIContext& ctx);
  virtual bool handle_cancel(UIContext& ctx);
  virtual bool handle_text_input(std::string_view text, UIContext& ctx);
  virtual bool handle_backspace(UIContext& ctx);
  virtual bool handle_submit(UIContext& ctx);

  [[nodiscard]] virtual bool can_close() const noexcept {
    return true;
  }
  [[nodiscard]] virtual bool blocks_gameplay_input() const noexcept {
    return true;
  }
  [[nodiscard]] virtual bool pauses_singleplayer() const noexcept {
    return false;
  }
};

class UIScreenManager {
public:
  void push(std::unique_ptr<UIScreen> screen);
  void pop();
  void replace(std::unique_ptr<UIScreen> screen);
  void clear();

  [[nodiscard]] UIScreen* current() noexcept;
  [[nodiscard]] const UIScreen* current() const noexcept;
  [[nodiscard]] usize depth() const noexcept {
    return stack_.size();
  }
  [[nodiscard]] bool empty() const noexcept {
    return stack_.empty();
  }
  [[nodiscard]] bool has_active_screen() const noexcept {
    return !stack_.empty();
  }

  void update(UIContext& ctx);
  void append_to(UIContext& ctx);

  bool route_action(const UiAction& action, UIContext& ctx);
  bool handle_cancel(UIContext& ctx);
  bool handle_text_input(std::string_view text, UIContext& ctx);
  bool handle_backspace(UIContext& ctx);
  bool handle_submit(UIContext& ctx);

  [[nodiscard]] bool blocks_gameplay_input() const noexcept;
  [[nodiscard]] bool pauses_singleplayer() const noexcept;

private:
  std::vector<std::unique_ptr<UIScreen>> stack_{};
};

} // namespace arv::ui
