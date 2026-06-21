#include "web_profile.hpp"

#include "web_platform.hpp"

#include <emscripten.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

inline constexpr std::string_view WebArvaneHome = "/arvane_home";
inline constexpr std::string_view DefaultWebDisplayName = "WebPlayer";
inline constexpr arv::usize MaxDisplayNameBytes = 32U;

// clang-format off
EM_JS(void, arv_web_apply_profile_state, (
    const char* display_name,
    const char* skin_hash,
    int skin_width,
    int skin_height,
    int slim_model), {
  if (typeof window.arvWebApplyProfileState !== 'function') {
    return;
  }
  window.arvWebApplyProfileState({
    displayName: UTF8ToString(display_name),
    skinHash: UTF8ToString(skin_hash),
    skinWidth: skin_width,
    skinHeight: skin_height,
    slimModel: slim_model !== 0
  });
});
// clang-format on

[[nodiscard]] std::filesystem::path web_home_path() {
  return std::filesystem::path{std::string{WebArvaneHome}};
}

[[nodiscard]] std::string trim_ascii(std::string value) {
  const auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  const auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  const auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string{begin, end};
}

[[nodiscard]] std::string normalize_display_name(const char* display_name) {
  std::string name = trim_ascii(display_name == nullptr ? std::string{} : display_name);
  if (name.empty()) {
    name = std::string{DefaultWebDisplayName};
  }
  if (name.size() > MaxDisplayNameBytes) {
    name.resize(MaxDisplayNameBytes);
  }
  return name;
}

[[nodiscard]] arv::Expected<arv::client::LocalProfileStore, arv::client::LocalIdentityError>
load_or_create_web_profiles(std::string display_name = std::string{DefaultWebDisplayName}) {
  return arv::client::load_or_create_local_profile_store(
      web_home_path(),
      std::move(display_name));
}

[[nodiscard]] arv::client::LocalPlayerIdentity* active_profile(
    arv::client::LocalProfileStore& store) noexcept {
  if (store.profiles.empty()) {
    return nullptr;
  }
  return &store.profiles[store.active_profile_index()];
}

void publish_profile_state(const arv::client::LocalPlayerIdentity& profile) {
  const arv::client::LocalProfileSkin* skin =
      profile.skins.empty() ? nullptr : &profile.skins.front();
  const std::string empty_skin_hash;
  arv_web_apply_profile_state(
      profile.display_name.c_str(),
      skin == nullptr ? empty_skin_hash.c_str() : skin->skin_hash.c_str(),
      skin == nullptr ? 0 : static_cast<int>(skin->width),
      skin == nullptr ? 0 : static_cast<int>(skin->height),
      skin != nullptr && skin->slim_model ? 1 : 0);
}

void report_profile_error(std::string_view prefix, const arv::client::LocalIdentityError& error) {
  arv::web::set_status(std::string{prefix} + ": " + error.message);
}

[[nodiscard]] bool save_profiles(
    const arv::client::LocalProfileStore& store,
    std::string_view success_status) {
  auto saved = arv::client::save_local_profile_store(web_home_path(), store);
  if (!saved) {
    report_profile_error("profile save failed", saved.error());
    return false;
  }
  arv::web::flush_storage();
  if (!success_status.empty()) {
    arv::web::set_status(std::string{success_status});
  }
  return true;
}

} // namespace

namespace arv::web {

void refresh_web_profile_ui() {
  if (!storage_ready()) {
    return;
  }
  auto store = load_or_create_web_profiles();
  if (!store) {
    report_profile_error("profile load failed", store.error());
    return;
  }
  const client::LocalPlayerIdentity* profile = store.value().active_profile();
  if (profile == nullptr) {
    set_status("profile load failed: no active profile");
    return;
  }
  publish_profile_state(*profile);
}

bool update_web_profile_name(const char* display_name) {
  if (!storage_ready()) {
    set_status("browser storage is not ready");
    return false;
  }
  auto store = load_or_create_web_profiles(normalize_display_name(display_name));
  if (!store) {
    report_profile_error("profile load failed", store.error());
    return false;
  }
  client::LocalPlayerIdentity* profile = active_profile(store.value());
  if (profile == nullptr) {
    set_status("profile save failed: no active profile");
    return false;
  }
  profile->display_name = normalize_display_name(display_name);
  const bool saved = save_profiles(store.value(), "profile saved");
  if (saved) {
    publish_profile_state(*profile);
  }
  return saved;
}

bool import_web_profile_skin(const char* source_png, bool slim_model) {
  if (!storage_ready()) {
    set_status("browser storage is not ready");
    return false;
  }
  if (source_png == nullptr || std::string_view{source_png}.empty()) {
    set_status("skin import failed: missing PNG path");
    return false;
  }

  auto store = load_or_create_web_profiles();
  if (!store) {
    report_profile_error("profile load failed", store.error());
    return false;
  }
  client::LocalPlayerIdentity* profile = active_profile(store.value());
  if (profile == nullptr) {
    set_status("skin import failed: no active profile");
    return false;
  }

  auto imported =
      client::import_local_profile_skin_png(web_home_path(), std::filesystem::path{source_png});
  std::error_code remove_error;
  std::filesystem::remove(std::filesystem::path{source_png}, remove_error);
  if (!imported) {
    report_profile_error("skin import failed", imported.error());
    return false;
  }

  imported.value().slim_model = slim_model;
  profile->skins.erase(
      std::remove_if(
          profile->skins.begin(),
          profile->skins.end(),
          [&](const client::LocalProfileSkin& skin) {
            return skin.skin_hash == imported.value().skin_hash;
          }),
      profile->skins.end());
  profile->skins.insert(profile->skins.begin(), std::move(imported.value()));

  const bool saved = save_profiles(store.value(), "skin saved for next connect");
  if (saved) {
    publish_profile_state(*profile);
  }
  return saved;
}

bool set_web_profile_skin_model(bool slim_model) {
  if (!storage_ready()) {
    set_status("browser storage is not ready");
    return false;
  }
  auto store = load_or_create_web_profiles();
  if (!store) {
    report_profile_error("profile load failed", store.error());
    return false;
  }
  client::LocalPlayerIdentity* profile = active_profile(store.value());
  if (profile == nullptr || profile->skins.empty()) {
    set_status("skin model saved for default skin");
    refresh_web_profile_ui();
    return true;
  }
  profile->skins.front().slim_model = slim_model;
  const bool saved = save_profiles(store.value(), "skin model saved");
  if (saved) {
    publish_profile_state(*profile);
  }
  return saved;
}

Expected<client::LocalPlayerIdentity, client::LocalIdentityError> load_web_local_identity() {
  return client::load_or_create_local_identity(
      web_home_path(),
      std::string{DefaultWebDisplayName});
}

} // namespace arv::web

extern "C" {

EMSCRIPTEN_KEEPALIVE void arv_web_refresh_profile() {
  arv::web::refresh_web_profile_ui();
}

EMSCRIPTEN_KEEPALIVE int arv_web_set_player_name(const char* display_name) {
  return arv::web::update_web_profile_name(display_name) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int arv_web_import_skin(const char* source_png, int slim_model) {
  return arv::web::import_web_profile_skin(source_png, slim_model != 0) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int arv_web_set_skin_model(int slim_model) {
  return arv::web::set_web_profile_skin_model(slim_model != 0) ? 1 : 0;
}

} // extern "C"
