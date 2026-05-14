#include "arv/client/local_identity.hpp"

#include "arv/asset/resource.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"
#include "arv/io/files.hpp"

#include <fstream>
#include <limits>
#include <span>
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

[[nodiscard]] std::filesystem::path keys_root(const std::filesystem::path& home) {
  return home / std::string{KeysDirectoryName};
}

[[nodiscard]] std::filesystem::path skins_root(const std::filesystem::path& home) {
  return home / std::string{SkinsDirectoryName};
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

[[nodiscard]] Expected<void, LocalIdentityError> write_binary_file_atomically(
    const std::filesystem::path& path,
    std::span<const u8> bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create directory: " + error.message()});
  }

  const std::filesystem::path temp_path = path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Expected<void, LocalIdentityError>::failure(
          LocalIdentityError{"failed to write temporary skin file"});
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      return Expected<void, LocalIdentityError>::failure(
          LocalIdentityError{"failed to write skin PNG bytes"});
    }
  }

  std::filesystem::rename(temp_path, path, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to save skin PNG: " + error.message()});
  }
  return Expected<void, LocalIdentityError>::success();
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
  skin.slim_model = node.value("slim_model", false);
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
      {"slim_model", skin.slim_model},
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

  auto public_key =
      crypto::parse_prefixed_ed25519_public_key(node.at("public_key").get<std::string>());
  auto private_key = read_private_key(local_private_key_path(home, profile.private_key_file));
  if (!public_key) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{public_key.error().message});
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
  const std::filesystem::path profiles_path = local_profiles_json_path(home);
  auto text = io::read_text_file_contents(profiles_path);
  if (!text) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local profile file: " + profiles_path.string()});
  }

  try {
    const json root = json::parse(text.value());
    if (!root.is_object()) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{"profiles.json must contain an object"});
    }
    const auto profiles_node = root.find("profiles");
    if (profiles_node == root.end() || !profiles_node->is_array()) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{"profiles.json profiles must contain an array"});
    }

    LocalProfileStore store;
    const auto selected_profile = root.find("selected_profile");
    if (selected_profile != root.end()) {
      if (!selected_profile->is_number_unsigned()) {
        return Expected<LocalProfileStore, LocalIdentityError>::failure(
            LocalIdentityError{"profiles.json selected_profile must be an unsigned integer"});
      }
      store.selected_profile = selected_profile->get<usize>();
    }
    store.profiles.reserve(profiles_node->size());
    for (const json& profile_node : *profiles_node) {
      auto profile = profile_from_json(home, profile_node);
      if (!profile) {
        return Expected<LocalProfileStore, LocalIdentityError>::failure(profile.error());
      }
      store.profiles.push_back(std::move(profile.value()));
    }
    if (store.selected_profile >= store.profiles.size()) {
      store.selected_profile = 0U;
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

} // namespace

usize LocalProfileStore::active_profile_index() const noexcept {
  return selected_profile < profiles.size() ? selected_profile : 0U;
}

const LocalPlayerIdentity* LocalProfileStore::active_profile() const noexcept {
  return profiles.empty() ? nullptr : &profiles[active_profile_index()];
}

std::filesystem::path local_profiles_json_path(const std::filesystem::path& arvane_home) {
  return arvane_home / std::string{ProfilesFileName};
}

std::filesystem::path local_private_key_path(
    const std::filesystem::path& arvane_home,
    std::string_view private_key_file) {
  return keys_root(arvane_home) / std::string{private_key_file};
}

std::filesystem::path local_skin_path(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin) {
  return skins_root(arvane_home) / skin.file;
}

std::filesystem::path local_skins_directory_path(const std::filesystem::path& arvane_home) {
  return skins_root(arvane_home);
}

std::filesystem::path cached_skin_path(
    const std::filesystem::path& arvane_home,
    std::string_view skin_hash) {
  return arvane_home / std::string{CacheDirectoryName} /
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
  const std::filesystem::path& home = arvane_home;
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
  std::filesystem::create_directories(skins_root(home), error);
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

  json profiles = json::array();
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
    profiles.push_back(profile_to_json(profile));
  }
  json root{
      {"selected_profile", store.profiles.empty() ? 0U : store.active_profile_index()},
      {"profiles", std::move(profiles)},
  };

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

Expected<LocalProfileSkin, LocalIdentityError> import_local_profile_skin_png(
    const std::filesystem::path& arvane_home,
    const std::filesystem::path& source_png) {
  auto bytes = io::read_binary_file(source_png);
  if (!bytes) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open PNG file: " + source_png.string()});
  }
  if (bytes.value().empty() || bytes.value().size() > protocol::MaxSkinImagePayloadBytes) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"skin PNG is empty or exceeds the maximum skin size"});
  }

  auto decoded = asset::decode_png_image_bytes(
      bytes.value(),
      {"arvane:texture/player_skin/import"});
  if (!decoded) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"failed to decode skin PNG: " + decoded.error().message});
  }
  if (decoded.value().width > std::numeric_limits<u16>::max() ||
      decoded.value().height > std::numeric_limits<u16>::max()) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"skin PNG dimensions are too large"});
  }

  LocalProfileSkin skin;
  skin.skin_hash = protocol::skin_hash_for_bytes(bytes.value());
  skin.format = protocol::SkinImageFormat::Png;
  skin.width = static_cast<u16>(decoded.value().width);
  skin.height = static_cast<u16>(decoded.value().height);
  skin.file = skin.skin_hash + ".png";

  const std::filesystem::path destination =
      skins_root(arvane_home) / skin.file;
  std::error_code error;
  if (!std::filesystem::exists(destination, error)) {
    auto write_result = write_binary_file_atomically(destination, bytes.value());
    if (!write_result) {
      return Expected<LocalProfileSkin, LocalIdentityError>::failure(write_result.error());
    }
  }
  return Expected<LocalProfileSkin, LocalIdentityError>::success(std::move(skin));
}

Expected<LocalProfileStore, LocalIdentityError> load_or_create_local_profile_store(
    const std::filesystem::path& arvane_home,
    std::string display_name) {
  const std::filesystem::path& home = arvane_home;
  std::error_code error;
  if (std::filesystem::exists(local_profiles_json_path(home), error)) {
    auto store = read_profile_store(home);
    if (!store || !store.value().profiles.empty()) {
      return store;
    }
    store.value().profiles.push_back(make_persistent_identity(std::move(display_name)));
    store.value().profiles.front().arvane_home = home;
    store.value().selected_profile = 0U;
    auto save_result = save_local_profile_store(home, store.value());
    if (!save_result) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(save_result.error());
    }
    return store;
  }

  LocalProfileStore store;
  store.profiles.push_back(make_persistent_identity(std::move(display_name)));
  store.profiles.front().arvane_home = home;
  store.selected_profile = 0U;
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
