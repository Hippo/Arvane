#include "arv/server/player_profile.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"
#include "arv/protocol/skins.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::server {
namespace {

using json = nlohmann::json;

[[nodiscard]] json vec3_to_json(Vec3f value) {
  return json::array({value.x, value.y, value.z});
}

[[nodiscard]] Vec3f json_to_vec3(const json& node) {
  return {
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()};
}

[[nodiscard]] Expected<crypto::Ed25519PublicKey, PlayerProfileError> public_key_from_hex(
    std::string_view text) {
  auto bytes = crypto::from_prefixed_lower_hex(text);
  if (!bytes) {
    return Expected<crypto::Ed25519PublicKey, PlayerProfileError>::failure(
        PlayerProfileError{bytes.error().message});
  }
  if (bytes.value().size() != crypto::ed25519_public_key_size) {
    return Expected<crypto::Ed25519PublicKey, PlayerProfileError>::failure(
        PlayerProfileError{"profile public key has the wrong size"});
  }

  crypto::Ed25519PublicKey public_key;
  std::copy(bytes.value().begin(), bytes.value().end(), public_key.bytes.begin());
  return Expected<crypto::Ed25519PublicKey, PlayerProfileError>::success(public_key);
}

[[nodiscard]] Expected<std::string, PlayerProfileError> read_text_file(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Expected<std::string, PlayerProfileError>::failure(
        PlayerProfileError{"failed to open player profile file: " + path.string()});
  }
  return Expected<std::string, PlayerProfileError>::success(
      std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}});
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

  const json node{
      {"version", PlayerProfileFormatVersion},
      {"profile_id", profile.profile_id.string()},
      {"public_key", crypto::to_prefixed_lower_hex(profile.public_key.bytes)},
      {"display_name", profile.display_name},
      {"last_world", profile.last_world},
      {"last_position", vec3_to_json(profile.last_position)},
      {"last_rotation", json::array({profile.last_yaw_radians, profile.last_pitch_radians})},
      {"spawn_world", profile.spawn_world},
      {"spawn_position", vec3_to_json(profile.spawn_position)},
      {"game_mode", profile.game_mode_id},
      {"selected_skin_hash", profile.selected_skin_hash},
      {"permission_level", profile.permission_level},
      {"inventory", json::object()},
      {"stats", json::object()},
      {"settings", json::object()},
  };

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return Expected<void, PlayerProfileError>::failure(
        PlayerProfileError{"failed to open profile file for write"});
  }
  output << node.dump(2);
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

  auto text = read_text_file(player_profile_file_path(server_save_root, profile_id));
  if (!text) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(text.error());
  }

  try {
    const json node = json::parse(text.value());
    if (node.at("version").get<u32>() != PlayerProfileFormatVersion) {
      return Expected<PlayerProfile, PlayerProfileError>::failure(
          PlayerProfileError{"unsupported player profile version"});
    }

    auto public_key = public_key_from_hex(node.at("public_key").get<std::string>());
    if (!public_key) {
      return Expected<PlayerProfile, PlayerProfileError>::failure(public_key.error());
    }

    PlayerProfile profile;
    auto parsed_profile_id =
        crypto::parse_player_profile_id(node.at("profile_id").get<std::string>());
    if (!parsed_profile_id) {
      return Expected<PlayerProfile, PlayerProfileError>::failure(
          PlayerProfileError{parsed_profile_id.error().message});
    }
    profile.profile_id = parsed_profile_id.value();
    profile.public_key = public_key.value();
    profile.display_name = node.value("display_name", std::string{"local_player"});
    profile.last_world = node.value("last_world", std::string{"default"});
    if (const auto value = node.find("last_position"); value != node.end()) {
      profile.last_position = json_to_vec3(*value);
    }
    if (const auto value = node.find("last_rotation");
        value != node.end() && value->is_array() && value->size() >= 2U) {
      profile.last_yaw_radians = value->at(0).get<f32>();
      profile.last_pitch_radians = value->at(1).get<f32>();
    }
    profile.spawn_world = node.value("spawn_world", std::string{"default"});
    if (const auto value = node.find("spawn_position"); value != node.end()) {
      profile.spawn_position = json_to_vec3(*value);
    }
    profile.game_mode_id = node.value("game_mode", std::string{player::SurvivalGameModeID});
    profile.selected_skin_hash = node.value("selected_skin_hash", std::string{});
    if (!profile.selected_skin_hash.empty() &&
        !protocol::is_valid_skin_hash(profile.selected_skin_hash)) {
      profile.selected_skin_hash.clear();
    }
    profile.permission_level = node.value("permission_level", 0);

    const crypto::PlayerProfileID derived_profile_id =
        crypto::derive_profile_id_from_public_key(profile.public_key);
    if (profile.profile_id != derived_profile_id || profile.profile_id != profile_id) {
      return Expected<PlayerProfile, PlayerProfileError>::failure(
          PlayerProfileError{"profile id does not match public key"});
    }

    return Expected<PlayerProfile, PlayerProfileError>::success(std::move(profile));
  } catch (const json::exception& ex) {
    return Expected<PlayerProfile, PlayerProfileError>::failure(
        PlayerProfileError{"failed to parse player profile JSON: " + std::string{ex.what()}});
  }
}

} // namespace arv::server
