#include "arv/client/local_identity.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::client {
namespace {

using json = nlohmann::json;

inline constexpr u32 LocalProfileVersion = 1;
inline constexpr std::string_view ProfilesFileName = "profiles.json";
inline constexpr std::string_view KeysDirectoryName = "keys";
inline constexpr std::string_view SkinsDirectoryName = "skins";
inline constexpr std::string_view CacheDirectoryName = "cache";
inline constexpr std::string_view OldIdentityDirectoryName = "identity";
inline constexpr std::string_view OldIdentityFileName = "local_identity.json";
inline constexpr std::string_view OldPrivateKeyFileName = "local_private_key.bin";

[[nodiscard]] std::filesystem::path normalize_home_path(
    const std::filesystem::path& maybe_home) {
  return maybe_home.filename() == OldIdentityDirectoryName
             ? maybe_home.parent_path()
             : maybe_home;
}

[[nodiscard]] std::filesystem::path keys_root(const std::filesystem::path& home) {
  return home / std::string{KeysDirectoryName};
}

[[nodiscard]] std::filesystem::path old_identity_root(const std::filesystem::path& home) {
  return home / std::string{OldIdentityDirectoryName};
}

[[nodiscard]] std::filesystem::path old_identity_json_path(const std::filesystem::path& home) {
  return old_identity_root(home) / std::string{OldIdentityFileName};
}

[[nodiscard]] std::filesystem::path old_private_key_path(const std::filesystem::path& home) {
  return old_identity_root(home) / std::string{OldPrivateKeyFileName};
}

[[nodiscard]] Expected<std::string, LocalIdentityError> read_text_file(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Expected<std::string, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local profile file: " + path.string()});
  }
  return Expected<std::string, LocalIdentityError>::success(
      std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}});
}

[[nodiscard]] Expected<crypto::Ed25519PrivateKey, LocalIdentityError> read_private_key(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local private key file: " + path.string()});
  }

  crypto::Ed25519PrivateKey private_key;
  input.read(
      reinterpret_cast<char*>(private_key.bytes.data()),
      static_cast<std::streamsize>(private_key.bytes.size()));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::failure(
        LocalIdentityError{"local private key file has the wrong size"});
  }
  return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::success(private_key);
}

[[nodiscard]] Expected<void, LocalIdentityError> write_private_key(
    const std::filesystem::path& path,
    const crypto::Ed25519PrivateKey& private_key) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create key directory: " + error.message()});
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write local private key file"});
  }
  output.write(
      reinterpret_cast<const char*>(private_key.bytes.data()),
      static_cast<std::streamsize>(private_key.bytes.size()));
  if (!output) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write local private key bytes"});
  }
  return Expected<void, LocalIdentityError>::success();
}

[[nodiscard]] Expected<crypto::Ed25519PublicKey, LocalIdentityError> public_key_from_hex(
    std::string_view text) {
  auto bytes = crypto::from_prefixed_lower_hex(text);
  if (!bytes) {
    return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::failure(
        LocalIdentityError{bytes.error().message});
  }
  if (bytes.value().size() != crypto::ed25519_public_key_size) {
    return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::failure(
        LocalIdentityError{"local profile public key has the wrong size"});
  }

  crypto::Ed25519PublicKey public_key;
  std::copy(bytes.value().begin(), bytes.value().end(), public_key.bytes.begin());
  return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::success(public_key);
}

[[nodiscard]] bool safe_relative_file_name(std::string_view file) noexcept {
  return !file.empty() &&
         file.find('/') == std::string_view::npos &&
         file.find('\\') == std::string_view::npos &&
         file.find("..") == std::string_view::npos;
}

[[nodiscard]] Expected<LocalProfileSkin, LocalIdentityError> skin_from_json(
    const json& node) {
  if (!node.is_object()) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin entry must be an object"});
  }

  LocalProfileSkin skin;
  skin.skin_hash = node.at("skin_hash").get<std::string>();
  if (!protocol::is_valid_skin_hash(skin.skin_hash)) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin hash must be lowercase 0x-prefixed 256-bit hex"});
  }
  const std::string format = node.value("format", std::string{"png"});
  if (format != "png") {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"unsupported local profile skin format: " + format});
  }
  skin.format = protocol::SkinImageFormat::Png;
  skin.width = node.at("width").get<u16>();
  skin.height = node.at("height").get<u16>();
  skin.file = node.at("file").get<std::string>();
  if (!safe_relative_file_name(skin.file)) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin file must be a safe relative file name"});
  }
  return Expected<LocalProfileSkin, LocalIdentityError>::success(std::move(skin));
}

[[nodiscard]] json skin_to_json(const LocalProfileSkin& skin) {
  return json{
      {"skin_hash", skin.skin_hash},
      {"format", "png"},
      {"width", skin.width},
      {"height", skin.height},
      {"file", skin.file},
  };
}

