#include "arv/client/local_identity.hpp"

#include "arv/asset/resource.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace arv::client {
namespace {

inline constexpr u32 LocalProfileVersion = 1;
inline constexpr std::string_view ProfilesFileName = "profiles.json";
inline constexpr std::string_view KeysDirectoryName = "keys";
inline constexpr std::string_view SkinsDirectoryName = "skins";
inline constexpr std::string_view CacheDirectoryName = "cache";

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view key) noexcept {
  return object.get(key);
}

[[nodiscard]] std::string string_member_or(
    const io::DataValue& object,
    std::string_view key,
    std::string fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::String
             ? std::string{value->as_string_or({})}
             : std::move(fallback);
}

[[nodiscard]] Expected<std::string, LocalIdentityError> required_string_member(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return Expected<std::string, LocalIdentityError>::failure(
        LocalIdentityError{std::string{context} + " requires string field " + std::string{key}});
  }
  return Expected<std::string, LocalIdentityError>::success(
      std::string{value->as_string_or({})});
}

[[nodiscard]] Expected<u64, LocalIdentityError> required_u64_member(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || !io::is_integer_value(*value)) {
    return Expected<u64, LocalIdentityError>::failure(
        LocalIdentityError{std::string{context} + " requires integer field " + std::string{key}});
  }
  return Expected<u64, LocalIdentityError>::success(value->as_u64_or(0U));
}

[[nodiscard]] Expected<std::string, LocalIdentityError> encode_profiles_json(
    const io::DataValue& value) {
  auto encoded = io::encode_json_value(value);
  if (!encoded) {
    return Expected<std::string, LocalIdentityError>::failure(
        LocalIdentityError{"failed to encode profiles.json: " + encoded.error().describe()});
  }
  std::string text = std::move(encoded.value());
  if (!text.empty() && text.back() == '\n') {
    text.pop_back();
  }
  return Expected<std::string, LocalIdentityError>::success(std::move(text));
}

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

[[nodiscard]] Expected<LocalProfileSkin, LocalIdentityError> skin_from_value(
    const io::DataValue& node) {
  if (node.kind() != io::DataKind::Object) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin entry must be an object"});
  }

  LocalProfileSkin skin;
  auto skin_hash = required_string_member(node, "skin_hash", "profile skin entry");
  if (!skin_hash) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(skin_hash.error());
  }
  skin.skin_hash = std::move(skin_hash.value());
  if (!protocol::is_valid_skin_hash(skin.skin_hash)) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin hash must be lowercase 0x-prefixed 256-bit hex"});
  }
  const std::string format = string_member_or(node, "format", "png");
  if (format != "png") {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"unsupported local profile skin format: " + format});
  }
  skin.format = protocol::SkinImageFormat::Png;
  auto width = required_u64_member(node, "width", "profile skin entry");
  auto height = required_u64_member(node, "height", "profile skin entry");
  auto file = required_string_member(node, "file", "profile skin entry");
  if (!width) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(width.error());
  }
  if (!height) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(height.error());
  }
  if (!file) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(file.error());
  }
  if (width.value() > std::numeric_limits<u16>::max() ||
      height.value() > std::numeric_limits<u16>::max()) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin dimensions are too large"});
  }
  skin.width = static_cast<u16>(width.value());
  skin.height = static_cast<u16>(height.value());
  skin.file = std::move(file.value());
  if (const io::DataValue* slim_model = member(node, "slim_model");
      slim_model != nullptr && slim_model->kind() == io::DataKind::Bool) {
    skin.slim_model = slim_model->as_bool_or(false);
  }
  if (!safe_relative_file_name(skin.file)) {
    return Expected<LocalProfileSkin, LocalIdentityError>::failure(
        LocalIdentityError{"profile skin file must be a safe relative file name"});
  }
  return Expected<LocalProfileSkin, LocalIdentityError>::success(std::move(skin));
}

[[nodiscard]] io::DataValue skin_to_value(const LocalProfileSkin& skin) {
  io::DataValue node = io::DataValue::object();
  node.set("skin_hash", skin.skin_hash);
  node.set("format", "png");
  node.set("width", static_cast<u64>(skin.width));
  node.set("height", static_cast<u64>(skin.height));
  node.set("file", skin.file);
  node.set("slim_model", skin.slim_model);
  return node;
}

