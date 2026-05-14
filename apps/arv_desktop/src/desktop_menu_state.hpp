#pragma once

#include "arv/client/local_identity.hpp"
#include "arv/io/config.hpp"
#include "arv/text/language.hpp"
#include "arv/platform/input_bindings.hpp"
#include "arv/base/ui.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::desktop {

class DesktopMenuState final : public ui::UIScreenDataSource,
                               public ui::UICommandSink {
public:
  DesktopMenuState(
      std::filesystem::path saves_root,
      std::filesystem::path server_list_file,
      std::filesystem::path arvane_home,
      const io::EngineConfig* config,
      const platform::InputBindings* input_bindings,
      const arv::text::LanguageCatalog* language);

  [[nodiscard]] std::vector<ui::SinglePlayerSaveEntry> singleplayer_saves() const override;
  [[nodiscard]] std::vector<ui::ServerListEntry> multiplayer_servers() const override;
  [[nodiscard]] std::vector<ui::LocalProfileEntry> local_profiles() const override;
  [[nodiscard]] ui::PlayerSkinTextureView default_player_skin_texture() const override;
  [[nodiscard]] ui::PlayerSkinTextureView pending_create_profile_skin_texture() const override;
  [[nodiscard]] std::string pending_create_profile_skin_hash() const override;
  [[nodiscard]] std::vector<ui::LocalProfileSkinEntry> pending_create_profile_skins()
      const override;
  [[nodiscard]] bool has_pending_edit_profile_skins(
      std::string_view profile_id) const override;
  [[nodiscard]] std::vector<ui::LocalProfileSkinEntry> pending_edit_profile_skins(
      std::string_view profile_id) const override;
  [[nodiscard]] ui::PlayerSkinTextureView pending_edit_profile_skin_texture(
      std::string_view profile_id) const override;
  [[nodiscard]] std::string pending_edit_profile_skin_hash(
      std::string_view profile_id) const override;
  [[nodiscard]] std::string local_profile_status() const override;
  [[nodiscard]] ui::RenderSettingsView render_settings() const override;
  [[nodiscard]] ui::ControlsSettingsView controls_settings() const override;
  [[nodiscard]] ui::InterfaceSettingsView interface_settings() const override;
  [[nodiscard]] std::vector<ui::KeyBindEntryView> key_bind_entries() const override;

  void submit(ui::UICommand command) override;

  void refresh_worlds();
  void set_player_skin_texture_views(
      HashMap<std::string, ui::PlayerSkinTextureView> textures,
      ui::PlayerSkinTextureView default_texture);

  [[nodiscard]] const client::LocalProfileStore& local_profile_store() const noexcept {
    return profile_store_;
  }
  [[nodiscard]] const client::LocalPlayerIdentity* active_local_profile() const noexcept;
  [[nodiscard]] const std::vector<client::LocalProfileSkin>& pending_create_skins() const noexcept {
    return pending_create_skins_;
  }
  [[nodiscard]] const HashMap<std::string, std::vector<client::LocalProfileSkin>>&
  pending_edit_skins()
      const noexcept {
    return pending_edit_skins_;
  }

  [[nodiscard]] std::optional<ui::StartSinglePlayerCommand>
  take_start_singleplayer_request() noexcept;
  [[nodiscard]] std::optional<ui::CreateWorldCommand>
  take_create_world_request() noexcept;
  [[nodiscard]] std::optional<ui::DeleteSinglePlayerSaveCommand>
  take_delete_save_request() noexcept;
  [[nodiscard]] std::optional<ui::JoinServerCommand>
  take_join_server_request() noexcept;
  [[nodiscard]] bool take_cancel_server_connect_request() noexcept;
  [[nodiscard]] bool take_back_to_main_menu_request() noexcept;
  [[nodiscard]] std::optional<ui::SetRenderSettingsCommand>
  take_render_settings_request() noexcept;
  [[nodiscard]] std::optional<ui::SetControlsSettingsCommand>
  take_controls_settings_request() noexcept;
  [[nodiscard]] std::optional<ui::SetInterfaceSettingsCommand>
  take_interface_settings_request() noexcept;
  [[nodiscard]] std::optional<ui::SetKeyBindCommand>
  take_key_bind_request() noexcept;
  [[nodiscard]] std::optional<ui::ResetKeyBindCommand>
  take_key_bind_reset_request() noexcept;

  std::vector<ui::SinglePlayerSaveEntry> saves{};
  std::vector<ui::ServerListEntry> servers{};
  bool quit_requested{};

private:
  void handle(const ui::StartSinglePlayerCommand& command);
  void handle(const ui::CreateWorldCommand& command);
  void handle(const ui::DeleteSinglePlayerSaveCommand& command);
  void handle(const ui::JoinServerCommand& command);
  void handle(const ui::CancelServerConnectCommand& command);
  void handle(const ui::SaveServerEntryCommand& command);
  void handle(const ui::RemoveServerEntryCommand& command);
  void handle(const ui::SelectLocalProfileCommand& command);
  void handle(const ui::UpdateLocalProfileCommand& command);
  void handle(const ui::PickLocalProfileSkinCommand& command);
  void handle(const ui::SetLocalProfileSkinOrderCommand& command);
  void handle(const ui::SetLocalProfileSkinSlimCommand& command);
  void handle(const ui::CreateLocalProfileCommand& command);
  void handle(const ui::DeleteLocalProfileCommand& command);
  void handle(const ui::DiscardLocalProfileEditCommand& command);
  void handle(const ui::DiscardLocalProfileCreateCommand& command);
  void handle(const ui::ResumeGameCommand& command);
  void handle(const ui::BackToMainMenuCommand& command);
  void handle(const ui::QuitGameCommand& command);
  void handle(const ui::SetRenderSettingsCommand& command);
  void handle(const ui::SetControlsSettingsCommand& command);
  void handle(const ui::SetInterfaceSettingsCommand& command);
  void handle(const ui::SetKeyBindCommand& command);
  void handle(const ui::ResetKeyBindCommand& command);

  void load_servers();
  void persist_servers() const;
  void load_profiles();
  void persist_profiles();
  [[nodiscard]] client::LocalPlayerIdentity* profile_by_id(std::string_view profile_id) noexcept;
  [[nodiscard]] const client::LocalPlayerIdentity* profile_by_id(
      std::string_view profile_id) const noexcept;
  [[nodiscard]] std::optional<client::LocalProfileSkin> pick_and_import_skin();
  [[nodiscard]] ui::LocalProfileSkinEntry skin_entry_from(
      const client::LocalProfileSkin& skin) const;
  [[nodiscard]] std::vector<ui::LocalProfileSkinEntry> skin_entries_from(
      const std::vector<client::LocalProfileSkin>& skins) const;
  [[nodiscard]] std::vector<client::LocalProfileSkin> current_skin_source(
      const std::optional<std::string>& profile_id) const;
  [[nodiscard]] std::vector<client::LocalProfileSkin>* mutable_skin_source(
      const std::optional<std::string>& profile_id);

  std::filesystem::path saves_root_{};
  std::filesystem::path server_list_file_{};
  std::filesystem::path arvane_home_{};
  const io::EngineConfig* config_{};
  const platform::InputBindings* input_bindings_{};
  const arv::text::LanguageCatalog* language_{};
  std::optional<ui::StartSinglePlayerCommand> start_singleplayer_request_{};
  std::optional<ui::CreateWorldCommand> create_world_request_{};
  std::optional<ui::DeleteSinglePlayerSaveCommand> delete_save_request_{};
  std::optional<ui::JoinServerCommand> join_server_request_{};
  std::optional<ui::SetRenderSettingsCommand> render_settings_request_{};
  std::optional<ui::SetControlsSettingsCommand> controls_settings_request_{};
  std::optional<ui::SetInterfaceSettingsCommand> interface_settings_request_{};
  std::optional<ui::SetKeyBindCommand> key_bind_request_{};
  std::optional<ui::ResetKeyBindCommand> key_bind_reset_request_{};
  bool cancel_server_connect_requested_{};
  bool back_to_main_menu_requested_{};
  client::LocalProfileStore profile_store_{};
  std::vector<client::LocalProfileSkin> pending_create_skins_{};
  HashMap<std::string, std::vector<client::LocalProfileSkin>> pending_edit_skins_{};
  HashMap<std::string, ui::PlayerSkinTextureView> skin_textures_{};
  ui::PlayerSkinTextureView default_skin_texture_{};
  std::string profile_status_{};
};

} // namespace arv::desktop
