#pragma once

#include "arv/ui/core/ui.hpp"

#include "arv/lang/language.hpp"

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
  bool slim_model{};
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
  bool slim_model{};
};

enum class SettingsFrameSyncMode : u8 {
  Off,
  VSync,
  FpsCap,
};

enum class InputBindingDevice : u8 {
  Keyboard,
  MouseButton,
  MouseWheel,
};

struct InputBindingView {
  bool valid{};
  InputBindingDevice device{InputBindingDevice::Keyboard};
  std::string input_code{};
  std::string label{};
  u8 modifiers{};
};

struct RenderSettingsView {
  SettingsFrameSyncMode frame_sync{SettingsFrameSyncMode::VSync};
  u32 fps_cap{60};
  i32 load_distance{3};
  i32 simulation_distance{1};
  i32 view_distance{2};
  i32 stream_distance{2};
  u32 mesh_upload_budget{8};
  bool show_local_player_body{true};
  f32 star_intensity{1.25F};
  f32 cloud_intensity{1.25F};
  f32 cirrus_amount{0.45F};
  f32 cumulus_amount{0.80F};
  f32 outline_red{0.02F};
  f32 outline_green{0.02F};
  f32 outline_blue{0.02F};
  f32 outline_thickness{0.55F};
};

struct ControlsSettingsView {
  f32 mouse_sensitivity_percent{100.0F};
};

struct InterfaceSettingsView {
  u32 chat_history_max{1000U};
  u32 chat_open_lines{8U};
  u32 chat_passive_lines{8U};
  f32 chat_passive_visible_seconds{8.0F};
  f32 chat_passive_fade_seconds{2.0F};
};

struct KeyBindSlotView {
  bool has_binding{};
  InputBindingView binding{};
  bool conflict{};
};

struct KeyBindEntryView {
  std::string action_name{};
  std::string display_name{};
  KeyBindSlotView primary{};
  KeyBindSlotView secondary{};
};

struct CapturedInputView {
  InputBindingDevice device{InputBindingDevice::Keyboard};
  std::string input_code{};
  std::string label{};
  u8 modifiers{};
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

struct CancelServerConnectCommand {};

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

struct SetLocalProfileSkinSlimCommand {
  std::optional<std::string> profile_id{};
  std::string skin_hash{};
  bool slim_model{};
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

struct SetRenderSettingsCommand {
  RenderSettingsView settings{};
};

struct SetControlsSettingsCommand {
  ControlsSettingsView settings{};
};

struct SetInterfaceSettingsCommand {
  InterfaceSettingsView settings{};
};

struct SetKeyBindCommand {
  std::string action_name{};
  u8 slot{};
  std::optional<InputBindingView> binding{};
};

struct ResetKeyBindCommand {
  std::string action_name{};
};

using UICommand = std::variant<
    StartSinglePlayerCommand,
    CreateWorldCommand,
    DeleteSinglePlayerSaveCommand,
    JoinServerCommand,
    CancelServerConnectCommand,
    SaveServerEntryCommand,
    RemoveServerEntryCommand,
    SelectLocalProfileCommand,
    UpdateLocalProfileCommand,
    PickLocalProfileSkinCommand,
    SetLocalProfileSkinOrderCommand,
    SetLocalProfileSkinSlimCommand,
    CreateLocalProfileCommand,
    DeleteLocalProfileCommand,
    DiscardLocalProfileEditCommand,
    DiscardLocalProfileCreateCommand,
    ResumeGameCommand,
    BackToMainMenuCommand,
    QuitGameCommand,
    SetRenderSettingsCommand,
    SetControlsSettingsCommand,
    SetInterfaceSettingsCommand,
    SetKeyBindCommand,
    ResetKeyBindCommand>;

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
  [[nodiscard]] virtual RenderSettingsView render_settings() const {
    return {};
  }
  [[nodiscard]] virtual ControlsSettingsView controls_settings() const {
    return {};
  }
  [[nodiscard]] virtual InterfaceSettingsView interface_settings() const {
    return {};
  }
  [[nodiscard]] virtual std::vector<KeyBindEntryView> key_bind_entries() const {
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
  const lang::LanguageCatalog* language{};
};

enum class MenuScreenType : u8 {
  Gameplay,
  MainMenu,
  SinglePlayerMenu,
  CreateWorld,
  MultiPlayerMenu,
  MultiplayerConnecting,
  MultiplayerDisconnect,
  AddServer,
  EditServer,
  AccountCreate,
  AccountEdit,
  SettingsMenu,
  RenderSettings,
  KeyBinds,
  ControlsSettings,
  InterfaceSettings,
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
  virtual bool handle_captured_input(const CapturedInputView& input, UIContext& ctx);
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
  [[nodiscard]] u64 revision() const noexcept {
    return revision_;
  }
  void mark_dirty() noexcept;

  void update(UIContext& ctx);
  void append_to(UIContext& ctx);

  bool route_action(const UiAction& action, UIContext& ctx);
  bool handle_cancel(UIContext& ctx);
  bool handle_text_input(std::string_view text, UIContext& ctx);
  bool handle_backspace(UIContext& ctx);
  bool handle_submit(UIContext& ctx);
  bool handle_captured_input(const CapturedInputView& input, UIContext& ctx);
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
  u64 revision_{};
};

} // namespace arv::ui
