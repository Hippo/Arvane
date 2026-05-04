#include "arv/server/player_profile.hpp"

#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace {

struct TempDir {
  std::filesystem::path path{};

  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("arvane_profile_test_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

TEST(ServerPlayerProfileTests, FirstJoinCreatesProfileFromPublicKey) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfileStore store;

  const arv::server::PlayerProfileOpenResult result =
      store.open_or_create(key_pair.public_key, "Hippo");

  ASSERT_EQ(result.status, arv::server::PlayerProfileOpenStatus::Created);
  ASSERT_NE(result.profile, nullptr);
  EXPECT_EQ(result.profile->profile_id,
            arv::crypto::derive_profile_id_from_public_key(key_pair.public_key));
  EXPECT_EQ(result.profile->display_name, "Hippo");
  EXPECT_EQ(store.size(), 1U);
}

TEST(ServerPlayerProfileTests, ExistingProfileLoginAllowsDisplayNameChangeWithoutChangingId) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfileStore store;

  const arv::server::PlayerProfileOpenResult first =
      store.open_or_create(key_pair.public_key, "Hippo");
  const arv::crypto::PlayerProfileID profile_id = first.profile_id;
  const arv::server::PlayerProfileOpenResult second =
      store.open_or_create(key_pair.public_key, "New Hippo", profile_id);

  ASSERT_EQ(second.status, arv::server::PlayerProfileOpenStatus::Existing);
  ASSERT_NE(second.profile, nullptr);
  EXPECT_EQ(second.profile->profile_id, profile_id);
  EXPECT_EQ(second.profile->display_name, "New Hippo");
  EXPECT_EQ(store.size(), 1U);
}

TEST(ServerPlayerProfileTests, DuplicateDisplayNamesRemainDistinctProfiles) {
  const arv::crypto::Ed25519KeyPair first_key = arv::crypto::generate_ed25519_keypair();
  const arv::crypto::Ed25519KeyPair second_key = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfileStore store;

  const arv::server::PlayerProfileOpenResult first =
      store.open_or_create(first_key.public_key, "Hippo");
  const arv::server::PlayerProfileOpenResult second =
      store.open_or_create(second_key.public_key, "Hippo");

  ASSERT_NE(first.profile, nullptr);
  ASSERT_NE(second.profile, nullptr);
  EXPECT_EQ(first.profile->display_name, second.profile->display_name);
  EXPECT_NE(first.profile->profile_id, second.profile->profile_id);
  EXPECT_EQ(store.size(), 2U);
}

TEST(ServerPlayerProfileTests, RejectsClaimedProfileIdThatDoesNotMatchPublicKey) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  const arv::crypto::Ed25519KeyPair other_key_pair = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfileStore store;
  const arv::crypto::PlayerProfileID wrong_profile_id =
      arv::crypto::derive_profile_id_from_public_key(other_key_pair.public_key);

  const arv::server::PlayerProfileOpenResult result =
      store.open_or_create(key_pair.public_key, "Hippo", wrong_profile_id);

  EXPECT_EQ(result.status, arv::server::PlayerProfileOpenStatus::InvalidClaimedProfileID);
  EXPECT_EQ(result.profile, nullptr);
  EXPECT_EQ(store.size(), 0U);
}

TEST(ServerPlayerProfileTests, RejectsStoredProfileWithMismatchedPublicKey) {
  const arv::crypto::Ed25519KeyPair expected_key = arv::crypto::generate_ed25519_keypair();
  const arv::crypto::Ed25519KeyPair mismatched_key = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfile corrupted;
  corrupted.profile_id = arv::crypto::derive_profile_id_from_public_key(expected_key.public_key);
  corrupted.public_key = mismatched_key.public_key;

  arv::server::PlayerProfileStore store;
  ASSERT_TRUE(store.insert_loaded_profile(std::move(corrupted)));

  const arv::server::PlayerProfileOpenResult result =
      store.open_or_create(expected_key.public_key, "Hippo");

  EXPECT_EQ(result.status, arv::server::PlayerProfileOpenStatus::PublicKeyMismatch);
  EXPECT_EQ(result.profile, nullptr);
}

TEST(ServerPlayerProfileTests, ProfileSaveLoadRoundTripsWithoutPrivateKey) {
  TempDir temp_dir;
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  arv::server::PlayerProfile profile;
  profile.profile_id = arv::crypto::derive_profile_id_from_public_key(key_pair.public_key);
  profile.public_key = key_pair.public_key;
  profile.display_name = "Hippo";
  profile.last_world = "arvane:overworld";
  profile.last_position = {12.5F, 70.0F, -44.5F};
  profile.last_yaw_radians = 1.0F;
  profile.last_pitch_radians = 0.5F;
  profile.spawn_world = "arvane:overworld";
  profile.spawn_position = {0.5F, 70.0F, 0.5F};
  profile.game_mode_id = std::string{arv::player::CreativeGameModeID};
  profile.permission_level = 2;

  ASSERT_TRUE(arv::server::save_player_profile(temp_dir.path, profile));
  EXPECT_TRUE(std::filesystem::exists(
      arv::server::player_profile_file_path(temp_dir.path, profile.profile_id)));

  auto loaded = arv::server::load_player_profile(temp_dir.path, profile.profile_id);

  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded.value().profile_id, profile.profile_id);
  EXPECT_EQ(loaded.value().public_key, profile.public_key);
  EXPECT_EQ(loaded.value().display_name, "Hippo");
  EXPECT_EQ(loaded.value().last_world, "arvane:overworld");
  EXPECT_EQ(loaded.value().last_position, profile.last_position);
  EXPECT_EQ(loaded.value().game_mode_id, arv::player::CreativeGameModeID);
  EXPECT_EQ(loaded.value().permission_level, 2);
}
