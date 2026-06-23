#include "desktop_menu_state.hpp"

#include "arv/client/client_settings.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/persistence/save.hpp"
#include "arv/player/player_constants.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>
#include <variant>

#if ARVANE_HAS_NFD
#include <nfd.h>
#endif

namespace arv::desktop {
namespace {

[[nodiscard]] std::string desktop_text(
    const arv::text::LanguageCatalog* language,
    std::string_view key) {
  return language == nullptr ? std::string{key} : language->text(key);
}

[[nodiscard]] std::optional<std::filesystem::path> open_png_picker(
    const std::filesystem::path& default_path,
    const arv::text::LanguageCatalog* language,
    std::string& error_message) {
#if ARVANE_HAS_NFD
  const nfdresult_t init_result = NFD_Init();
  if (init_result != NFD_OKAY) {
    const char* error = NFD_GetError();
    error_message = error == nullptr
                        ? desktop_text(language, "ui.menu.account.error_file_dialog_init")
                        : error;
    return std::nullopt;
  }

  nfdu8char_t* out_path = nullptr;
  const std::string png_filter = desktop_text(language, "ui.menu.account.png_filter");
  const nfdu8filteritem_t filter_items[1] = {{png_filter.c_str(), "png"}};
  const std::string default_path_text = default_path.string();
  nfdopendialogu8args_t args = {0};
  args.filterList = filter_items;
  args.filterCount = 1;
  args.defaultPath = default_path_text.c_str();
  const nfdresult_t result = NFD_OpenDialogU8_With(&out_path, &args);
  if (result == NFD_OKAY) {
    std::filesystem::path selected{out_path == nullptr ? "" : out_path};
    NFD_FreePathU8(out_path);
    NFD_Quit();
    return selected;
  }
  if (result == NFD_CANCEL) {
    NFD_Quit();
    return std::nullopt;
  }

  const char* error = NFD_GetError();
  error_message = error == nullptr
                      ? desktop_text(language, "ui.menu.account.error_file_dialog")
                      : error;
  NFD_Quit();
  return std::nullopt;
#else
  (void)default_path;
  error_message = desktop_text(language, "ui.menu.account.error_file_picker_unavailable");
  return std::nullopt;
#endif
}

[[nodiscard]] std::string display_name_or_default(std::string name) {
  return name.empty() ? std::string{"local_player"} : std::move(name);
}

} // namespace

DesktopMenuState::DesktopMenuState(
    std::filesystem::path saves_root,
    std::filesystem::path server_list_file,
    std::filesystem::path arvane_home,
    const io::EngineConfig* config,
    const platform::InputBindings* input_bindings,
    const arv::text::LanguageCatalog* language) : saves_root_(std::move(saves_root)),
                                             server_list_file_(std::move(server_list_file)),
                                             arvane_home_(std::move(arvane_home)),
                                             config_(config),
                                             input_bindings_(input_bindings),
                                             language_(language) {
  refresh_worlds();
  load_servers();
  load_profiles();
}

std::vector<ui::SinglePlayerSaveEntry> DesktopMenuState::singleplayer_saves() const {
  return saves;
}

std::vector<ui::ServerListEntry> DesktopMenuState::multiplayer_servers() const {
  return servers;
}

std::vector<ui::LocalProfileEntry> DesktopMenuState::local_profiles() const {
  std::vector<ui::LocalProfileEntry> profiles;
  profiles.reserve(profile_store_.profiles.size());
  for (usize index = 0; index < profile_store_.profiles.size(); ++index) {
    const client::LocalPlayerIdentity& profile = profile_store_.profiles[index];
    ui::LocalProfileEntry entry;
    entry.profile_id = profile.profile_id.string();
    entry.display_name = display_name_or_default(profile.display_name);
    entry.active = index == profile_store_.active_profile_index();
    entry.skins = skin_entries_from(profile.skins);
    if (!profile.skins.empty()) {
      entry.selected_skin_hash = profile.skins.front().skin_hash;
      const auto found = skin_textures_.find(entry.selected_skin_hash);
      if (found != skin_textures_.end()) {
        entry.skin_texture = found->second;
      }
    }
    profiles.push_back(std::move(entry));
  }
  return profiles;
}

ui::PlayerSkinTextureView DesktopMenuState::default_player_skin_texture() const {
  return default_skin_texture_;
}

ui::PlayerSkinTextureView DesktopMenuState::pending_create_profile_skin_texture() const {
  if (pending_create_skins_.empty()) {
    return {};
  }
  const auto found = skin_textures_.find(pending_create_skins_.front().skin_hash);
  return found == skin_textures_.end() ? ui::PlayerSkinTextureView{} : found->second;
}

std::string DesktopMenuState::pending_create_profile_skin_hash() const {
  return pending_create_skins_.empty() ? std::string{} : pending_create_skins_.front().skin_hash;
}

std::vector<ui::LocalProfileSkinEntry> DesktopMenuState::pending_create_profile_skins() const {
  return skin_entries_from(pending_create_skins_);
}

bool DesktopMenuState::has_pending_edit_profile_skins(std::string_view profile_id) const {
  return pending_edit_skins_.find(std::string{profile_id}) != pending_edit_skins_.end();
}

std::vector<ui::LocalProfileSkinEntry> DesktopMenuState::pending_edit_profile_skins(
    std::string_view profile_id) const {
  const auto pending = pending_edit_skins_.find(std::string{profile_id});
  return pending == pending_edit_skins_.end() ? std::vector<ui::LocalProfileSkinEntry>{}
                                             : skin_entries_from(pending->second);
}

ui::PlayerSkinTextureView DesktopMenuState::pending_edit_profile_skin_texture(
    std::string_view profile_id) const {
  const auto pending = pending_edit_skins_.find(std::string{profile_id});
  if (pending == pending_edit_skins_.end() || pending->second.empty()) {
    return {};
  }
  const auto found = skin_textures_.find(pending->second.front().skin_hash);
  return found == skin_textures_.end() ? ui::PlayerSkinTextureView{} : found->second;
}

std::string DesktopMenuState::pending_edit_profile_skin_hash(
    std::string_view profile_id) const {
  const auto pending = pending_edit_skins_.find(std::string{profile_id});
  return pending == pending_edit_skins_.end() || pending->second.empty()
             ? std::string{}
             : pending->second.front().skin_hash;
}

std::string DesktopMenuState::local_profile_status() const {
  return profile_status_;
}

ui::RenderSettingsView DesktopMenuState::render_settings() const {
  if (config_ == nullptr) {
    return {};
  }
  ui::RenderSettingsView view;
  if (config_->render.frame_pacing_enabled) {
    view.frame_sync = ui::SettingsFrameSyncMode::FpsCap;
  } else if (config_->window.vsync) {
    view.frame_sync = ui::SettingsFrameSyncMode::VSync;
  } else {
    view.frame_sync = ui::SettingsFrameSyncMode::Off;
  }
  view.fps_cap = config_->render.frame_pacing_target_fps;
  view.load_distance = config_->chunks.load_distance;
  view.simulation_distance = config_->chunks.simulation_distance;
  view.view_distance = config_->chunks.render_distance;
  view.stream_distance = config_->chunks.stream_distance;
  view.mesh_upload_budget = config_->render.max_chunk_mesh_uploads_per_frame;
  view.show_local_player_body = config_->render.show_local_player_body;
  view.shadows_enabled = config_->render.shadows.enabled;
  view.star_intensity = config_->render.sky.star_intensity;
  view.cloud_intensity = config_->render.sky.cloud_intensity;
  view.cirrus_amount = config_->render.sky.cirrus_amount;
  view.cumulus_amount = config_->render.sky.cumulus_amount;
  view.outline_red = config_->render.block_outline.color_r;
  view.outline_green = config_->render.block_outline.color_g;
  view.outline_blue = config_->render.block_outline.color_b;
  view.outline_thickness = config_->render.block_outline.thickness;
  return view;
}

ui::ControlsSettingsView DesktopMenuState::controls_settings() const {
  if (config_ == nullptr) {
    return {};
  }
  return {
      client::mouse_sensitivity_percent_from_radians(
          config_->physics.mouse_sensitivity_radians,
          player::DefaultMouseSensitivityRadians)};
}

ui::InterfaceSettingsView DesktopMenuState::interface_settings() const {
  if (config_ == nullptr) {
    return {};
  }
  return {
      config_->ui.chat.max_history_messages,
      config_->ui.chat.open_max_lines,
      config_->ui.chat.passive_max_lines,
      config_->ui.chat.passive_visible_seconds,
      config_->ui.chat.passive_fade_seconds};
}

std::vector<ui::KeyBindEntryView> DesktopMenuState::key_bind_entries() const {
  if (input_bindings_ == nullptr) {
    return {};
  }
  return client::key_bind_entries_from_input_bindings(*input_bindings_);
}

const client::LocalPlayerIdentity* DesktopMenuState::active_local_profile() const noexcept {
  return profile_store_.active_profile();
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

void DesktopMenuState::set_player_skin_texture_views(
    HashMap<std::string, ui::PlayerSkinTextureView> textures,
    ui::PlayerSkinTextureView default_texture) {
  skin_textures_ = std::move(textures);
  default_skin_texture_ = default_texture;
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

bool DesktopMenuState::take_cancel_server_connect_request() noexcept {
  return std::exchange(cancel_server_connect_requested_, false);
}

bool DesktopMenuState::take_back_to_main_menu_request() noexcept {
  return std::exchange(back_to_main_menu_requested_, false);
}

std::optional<ui::SetRenderSettingsCommand>
DesktopMenuState::take_render_settings_request() noexcept {
  return std::exchange(render_settings_request_, std::nullopt);
}

std::optional<ui::SetControlsSettingsCommand>
DesktopMenuState::take_controls_settings_request() noexcept {
  return std::exchange(controls_settings_request_, std::nullopt);
}

std::optional<ui::SetInterfaceSettingsCommand>
DesktopMenuState::take_interface_settings_request() noexcept {
  return std::exchange(interface_settings_request_, std::nullopt);
}

std::optional<ui::SetKeyBindCommand>
DesktopMenuState::take_key_bind_request() noexcept {
  return std::exchange(key_bind_request_, std::nullopt);
}

std::optional<ui::ResetKeyBindCommand>
DesktopMenuState::take_key_bind_reset_request() noexcept {
  return std::exchange(key_bind_reset_request_, std::nullopt);
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

void DesktopMenuState::handle(const ui::CancelServerConnectCommand&) {
  cancel_server_connect_requested_ = true;
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

void DesktopMenuState::handle(const ui::SelectLocalProfileCommand& command) {
  const auto found = std::find_if(
      profile_store_.profiles.begin(),
      profile_store_.profiles.end(),
      [&](const client::LocalPlayerIdentity& profile) {
        return profile.profile_id.string() == command.profile_id;
      });
  if (found == profile_store_.profiles.end()) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }
  profile_store_.selected_profile =
      static_cast<usize>(std::distance(profile_store_.profiles.begin(), found));
  persist_profiles();
}

void DesktopMenuState::handle(const ui::UpdateLocalProfileCommand& command) {
  client::LocalPlayerIdentity* profile = profile_by_id(command.profile_id);
  if (profile == nullptr) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }
  profile->display_name = display_name_or_default(command.display_name);
  if (const auto pending = pending_edit_skins_.find(command.profile_id);
      pending != pending_edit_skins_.end()) {
    profile->skins = std::move(pending->second);
    pending_edit_skins_.erase(pending);
  }
  persist_profiles();
}

void DesktopMenuState::handle(const ui::PickLocalProfileSkinCommand& command) {
  std::optional<client::LocalProfileSkin> imported = pick_and_import_skin();
  if (!imported.has_value()) {
    return;
  }
  if (!command.profile_id.has_value()) {
    pending_create_skins_.erase(
        std::remove_if(
            pending_create_skins_.begin(),
            pending_create_skins_.end(),
            [&](const client::LocalProfileSkin& skin) {
              return skin.skin_hash == imported->skin_hash;
            }),
        pending_create_skins_.end());
    pending_create_skins_.insert(pending_create_skins_.begin(), std::move(imported.value()));
    profile_status_.clear();
    return;
  }

  client::LocalPlayerIdentity* profile = profile_by_id(command.profile_id.value());
  if (profile == nullptr) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }
  auto pending_it = pending_edit_skins_.find(command.profile_id.value());
  if (pending_it == pending_edit_skins_.end()) {
    pending_it =
        pending_edit_skins_.emplace(command.profile_id.value(), profile->skins).first;
  }
  std::vector<client::LocalProfileSkin>& pending = pending_it->second;
  pending.erase(
      std::remove_if(
          pending.begin(),
          pending.end(),
          [&](const client::LocalProfileSkin& skin) {
            return skin.skin_hash == imported->skin_hash;
          }),
      pending.end());
  pending.insert(pending.begin(), std::move(imported.value()));
  profile_status_.clear();
}

void DesktopMenuState::handle(const ui::SetLocalProfileSkinOrderCommand& command) {
  const std::vector<client::LocalProfileSkin> source = current_skin_source(command.profile_id);
  std::vector<client::LocalProfileSkin> ordered;
  ordered.reserve(command.skin_hashes.size());
  HashSet<std::string> seen;
  for (const std::string& skin_hash : command.skin_hashes) {
    if (!seen.insert(skin_hash).second) {
      continue;
    }
    const auto found = std::find_if(
        source.begin(),
        source.end(),
        [&](const client::LocalProfileSkin& skin) {
          return skin.skin_hash == skin_hash;
        });
    if (found != source.end()) {
      ordered.push_back(*found);
    }
  }

  if (!command.profile_id.has_value()) {
    pending_create_skins_ = std::move(ordered);
    profile_status_.clear();
    return;
  }

  if (profile_by_id(command.profile_id.value()) == nullptr) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }
  pending_edit_skins_[command.profile_id.value()] = std::move(ordered);
  profile_status_.clear();
}

void DesktopMenuState::handle(const ui::SetLocalProfileSkinSlimCommand& command) {
  std::vector<client::LocalProfileSkin>* skins = mutable_skin_source(command.profile_id);
  if (skins == nullptr) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }
  const auto found = std::find_if(
      skins->begin(),
      skins->end(),
      [&](const client::LocalProfileSkin& skin) {
        return skin.skin_hash == command.skin_hash;
      });
  if (found == skins->end()) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_skin_missing");
    return;
  }
  found->slim_model = command.slim_model;
  profile_status_.clear();
}

