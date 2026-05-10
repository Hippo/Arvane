#include "arv/client/local_identity.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("arvane_local_profiles_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

TEST(LocalProfileTests, SavesLoadsMultipleProfilesAndUsesSelectedProfile) {
  TempDir temp;
  arv::client::LocalProfileStore store;
  store.profiles.push_back(arv::client::make_ephemeral_local_identity("Hippo"));
  store.profiles.back().private_key_file = store.profiles.back().profile_id.string() + ".pk";
  store.profiles.back().skins.push_back({
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d",
      arv::protocol::SkinImageFormat::Png,
      64,
      64,
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d.png",
      true});
  store.profiles.push_back(arv::client::make_ephemeral_local_identity("Second"));
  store.profiles.back().private_key_file = store.profiles.back().profile_id.string() + ".pk";
  store.selected_profile = 1U;

  ASSERT_TRUE(arv::client::save_local_profile_store(temp.path, store));
  auto loaded = arv::client::load_or_create_local_profile_store(temp.path);

  ASSERT_TRUE(loaded);
  ASSERT_EQ(loaded.value().profiles.size(), 2U);
  EXPECT_EQ(loaded.value().selected_profile, 1U);
  ASSERT_NE(loaded.value().active_profile(), nullptr);
  EXPECT_EQ(loaded.value().active_profile()->display_name, "Second");
  EXPECT_EQ(loaded.value().profiles.front().display_name, "Hippo");
  EXPECT_EQ(loaded.value().profiles.front().skins.size(), 1U);
  EXPECT_TRUE(loaded.value().profiles.front().skins.front().slim_model);
  EXPECT_TRUE(std::filesystem::exists(arv::client::local_profiles_json_path(temp.path)));
  EXPECT_TRUE(std::filesystem::exists(arv::client::local_private_key_path(
      temp.path,
      loaded.value().profiles.front().private_key_file)));
}

TEST(LocalProfileTests, EmptyExistingProfilesFileCreatesFreshProfile) {
  TempDir temp;
  std::ofstream output(arv::client::local_profiles_json_path(temp.path), std::ios::trunc);
  output << R"({"selected_profile": 7, "profiles": []})";
  output.close();

  auto loaded = arv::client::load_or_create_local_profile_store(temp.path, "Fresh");

  ASSERT_TRUE(loaded);
  ASSERT_EQ(loaded.value().profiles.size(), 1U);
  EXPECT_EQ(loaded.value().selected_profile, 0U);
  EXPECT_EQ(loaded.value().profiles.front().display_name, "Fresh");
  EXPECT_TRUE(std::filesystem::exists(arv::client::local_private_key_path(
      temp.path,
      loaded.value().profiles.front().private_key_file)));
}

TEST(LocalProfileTests, OutOfBoundsSelectedProfileFallsBackToFirst) {
  TempDir temp;
  arv::client::LocalProfileStore store;
  store.profiles.push_back(arv::client::make_ephemeral_local_identity("Hippo"));
  store.profiles.back().private_key_file = store.profiles.back().profile_id.string() + ".pk";
  store.profiles.push_back(arv::client::make_ephemeral_local_identity("Second"));
  store.profiles.back().private_key_file = store.profiles.back().profile_id.string() + ".pk";
  store.selected_profile = 1U;

  ASSERT_TRUE(arv::client::save_local_profile_store(temp.path, store));
  const std::filesystem::path path = arv::client::local_profiles_json_path(temp.path);
  std::ifstream input(path);
  std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  const std::string selected_one = R"("selected_profile": 1)";
  const std::string selected_out_of_bounds = R"("selected_profile": 99)";
  const std::string::size_type selected_pos = text.find(selected_one);
  ASSERT_NE(selected_pos, std::string::npos);
  text.replace(selected_pos, selected_one.size(), selected_out_of_bounds);
  std::ofstream output(path, std::ios::trunc);
  output << text;
  output.close();

  auto loaded = arv::client::load_or_create_local_profile_store(temp.path);

  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded.value().selected_profile, 0U);
  ASSERT_NE(loaded.value().active_profile(), nullptr);
  EXPECT_EQ(loaded.value().active_profile()->display_name, "Hippo");
}

TEST(LocalProfileTests, SelectsFirstSkinWithinResolution) {
  arv::client::LocalPlayerIdentity profile;
  profile.skins.push_back({"0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d",
                           arv::protocol::SkinImageFormat::Png,
                           128,
                           128,
                           "large.png"});
  profile.skins.push_back({"0x23a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d",
                           arv::protocol::SkinImageFormat::Png,
                           64,
                           64,
                           "small.png"});

  const arv::client::LocalProfileSkin* selected =
      arv::client::best_local_skin_for_max_resolution(profile, 64);

  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(selected->width, 64U);
}

TEST(LocalProfileTests, DerivesSkinCachePaths) {
  const std::filesystem::path home = "/tmp/arvane";
  const std::string hash =
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d";

  EXPECT_EQ(
      arv::client::cached_skin_path(home, hash),
      home / "cache" / "skins" / (hash + ".png"));
}
