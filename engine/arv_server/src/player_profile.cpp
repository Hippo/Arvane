#include "arv/server/player_profile.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"
#include "arv/protocol/skins.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace arv::server {
namespace {

[[nodiscard]] bool is_integer(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::I64 || value.kind() == io::DataKind::U64;
}

[[nodiscard]] bool is_number(const io::DataValue& value) noexcept {
  return is_integer(value) || value.kind() == io::DataKind::F64;
}

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

[[nodiscard]] Expected<std::string, PlayerProfileError> required_string_member(
    const io::DataValue& object,
    std::string_view key) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return Expected<std::string, PlayerProfileError>::failure(
        PlayerProfileError{"player profile requires string field " + std::string{key}});
  }
  return Expected<std::string, PlayerProfileError>::success(
      std::string{value->as_string_or({})});
}

[[nodiscard]] Expected<u64, PlayerProfileError> required_u64_member(
    const io::DataValue& object,
    std::string_view key) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || !is_integer(*value)) {
    return Expected<u64, PlayerProfileError>::failure(
        PlayerProfileError{"player profile requires integer field " + std::string{key}});
  }
  return Expected<u64, PlayerProfileError>::success(value->as_u64_or(0U));
}

[[nodiscard]] io::DataValue vec3_to_value(Vec3f value) {
  io::DataValue array = io::DataValue::array();
  array.push(static_cast<f64>(value.x));
  array.push(static_cast<f64>(value.y));
  array.push(static_cast<f64>(value.z));
  return array;
}

[[nodiscard]] Vec3f vec3_from_value(const io::DataValue& node, Vec3f fallback) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() < 3U ||
      !is_number((*array)[0]) || !is_number((*array)[1]) || !is_number((*array)[2])) {
    return fallback;
  }
  return {
      static_cast<f32>((*array)[0].as_f64_or(fallback.x)),
      static_cast<f32>((*array)[1].as_f64_or(fallback.y)),
      static_cast<f32>((*array)[2].as_f64_or(fallback.z))};
}

[[nodiscard]] io::DataValue rotation_to_value(f32 yaw, f32 pitch) {
  io::DataValue array = io::DataValue::array();
  array.push(static_cast<f64>(yaw));
  array.push(static_cast<f64>(pitch));
  return array;
}

Expected<std::string, PlayerProfileError> encode_profile_json_text(
    const io::DataValue& node) {
  auto encoded = io::encode_json_value(node);
  if (!encoded) {
    return Expected<std::string, PlayerProfileError>::failure(
        PlayerProfileError{"failed to encode player profile JSON: " +
                           encoded.error().describe()});
  }
  std::string text = std::move(encoded.value());
  if (!text.empty() && text.back() == '\n') {
    text.pop_back();
  }
  return Expected<std::string, PlayerProfileError>::success(std::move(text));
}

Expected<io::DataDocument, PlayerProfileError> decode_profile_json_text(std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<io::DataDocument, PlayerProfileError>::failure(
        PlayerProfileError{"failed to parse player profile JSON: " +
                           decoded.error().describe()});
  }
  return Expected<io::DataDocument, PlayerProfileError>::success(std::move(decoded.value()));
}

} // namespace

PlayerProfileOpenResult PlayerProfileStore::open_or_create(
    const crypto::Ed25519PublicKey& public_key,
    std::string display_name,
    crypto::PlayerProfileID claimed_profile_id) {
  const crypto::PlayerProfileID derived_profile_id =
      crypto::derive_profile_id_from_public_key(public_key);
  if (!claimed_profile_id.empty() && claimed_profile_id != derived_profile_id) {
    return {PlayerProfileOpenStatus::InvalidClaimedProfileID, nullptr, derived_profile_id};
  }

  auto found = profiles_.find(derived_profile_id.string());
  if (found != profiles_.end()) {
    if (found->second.public_key != public_key) {
      return {PlayerProfileOpenStatus::PublicKeyMismatch, nullptr, derived_profile_id};
    }
    if (!display_name.empty()) {
      found->second.display_name = std::move(display_name);
      found->second.dirty = true;
    }
    return {PlayerProfileOpenStatus::Existing, &found->second, derived_profile_id};
  }

  PlayerProfile profile;
  profile.profile_id = derived_profile_id;
  profile.public_key = public_key;
  profile.display_name = display_name.empty() ? "local_player" : std::move(display_name);
  profile.dirty = true;

  auto [iter, inserted] = profiles_.emplace(profile.profile_id.string(), std::move(profile));
  (void)inserted;
  return {PlayerProfileOpenStatus::Created, &iter->second, iter->second.profile_id};
}