void DesktopMenuState::handle(const ui::CreateLocalProfileCommand& command) {
  client::LocalPlayerIdentity profile =
      client::make_ephemeral_local_identity(display_name_or_default(command.display_name));
  profile.arvane_home = arvane_home_;
  profile.private_key_file = profile.profile_id.string() + ".pk";
  profile.skins = std::move(pending_create_skins_);
  pending_create_skins_.clear();
  profile_store_.selected_profile = profile_store_.profiles.size();
  profile_store_.profiles.push_back(std::move(profile));
  persist_profiles();
}

void DesktopMenuState::handle(const ui::DeleteLocalProfileCommand& command) {
  const auto found = std::find_if(
      profile_store_.profiles.begin(),
      profile_store_.profiles.end(),
      [&](const client::LocalPlayerIdentity& profile) {
        return profile.profile_id.string() == command.profile_id;
      });
  if (found == profile_store_.profiles.end()) {
    profile_status_ = desktop_text(language_, "ui.menu.account.error_profile_missing");
    return;
  }

  const usize removed_index =
      static_cast<usize>(std::distance(profile_store_.profiles.begin(), found));
  const usize selected_index = profile_store_.active_profile_index();
  pending_edit_skins_.erase(command.profile_id);
  const std::filesystem::path key_path =
      client::local_private_key_path(arvane_home_, found->private_key_file);
  profile_store_.profiles.erase(found);
  if (profile_store_.profiles.empty() || removed_index == selected_index) {
    profile_store_.selected_profile = 0U;
  } else if (removed_index < selected_index) {
    profile_store_.selected_profile = selected_index - 1U;
  } else {
    profile_store_.selected_profile = selected_index;
  }
  persist_profiles();

  std::error_code error;
  std::filesystem::remove(key_path, error);
  if (error) {
    core::warn("identity", "failed to delete private key '{}': {}", key_path, error.message());
  }
}

