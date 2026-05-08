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

struct PlayerSkinTextureView {
  bool valid{};
  u32 page{};
  u16 atlas_width{};
  u16 atlas_height{};
  u16 x{};
  u16 y{};
  u16 width{};
  u16 height{};
  u16 image_width{};
  u16 image_height{};
};

struct LocalProfileSkinEntry {
  std::string skin_hash{};
  u32 width{};
  u32 height{};
  PlayerSkinTextureView skin_texture{};
};

struct LocalProfileEntry {
  std::string profile_id{};
  std::string display_name{};
  std::string selected_skin_hash{};
  PlayerSkinTextureView skin_texture{};
  bool active{};
  std::vector<LocalProfileSkinEntry> skins{};
};

struct PlayerSkinPreviewRequest {
  std::string element_id{};
  std::string skin_hash{};
  f32 yaw_radians{};
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

struct SelectLocalProfileCommand {
  std::string profile_id{};
};

struct UpdateLocalProfileCommand {
  std::string profile_id{};
  std::string display_name{};
};

struct PickLocalProfileSkinCommand {
  std::optional<std::string> profile_id{};
};

struct SetLocalProfileSkinOrderCommand {
  std::optional<std::string> profile_id{};
  std::vector<std::string> skin_hashes{};
};

struct CreateLocalProfileCommand {
  std::string display_name{};
};

struct DeleteLocalProfileCommand {
  std::string profile_id{};
};

struct DiscardLocalProfileEditCommand {
  std::string profile_id{};
};

struct DiscardLocalProfileCreateCommand {};

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
    SelectLocalProfileCommand,
    UpdateLocalProfileCommand,
    PickLocalProfileSkinCommand,
    SetLocalProfileSkinOrderCommand,
    CreateLocalProfileCommand,
    DeleteLocalProfileCommand,
    DiscardLocalProfileEditCommand,
    DiscardLocalProfileCreateCommand,
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
  [[nodiscard]] virtual std::vector<LocalProfileEntry> local_profiles() const {
    return {};
  }
  [[nodiscard]] virtual PlayerSkinTextureView default_player_skin_texture() const {
    return {};
  }
  [[nodiscard]] virtual PlayerSkinTextureView pending_create_profile_skin_texture() const {
    return {};
  }
  [[nodiscard]] virtual std::string pending_create_profile_skin_hash() const {
    return {};
  }
  [[nodiscard]] virtual std::vector<LocalProfileSkinEntry> pending_create_profile_skins() const {
    return {};
  }
  [[nodiscard]] virtual bool has_pending_edit_profile_skins(
      std::string_view profile_id) const {
    (void)profile_id;
    return false;
  }
  [[nodiscard]] virtual std::vector<LocalProfileSkinEntry> pending_edit_profile_skins(
      std::string_view profile_id) const {
    (void)profile_id;
    return {};
  }
  [[nodiscard]] virtual PlayerSkinTextureView pending_edit_profile_skin_texture(
      std::string_view profile_id) const {
    (void)profile_id;
    return {};
  }
  [[nodiscard]] virtual std::string pending_edit_profile_skin_hash(
      std::string_view profile_id) const {
    (void)profile_id;
    return {};
  }
  [[nodiscard]] virtual std::string local_profile_status() const {
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
  const UiTheme* theme{};
};

enum class MenuScreenType : u8 {
  MainMenu,
  SinglePlayerMenu,
  CreateWorld,
  MultiPlayerMenu,
  AddServer,
  EditServer,
  AccountCreate,
  AccountEdit,
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
  virtual bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx);
  virtual bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx);
  [[nodiscard]] virtual std::optional<PlayerSkinPreviewRequest> player_skin_preview(
      const UIContext& ctx) const;

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
  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx);
  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx);

  [[nodiscard]] bool blocks_gameplay_input() const noexcept;
  [[nodiscard]] bool pauses_singleplayer() const noexcept;

private:
  std::vector<std::unique_ptr<UIScreen>> stack_{};
};

} // namespace arv::ui
