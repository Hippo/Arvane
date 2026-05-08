#pragma once

#include "arv/core/expected.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/skins.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::client {

struct LocalIdentityError {
  std::string message{};
};

struct LocalProfileSkin {
  std::string skin_hash{};
  protocol::SkinImageFormat format{protocol::SkinImageFormat::Png};
  u16 width{};
  u16 height{};
  std::string file{};
  bool slim_model{};
};

struct LocalPlayerIdentity {
  std::filesystem::path arvane_home{};
  crypto::PlayerProfileID profile_id{};
  std::string display_name{"local_player"};
  std::string private_key_file{};
  crypto::Ed25519KeyPair key_pair{};
  std::vector<LocalProfileSkin> skins{};
};

struct LocalProfileStore {
  std::vector<LocalPlayerIdentity> profiles{};

  [[nodiscard]] const LocalPlayerIdentity* active_profile() const noexcept;
};

[[nodiscard]] LocalPlayerIdentity make_ephemeral_local_identity(
    std::string display_name = "local_player");

[[nodiscard]] Expected<LocalPlayerIdentity, LocalIdentityError> load_or_create_local_identity(
    const std::filesystem::path& arvane_home,
    std::string display_name = "local_player");

[[nodiscard]] Expected<LocalProfileStore, LocalIdentityError> load_or_create_local_profile_store(
    const std::filesystem::path& arvane_home,
    std::string display_name = "local_player");

[[nodiscard]] Expected<void, LocalIdentityError> save_local_profile_store(
    const std::filesystem::path& arvane_home,
    const LocalProfileStore& store);

[[nodiscard]] Expected<LocalProfileSkin, LocalIdentityError> import_local_profile_skin_png(
    const std::filesystem::path& arvane_home,
    const std::filesystem::path& source_png);

[[nodiscard]] const LocalProfileSkin* best_local_skin_for_max_resolution(
    const LocalPlayerIdentity& profile,
    u16 max_resolution) noexcept;

[[nodiscard]] std::filesystem::path local_profiles_json_path(
    const std::filesystem::path& arvane_home);
[[nodiscard]] std::filesystem::path local_private_key_path(
    const std::filesystem::path& arvane_home,
    std::string_view private_key_file);
[[nodiscard]] std::filesystem::path local_skin_path(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin);
[[nodiscard]] std::filesystem::path local_skins_directory_path(
    const std::filesystem::path& arvane_home);
[[nodiscard]] std::filesystem::path cached_skin_path(
    const std::filesystem::path& arvane_home,
    std::string_view skin_hash);

} // namespace arv::client