PlayerProfile* PlayerProfileStore::find(const crypto::PlayerProfileID& profile_id) noexcept {
  const auto found = profiles_.find(profile_id.string());
  return found == profiles_.end() ? nullptr : &found->second;
}

const PlayerProfile* PlayerProfileStore::find(const crypto::PlayerProfileID& profile_id) const noexcept {
  const auto found = profiles_.find(profile_id.string());
  return found == profiles_.end() ? nullptr : &found->second;
}

usize PlayerProfileStore::size() const noexcept {
  return profiles_.size();
}

std::vector<PlayerProfile> PlayerProfileStore::profiles() const {
  std::vector<PlayerProfile> out;
  out.reserve(profiles_.size());
  for (const auto& [profile_id, profile] : profiles_) {
    (void)profile_id;
    out.push_back(profile);
  }
  return out;
}

bool PlayerProfileStore::insert_loaded_profile(PlayerProfile profile) {
  if (profile.profile_id.empty()) {
    profile.profile_id = crypto::derive_profile_id_from_public_key(profile.public_key);
  }
  auto [iter, inserted] = profiles_.emplace(profile.profile_id.string(), std::move(profile));
  (void)iter;
  return inserted;
}

std::filesystem::path player_profile_file_path(
    const std::filesystem::path& server_save_root,
    const crypto::PlayerProfileID& profile_id) {
  return server_save_root / "players" / (profile_id.string() + ".json");
}

Expected<void, PlayerProfileError> save_player_profile(
    const std::filesystem::path& server_save_root,
    const PlayerProfile& profile) {
  if (profile.profile_id.empty()) {
    return Expected<void, PlayerProfileError>::failure(
      PlayerProfileError{"profile id must be 0x-prefixed lowercase hex"});
  }

  std::error_code error;
  const std::filesystem::path path = player_profile_file_path(server_save_root, profile.profile_id);
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, PlayerProfileError>::failure(
        PlayerProfileError{"failed to create profile directory: " + error.message()});
  }

  io::DataValue node = io::DataValue::object();
  node.set("version", static_cast<u64>(PlayerProfileFormatVersion));
  node.set("profile_id", profile.profile_id.string());
  node.set("public_key", crypto::to_prefixed_lower_hex(profile.public_key.bytes));
  node.set("display_name", profile.display_name);
  node.set("last_world", profile.last_world);
  node.set("last_position", vec3_to_value(profile.last_position));
  node.set("last_rotation", rotation_to_value(profile.last_yaw_radians, profile.last_pitch_radians));
  node.set("spawn_world", profile.spawn_world);
  node.set("spawn_position", vec3_to_value(profile.spawn_position));
  node.set("game_mode", profile.game_mode_id);
  node.set("selected_skin_hash", profile.selected_skin_hash);
  node.set("selected_skin_slim_model", profile.selected_skin_slim_model);
  node.set("permission_level", static_cast<i64>(profile.permission_level));
  node.set("inventory", io::DataValue::object());
  node.set("stats", io::DataValue::object());
  node.set("settings", io::DataValue::object());

  auto encoded = encode_profile_json_text(node);
  if (!encoded) {
    return Expected<void, PlayerProfileError>::failure(encoded.error());
  }

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return Expected<void, PlayerProfileError>::failure(
        PlayerProfileError{"failed to open profile file for write"});
  }
  output << encoded.value();
  if (!output) {
    return Expected<void, PlayerProfileError>::failure(
        PlayerProfileError{"failed to write profile file"});
  }
  return Expected<void, PlayerProfileError>::success();
}

