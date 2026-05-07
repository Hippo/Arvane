#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/player/player_state.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server {

inline constexpr u32 PlayerProfileFormatVersion = 1;

struct PlayerProfileError {
  std::string message{};
};

enum class PlayerProfileOpenStatus {
  Created,
  Existing,
  PublicKeyMismatch,
  InvalidClaimedProfileID,
};

struct PlayerProfile {
  crypto::PlayerProfileID profile_id{};
  crypto::Ed25519PublicKey public_key{};
  std::string display_name{"local_player"};
  std::string last_world{"default"};
  Vec3f last_position{8.0F, 20.0F, -20.0F};
  f32 last_yaw_radians{};
  f32 last_pitch_radians{};
  std::string spawn_world{"default"};
  Vec3f spawn_position{8.0F, 20.0F, -20.0F};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
  std::string selected_skin_hash{};
  i32 permission_level{};
  player::Inventory inventory{player::DefaultPlayerInventorySlotCount};
  bool dirty{};
};

struct PlayerProfileOpenResult {
  PlayerProfileOpenStatus status{PlayerProfileOpenStatus::Created};
  PlayerProfile* profile{};
  crypto::PlayerProfileID profile_id{};
};

class PlayerProfileStore {
public:
  [[nodiscard]] PlayerProfileOpenResult open_or_create(
      const crypto::Ed25519PublicKey& public_key,
      std::string display_name,
      crypto::PlayerProfileID claimed_profile_id = {});

  [[nodiscard]] PlayerProfile* find(const crypto::PlayerProfileID& profile_id) noexcept;
  [[nodiscard]] const PlayerProfile* find(const crypto::PlayerProfileID& profile_id) const noexcept;
  [[nodiscard]] usize size() const noexcept;
  [[nodiscard]] std::vector<PlayerProfile> profiles() const;

  [[nodiscard]] bool insert_loaded_profile(PlayerProfile profile);

private:
  StableHashMap<std::string, PlayerProfile> profiles_;
};

[[nodiscard]] std::filesystem::path player_profile_file_path(
    const std::filesystem::path& server_save_root,
    const crypto::PlayerProfileID& profile_id);

[[nodiscard]] Expected<void, PlayerProfileError> save_player_profile(
    const std::filesystem::path& server_save_root,
    const PlayerProfile& profile);

[[nodiscard]] Expected<PlayerProfile, PlayerProfileError> load_player_profile(
    const std::filesystem::path& server_save_root,
    const crypto::PlayerProfileID& profile_id);

} // namespace arv::server