[[nodiscard]] Expected<LocalPlayerIdentity, LocalIdentityError> profile_from_json(
    const std::filesystem::path& home,
    const json& node) {
  if (!node.is_object()) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile entry must be an object"});
  }
  if (node.at("version").get<u32>() != LocalProfileVersion) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"unsupported local profile version"});
  }

  LocalPlayerIdentity profile;
  profile.arvane_home = home;
  profile.display_name = node.value("display_name", std::string{"local_player"});
  profile.private_key_file = node.at("private_key_file").get<std::string>();
  if (!safe_relative_file_name(profile.private_key_file)) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile private key file must be a safe relative file name"});
  }

  auto public_key = public_key_from_hex(node.at("public_key").get<std::string>());
  auto private_key = read_private_key(local_private_key_path(home, profile.private_key_file));
  if (!public_key) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(public_key.error());
  }
  if (!private_key) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(private_key.error());
  }

  profile.key_pair.public_key = public_key.value();
  profile.key_pair.private_key = private_key.value();
  profile.profile_id = crypto::derive_profile_id_from_public_key(profile.key_pair.public_key);
  auto saved_profile_id =
      crypto::parse_player_profile_id(node.at("profile_id").get<std::string>());
  if (!saved_profile_id) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{saved_profile_id.error().message});
  }
  if (saved_profile_id.value() != profile.profile_id) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile id does not match public key"});
  }

  if (const auto skins = node.find("skins"); skins != node.end()) {
    if (!skins->is_array()) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
          LocalIdentityError{"local profile skins must be an array"});
    }
    profile.skins.reserve(skins->size());
    for (const json& skin_node : *skins) {
      auto skin = skin_from_json(skin_node);
      if (!skin) {
        return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(skin.error());
      }
      profile.skins.push_back(std::move(skin.value()));
    }
  }
  return Expected<LocalPlayerIdentity, LocalIdentityError>::success(std::move(profile));
}

[[nodiscard]] json profile_to_json(const LocalPlayerIdentity& profile) {
  json skins = json::array();
  for (const LocalProfileSkin& skin : profile.skins) {
    skins.push_back(skin_to_json(skin));
  }
  return json{
      {"version", LocalProfileVersion},
      {"display_name", profile.display_name},
      {"private_key_file", profile.private_key_file},
      {"profile_id", profile.profile_id.string()},
      {"public_key", crypto::to_prefixed_lower_hex(profile.key_pair.public_key.bytes)},
      {"skins", std::move(skins)},
  };
}

[[nodiscard]] Expected<LocalProfileStore, LocalIdentityError> read_profile_store(
    const std::filesystem::path& home) {
  auto text = read_text_file(local_profiles_json_path(home));
  if (!text) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(text.error());
  }

  try {
    const json root = json::parse(text.value());
    if (!root.is_array()) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{"profiles.json must contain an array"});
    }
    LocalProfileStore store;
    store.profiles.reserve(root.size());
    for (const json& profile_node : root) {
      auto profile = profile_from_json(home, profile_node);
      if (!profile) {
        return Expected<LocalProfileStore, LocalIdentityError>::failure(profile.error());
      }
      store.profiles.push_back(std::move(profile.value()));
    }
    return Expected<LocalProfileStore, LocalIdentityError>::success(std::move(store));
  } catch (const json::exception& ex) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to parse profiles.json: " + std::string{ex.what()}});
  }
}

[[nodiscard]] LocalPlayerIdentity make_persistent_identity(std::string display_name) {
  LocalPlayerIdentity identity = make_ephemeral_local_identity(std::move(display_name));
  identity.private_key_file = identity.profile_id.string() + ".pk";
  return identity;
}

[[nodiscard]] Expected<LocalProfileStore, LocalIdentityError> migrate_old_identity(
    const std::filesystem::path& home) {
  auto old_text = read_text_file(old_identity_json_path(home));
  if (!old_text) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(old_text.error());
  }

  try {
    const json old = json::parse(old_text.value());
    auto public_key = public_key_from_hex(old.at("public_key").get<std::string>());
    auto private_key = read_private_key(
        old_identity_root(home) / old.value("private_key_file", std::string{OldPrivateKeyFileName}));
    if (!public_key) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(public_key.error());
    }
    if (!private_key) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(private_key.error());
    }

    LocalPlayerIdentity profile;
    profile.arvane_home = home;
    profile.key_pair.public_key = public_key.value();
    profile.key_pair.private_key = private_key.value();
    profile.profile_id = crypto::derive_profile_id_from_public_key(profile.key_pair.public_key);
    profile.display_name = old.value("display_name", std::string{"local_player"});
    profile.private_key_file = profile.profile_id.string() + ".pk";
    auto saved_profile_id =
        crypto::parse_player_profile_id(old.at("profile_id").get<std::string>());
    if (!saved_profile_id) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{saved_profile_id.error().message});
    }
    if (saved_profile_id.value() != profile.profile_id) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{"old local identity profile id does not match public key"});
    }

    LocalProfileStore store;
    store.profiles.push_back(std::move(profile));
    auto save_result = save_local_profile_store(home, store);
    if (!save_result) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(save_result.error());
    }
    return Expected<LocalProfileStore, LocalIdentityError>::success(std::move(store));
  } catch (const json::exception& ex) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to parse old local identity JSON: " + std::string{ex.what()}});
  }
}

} // namespace