[[nodiscard]] Expected<LocalPlayerIdentity, LocalIdentityError> profile_from_value(
    const std::filesystem::path& home,
    const io::DataValue& node) {
  if (node.kind() != io::DataKind::Object) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile entry must be an object"});
  }
  auto version = required_u64_member(node, "version", "local profile entry");
  if (!version) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(version.error());
  }
  if (version.value() != LocalProfileVersion) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"unsupported local profile version"});
  }

  LocalPlayerIdentity profile;
  profile.arvane_home = home;
  profile.display_name = string_member_or(node, "display_name", "local_player");
  auto private_key_file =
      required_string_member(node, "private_key_file", "local profile entry");
  if (!private_key_file) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        private_key_file.error());
  }
  profile.private_key_file = std::move(private_key_file.value());
  if (!safe_relative_file_name(profile.private_key_file)) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile private key file must be a safe relative file name"});
  }

  auto public_key_text = required_string_member(node, "public_key", "local profile entry");
  auto profile_id_text = required_string_member(node, "profile_id", "local profile entry");
  if (!public_key_text) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(public_key_text.error());
  }
  if (!profile_id_text) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(profile_id_text.error());
  }
  auto public_key = crypto::parse_prefixed_ed25519_public_key(public_key_text.value());
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
  auto saved_profile_id = crypto::parse_player_profile_id(profile_id_text.value());
  if (!saved_profile_id) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{saved_profile_id.error().message});
  }
  if (saved_profile_id.value() != profile.profile_id) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"local profile id does not match public key"});
  }

  if (const io::DataValue* skins = member(node, "skins"); skins != nullptr) {
    const io::DataValue::Array* skin_array = skins->array_if();
    if (skin_array == nullptr) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
          LocalIdentityError{"local profile skins must be an array"});
    }
    profile.skins.reserve(skin_array->size());
    for (const io::DataValue& skin_node : *skin_array) {
      auto skin = skin_from_value(skin_node);
      if (!skin) {
        return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(skin.error());
      }
      profile.skins.push_back(std::move(skin.value()));
    }
  }
  return Expected<LocalPlayerIdentity, LocalIdentityError>::success(std::move(profile));
}

[[nodiscard]] io::DataValue profile_to_value(const LocalPlayerIdentity& profile) {
  io::DataValue skins = io::DataValue::array();
  for (const LocalProfileSkin& skin : profile.skins) {
    skins.push(skin_to_value(skin));
  }
  io::DataValue node = io::DataValue::object();
  node.set("version", static_cast<u64>(LocalProfileVersion));
  node.set("display_name", profile.display_name);
  node.set("private_key_file", profile.private_key_file);
  node.set("profile_id", profile.profile_id.string());
  node.set("public_key", crypto::to_prefixed_lower_hex(profile.key_pair.public_key.bytes));
  node.set("skins", std::move(skins));
  return node;
}

[[nodiscard]] Expected<LocalProfileStore, LocalIdentityError> read_profile_store(
    const std::filesystem::path& home) {
  const std::filesystem::path profiles_path = local_profiles_json_path(home);
  auto text = io::read_text_file_contents(profiles_path);
  if (!text) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local profile file: " + profiles_path.string()});
  }

  auto decoded = io::decode_json_value(text.value());
  if (!decoded) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to parse profiles.json: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  if (root.kind() != io::DataKind::Object) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"profiles.json must contain an object"});
  }
  const io::DataValue* profiles_node = member(root, "profiles");
  const io::DataValue::Array* profiles_array =
      profiles_node == nullptr ? nullptr : profiles_node->array_if();
  if (profiles_array == nullptr) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"profiles.json profiles must contain an array"});
  }

  LocalProfileStore store;
  if (const io::DataValue* selected_profile = member(root, "selected_profile");
      selected_profile != nullptr) {
    if (!io::is_integer_value(*selected_profile)) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(
          LocalIdentityError{"profiles.json selected_profile must be an unsigned integer"});
    }
    store.selected_profile = static_cast<usize>(selected_profile->as_u64_or(0U));
  }
  store.profiles.reserve(profiles_array->size());
  for (const io::DataValue& profile_node : *profiles_array) {
    auto profile = profile_from_value(home, profile_node);
    if (!profile) {
      return Expected<LocalProfileStore, LocalIdentityError>::failure(profile.error());
    }
    store.profiles.push_back(std::move(profile.value()));
  }
  if (store.selected_profile >= store.profiles.size()) {
    store.selected_profile = 0U;
  }
  return Expected<LocalProfileStore, LocalIdentityError>::success(std::move(store));
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

  io::DataValue profiles = io::DataValue::array();
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
    profiles.push(profile_to_value(profile));
  }
  io::DataValue root = io::DataValue::object();
  root.set(
      "selected_profile",
      static_cast<u64>(store.profiles.empty() ? 0U : store.active_profile_index()));
  root.set("profiles", std::move(profiles));

  auto encoded = encode_profiles_json(root);
  if (!encoded) {
    return Expected<void, LocalIdentityError>::failure(encoded.error());
  }

  std::ofstream output(local_profiles_json_path(home), std::ios::trunc);
  if (!output) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write profiles.json"});
  }
  output << encoded.value();
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

Expected<LocalProfileStore, LocalIdentityError> load_local_profile_store(
    const std::filesystem::path& arvane_home) {
  const std::filesystem::path& home = arvane_home;
  std::error_code error;
  const bool profiles_file_exists =
      std::filesystem::exists(local_profiles_json_path(home), error);
  if (error) {
    return Expected<LocalProfileStore, LocalIdentityError>::failure(
        LocalIdentityError{"failed to inspect local profile file: " + error.message()});
  }
  if (!profiles_file_exists) {
    return Expected<LocalProfileStore, LocalIdentityError>::success(LocalProfileStore{});
  }
  return read_profile_store(home);
}

Expected<LocalProfileStore, LocalIdentityError> load_or_create_local_profile_store(
    const std::filesystem::path& arvane_home,
    std::string display_name) {
  const std::filesystem::path& home = arvane_home;
  auto store = load_local_profile_store(home);
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
