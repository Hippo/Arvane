#include "arv/server/skin_runtime.hpp"

#include <gtest/gtest.h>

#include <filesystem>

TEST(ServerSkinRuntimeTests, ChoosesUploadModeFromResolutionAndBytes) {
  arv::io::SkinConfig config;

  EXPECT_EQ(
      arv::server::choose_skin_upload_mode(config, 64, 64, 1024),
      arv::protocol::SkinUploadMode::Full);
  EXPECT_EQ(
      arv::server::choose_skin_upload_mode(config, 512, 512, 1024),
      arv::protocol::SkinUploadMode::Chunked);
  EXPECT_EQ(
      arv::server::choose_skin_upload_mode(config, 64, 64, config.uploads.chunk_threshold_bytes),
      arv::protocol::SkinUploadMode::Chunked);
}

TEST(ServerSkinRuntimeTests, ValidatesUploadBeginDenials) {
  arv::io::SkinConfig config;
  arv::player::ResolvedPermissions permissions;
  arv::protocol::ServerboundSkinUploadBegin begin;
  begin.format = arv::protocol::SkinImageFormat::Png;
  begin.width = 64;
  begin.height = 64;
  begin.total_upload_bytes = 1024;

  auto accepted = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      begin);
  ASSERT_TRUE(accepted);
  EXPECT_EQ(accepted.value().upload_mode, arv::protocol::SkinUploadMode::Full);

  config.enabled = false;
  auto disabled = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      begin);
  ASSERT_FALSE(disabled);
  EXPECT_EQ(disabled.error(), arv::protocol::SkinUploadDenyReason::SkinsDisabled);
}

TEST(ServerSkinRuntimeTests, RejectsInvalidResolutionAndOversizeUpload) {
  arv::io::SkinConfig config;
  arv::player::ResolvedPermissions permissions;
  arv::protocol::ServerboundSkinUploadBegin begin;
  begin.format = arv::protocol::SkinImageFormat::Png;
  begin.width = 96;
  begin.height = 96;
  begin.total_upload_bytes = 1024;

  auto invalid = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      begin);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error(), arv::protocol::SkinUploadDenyReason::InvalidResolution);

  begin.width = 64;
  begin.height = 64;
  begin.total_upload_bytes = arv::io::default_skin_max_upload_bytes(64) + 1U;
  auto too_large = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      begin);
  ASSERT_FALSE(too_large);
  EXPECT_EQ(too_large.error(), arv::protocol::SkinUploadDenyReason::UploadTooLarge);
}

TEST(ServerSkinRuntimeTests, DerivesServerSkinStoragePath) {
  const std::string hash =
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d";

  EXPECT_EQ(
      arv::server::server_skin_storage_path("/tmp/save", hash),
      std::filesystem::path("/tmp/save/player_assets/skins") / (hash + ".png"));
}