const LocalPlayerIdentity* LocalProfileStore::active_profile() const noexcept {
  return profiles.empty() ? nullptr : &profiles.front();
}

std::filesystem::path local_profiles_json_path(const std::filesystem::path& arvane_home) {
  return normalize_home_path(arvane_home) / std::string{ProfilesFileName};
}

std::filesystem::path local_private_key_path(
    const std::filesystem::path& arvane_home,
    std::string_view private_key_file) {
  return keys_root(normalize_home_path(arvane_home)) / std::string{private_key_file};
}

std::filesystem::path local_skin_path(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin) {
  return normalize_home_path(arvane_home) / std::string{SkinsDirectoryName} / skin.file;
}

std::filesystem::path cached_skin_path(
    const std::filesystem::path& arvane_home,
    std::string_view skin_hash) {
  return normalize_home_path(arvane_home) / std::string{CacheDirectoryName} /
         std::string{SkinsDirectoryName} / (std::string{skin_hash} + ".png");
}

LocalPlayerIdentity make_ephemeral_local_identity(std::string display_name) {
  LocalPlayerIdentity identity;
  identity.display_name = display_name.empty() ? "local_player" : std::move(display_name);
  identity.key_pair = crypto::generate_ed25519_keypair();
  identity.profile_id = crypto::derive_profile_id_from_public_key(identity.key_pair.public_key);
  identity.private_key_file = identity.profile_id.string() + ".pk";
  return identity;
}

Expected<void, LocalIdentityError> save_local_profile_store(
    const std::filesystem::path& arvane_home,
    const LocalProfileStore& store) {
  const std::filesystem::path home = normalize_home_path(arvane_home);
  std::error_code error;
  std::filesystem::create_directories(home, error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create arvane home directory: " + error.message()});
  }
  std::filesystem::create_directories(keys_root(home), error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create key directory: " + error.message()});
  }
  std::filesystem::create_directories(home / std::string{SkinsDirectoryName}, error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create skins directory: " + error.message()});
  }
  std::filesystem::create_directories(
      home / std::string{CacheDirectoryName} / std::string{SkinsDirectoryName},
      error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create skin cache directory: " + error.message()});
  }

  json root = json::array();
  for (const LocalPlayerIdentity& profile : store.profiles) {
    if (profile.private_key_file.empty() ||
        !safe_relative_file_name(profile.private_key_file)) {
      return Expected<void, LocalIdentityError>::failure(
          LocalIdentityError{"local profile private key file must be a safe relative file name"});
    }
    auto key_result =
        write_private_key(local_private_key_path(home, profile.private_key_file),
                          profile.key_pair.private_key);
    if (!key_result) {
      return Expected<void, LocalIdentityError>::failure(key_result.error());
    }
    root.push_back(profile_to_json(profile));
  }

  std::ofstream output(local_profiles_json_path(home), std::ios::trunc);
  if (!output) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write profiles.json"});
  }
  output << root.dump(2);
  if (!output) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write profiles.json bytes"});
  }
  return Expected<void, LocalIdentityError>::success();
}

Expected<LocalProfileStore, LocalIdentityError> load_or_create_local_profile_store(
    const std::filesystem::path& arvane_home,
    std::string display_name) {
  const std::filesystem::path home = normalize_home_path(arvane_home);
  std::error_code error;
  if (std::filesystem::exists(local_profiles_json_path(home), error)) {
    return read_profile_store(home);
  }
  error.clear();
  if (std::filesystem::exists(old_identity_json_path(home), error) &&
      std::filesystem::exists(old_private_key_path(home), error)) {
    return migrate_old_identity(home);
  }

  LocalProfileStore store;
  store.profiles.push_back(make_persistent_identity(std::move(display_name)));
  store.profiles.front().arvane_home = home;
  auto save_result = save_local_profile_store(home, store);
  if (!save_result) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(save_result.error());
  }
  return Expected<LocalProfileStore, LocalIdentityError>::success(std::move(store));
}

Expected<LocalPlayerIdentity, LocalIdentityError> load_or_create_local_identity(
    const std::filesystem::path& arvane_home,
    std::string display_name) {
  auto store = load_or_create_local_profile_store(arvane_home, std::move(display_name));
  if (!store) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(store.error());
  }
  const LocalPlayerIdentity* active = store.value().active_profile();
  if (active == nullptr) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"profiles.json did not contain any local profiles"});
  }
  return Expected<LocalPlayerIdentity, LocalIdentityError>::success(*active);
}

const LocalProfileSkin* best_local_skin_for_max_resolution(
    const LocalPlayerIdentity& profile,
    u16 max_resolution) noexcept {
  for (const LocalProfileSkin& skin : profile.skins) {
    if (skin.width <= max_resolution && skin.height <= max_resolution) {
      return &skin;
    }
  }
  return nullptr;
}

} // namespace arv::client