void DesktopMenuState::handle(const ui::DiscardLocalProfileEditCommand& command) {
  pending_edit_skins_.erase(command.profile_id);
  profile_status_.clear();
}

void DesktopMenuState::handle(const ui::DiscardLocalProfileCreateCommand&) {
  pending_create_skins_.clear();
  profile_status_.clear();
}

void DesktopMenuState::handle(const ui::ResumeGameCommand&) {
}

void DesktopMenuState::handle(const ui::BackToMainMenuCommand&) {
  back_to_main_menu_requested_ = true;
}

void DesktopMenuState::handle(const ui::QuitGameCommand&) {
  quit_requested = true;
}

void DesktopMenuState::handle(const ui::SetRenderSettingsCommand& command) {
  render_settings_request_ = command;
}

void DesktopMenuState::handle(const ui::SetControlsSettingsCommand& command) {
  controls_settings_request_ = command;
}

void DesktopMenuState::handle(const ui::SetInterfaceSettingsCommand& command) {
  interface_settings_request_ = command;
}

void DesktopMenuState::handle(const ui::SetKeyBindCommand& command) {
  key_bind_request_ = command;
}

void DesktopMenuState::handle(const ui::ResetKeyBindCommand& command) {
  key_bind_reset_request_ = command;
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

void DesktopMenuState::load_profiles() {
  profile_status_.clear();
  auto loaded = client::load_local_profile_store(arvane_home_);
  if (!loaded) {
    profile_status_ = loaded.error().message;
    core::warn("identity", loaded.error().message);
    profile_store_.profiles.clear();
    return;
  }
  profile_store_ = std::move(loaded.value());
}

void DesktopMenuState::persist_profiles() {
  auto saved = client::save_local_profile_store(arvane_home_, profile_store_);
  if (!saved) {
    profile_status_ = saved.error().message;
    core::warn("identity", saved.error().message);
    return;
  }
  profile_status_.clear();
}

client::LocalPlayerIdentity* DesktopMenuState::profile_by_id(std::string_view profile_id) noexcept {
  for (client::LocalPlayerIdentity& profile : profile_store_.profiles) {
    if (profile.profile_id.string() == profile_id) {
      return &profile;
    }
  }
  return nullptr;
}

const client::LocalPlayerIdentity* DesktopMenuState::profile_by_id(
    std::string_view profile_id) const noexcept {
  for (const client::LocalPlayerIdentity& profile : profile_store_.profiles) {
    if (profile.profile_id.string() == profile_id) {
      return &profile;
    }
  }
  return nullptr;
}

ui::LocalProfileSkinEntry DesktopMenuState::skin_entry_from(
    const client::LocalProfileSkin& skin) const {
  ui::LocalProfileSkinEntry entry;
  entry.skin_hash = skin.skin_hash;
  entry.width = skin.width;
  entry.height = skin.height;
  entry.slim_model = skin.slim_model;
  const auto found = skin_textures_.find(skin.skin_hash);
  if (found != skin_textures_.end()) {
    entry.skin_texture = found->second;
  }
  return entry;
}

std::vector<ui::LocalProfileSkinEntry> DesktopMenuState::skin_entries_from(
    const std::vector<client::LocalProfileSkin>& skins) const {
  std::vector<ui::LocalProfileSkinEntry> entries;
  entries.reserve(skins.size());
  for (const client::LocalProfileSkin& skin : skins) {
    entries.push_back(skin_entry_from(skin));
  }
  return entries;
}

std::vector<client::LocalProfileSkin> DesktopMenuState::current_skin_source(
    const std::optional<std::string>& profile_id) const {
  if (!profile_id.has_value()) {
    return pending_create_skins_;
  }
  const auto pending = pending_edit_skins_.find(profile_id.value());
  if (pending != pending_edit_skins_.end()) {
    return pending->second;
  }
  const client::LocalPlayerIdentity* profile = profile_by_id(profile_id.value());
  return profile == nullptr ? std::vector<client::LocalProfileSkin>{} : profile->skins;
}

std::vector<client::LocalProfileSkin>* DesktopMenuState::mutable_skin_source(
    const std::optional<std::string>& profile_id) {
  if (!profile_id.has_value()) {
    return &pending_create_skins_;
  }
  auto pending = pending_edit_skins_.find(profile_id.value());
  if (pending != pending_edit_skins_.end()) {
    return &pending->second;
  }
  const client::LocalPlayerIdentity* profile = profile_by_id(profile_id.value());
  if (profile == nullptr) {
    return nullptr;
  }
  return &pending_edit_skins_.emplace(profile_id.value(), profile->skins).first->second;
}

std::optional<client::LocalProfileSkin> DesktopMenuState::pick_and_import_skin() {
  profile_status_.clear();
  std::error_code error;
  std::filesystem::create_directories(client::local_skins_directory_path(arvane_home_), error);
  if (error) {
    profile_status_ = language_ == nullptr
                          ? std::string{"ui.menu.account.error_create_skins_directory"}
                          : language_->format(
                                "ui.menu.account.error_create_skins_directory",
                                {{"message", error.message()}});
    return std::nullopt;
  }

  std::string picker_error;
  std::optional<std::filesystem::path> selected =
      open_png_picker(client::local_skins_directory_path(arvane_home_), language_, picker_error);
  if (!selected.has_value()) {
    if (!picker_error.empty()) {
      profile_status_ = picker_error;
      core::warn("identity", picker_error);
    }
    return std::nullopt;
  }

  auto imported = client::import_local_profile_skin_png(arvane_home_, selected.value());
  if (!imported) {
    profile_status_ = imported.error().message;
    core::warn("identity", imported.error().message);
    return std::nullopt;
  }
  return imported.value();
}

} // namespace arv::desktop