Expected<PlayerProfile, PlayerProfileError> load_player_profile(
    const std::filesystem::path& server_save_root,
    const crypto::PlayerProfileID& profile_id) {
  if (profile_id.empty()) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"profile id must be 0x-prefixed lowercase hex"});
  }

  const std::filesystem::path path = player_profile_file_path(server_save_root, profile_id);
  auto text = io::read_text_file_contents(path);
  if (!text) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"failed to open player profile file: " + path.string()});
  }

  auto decoded = decode_profile_json_text(text.value());
  if (!decoded) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(decoded.error());
  }
  const io::DataValue& node = decoded.value().root;
  if (node.kind() != io::DataKind::Object) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"player profile root must be an object"});
  }
  auto version = required_u64_member(node, "version");
  if (!version) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(version.error());
  }
  if (version.value() != PlayerProfileFormatVersion) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"unsupported player profile version"});
  }

  auto public_key_text = required_string_member(node, "public_key");
  if (!public_key_text) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(public_key_text.error());
  }
  auto public_key = crypto::parse_prefixed_ed25519_public_key(public_key_text.value());
  if (!public_key) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{public_key.error().message});
  }

  PlayerProfile profile;
  auto profile_id_text = required_string_member(node, "profile_id");
  if (!profile_id_text) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(profile_id_text.error());
  }
  auto parsed_profile_id = crypto::parse_player_profile_id(profile_id_text.value());
  if (!parsed_profile_id) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{parsed_profile_id.error().message});
  }
  profile.profile_id = parsed_profile_id.value();
  profile.public_key = public_key.value();
  profile.display_name = string_member_or(node, "display_name", "local_player");
  profile.last_world = string_member_or(node, "last_world", "default");
  if (const io::DataValue* value = member(node, "last_position"); value != nullptr) {
    profile.last_position = vec3_from_value(*value, profile.last_position);
  }
  if (const io::DataValue* value = member(node, "last_rotation"); value != nullptr) {
    const io::DataValue::Array* rotation = value->array_if();
    if (rotation != nullptr && rotation->size() >= 2U &&
        is_number((*rotation)[0]) && is_number((*rotation)[1])) {
      profile.last_yaw_radians = static_cast<f32>((*rotation)[0].as_f64_or(0.0));
      profile.last_pitch_radians = static_cast<f32>((*rotation)[1].as_f64_or(0.0));
    }
  }
  profile.spawn_world = string_member_or(node, "spawn_world", "default");
  if (const io::DataValue* value = member(node, "spawn_position"); value != nullptr) {
    profile.spawn_position = vec3_from_value(*value, profile.spawn_position);
  }
  profile.game_mode_id =
      string_member_or(node, "game_mode", std::string{player::SurvivalGameModeID});
  profile.selected_skin_hash = string_member_or(node, "selected_skin_hash", std::string{});
  if (!profile.selected_skin_hash.empty() &&
      !protocol::is_valid_skin_hash(profile.selected_skin_hash)) {
    profile.selected_skin_hash.clear();
  }
  profile.selected_skin_slim_model = false;
  if (!profile.selected_skin_hash.empty()) {
    if (const io::DataValue* value = member(node, "selected_skin_slim_model");
        value != nullptr && value->kind() == io::DataKind::Bool) {
      profile.selected_skin_slim_model = value->as_bool_or(false);
    }
  }
  if (const io::DataValue* value = member(node, "permission_level");
      value != nullptr && is_integer(*value)) {
    profile.permission_level = static_cast<i32>(value->as_i64_or(0));
  }

  const crypto::PlayerProfileID derived_profile_id =
      crypto::derive_profile_id_from_public_key(profile.public_key);
  if (profile.profile_id != derived_profile_id || profile.profile_id != profile_id) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"profile id does not match public key"});
  }

  return Expected<PlayerProfile, PlayerProfileError>::success(std::move(profile));
}

} // namespace arv::server
