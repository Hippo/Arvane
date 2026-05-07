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
      false,
      begin);
  ASSERT_FALSE(disabled);
  EXPECT_EQ(disabled.error(), arv::protocol::SkinUploadDenyReason::SkinsDisabled);

  config.enabled = true;
  config.allow_uploads = false;
  auto uploads_disabled = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      false,
      begin);
  ASSERT_FALSE(uploads_disabled);
  EXPECT_EQ(
      uploads_disabled.error(),
      arv::protocol::SkinUploadDenyReason::UploadsDisabled);

  config.allow_uploads = true;
  auto unauthenticated = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      false,
      false,
      false,
      false,
      begin);
  ASSERT_FALSE(unauthenticated);
  EXPECT_EQ(
      unauthenticated.error(),
      arv::protocol::SkinUploadDenyReason::NotAuthenticated);

  auto rate_limited = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      true,
      false,
      begin);
  ASSERT_FALSE(rate_limited);
  EXPECT_EQ(rate_limited.error(), arv::protocol::SkinUploadDenyReason::RateLimited);

  auto already_pending = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      true,
      false,
      false,
      begin);
  ASSERT_FALSE(already_pending);
  EXPECT_EQ(
      already_pending.error(),
      arv::protocol::SkinUploadDenyReason::UploadAlreadyPending);

  auto global_limit = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      true,
      begin);
  ASSERT_FALSE(global_limit);
  EXPECT_EQ(global_limit.error(), arv::protocol::SkinUploadDenyReason::RateLimited);
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
      false,
      begin);
  ASSERT_FALSE(too_large);
  EXPECT_EQ(too_large.error(), arv::protocol::SkinUploadDenyReason::UploadTooLarge);
}

TEST(ServerSkinRuntimeTests, RejectsInvalidFormatDisabledResolutionAndPermission) {
  arv::io::SkinConfig config;
  arv::player::ResolvedPermissions permissions;
  arv::protocol::ServerboundSkinUploadBegin begin;
  begin.format = static_cast<arv::protocol::SkinImageFormat>(99);
  begin.width = 64;
  begin.height = 64;
  begin.total_upload_bytes = 1024;

  auto invalid_format = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      false,
      begin);
  ASSERT_FALSE(invalid_format);
  EXPECT_EQ(
      invalid_format.error(),
      arv::protocol::SkinUploadDenyReason::InvalidFormat);

  begin.format = arv::protocol::SkinImageFormat::Png;
  begin.width = 128;
  begin.height = 128;
  auto disabled_resolution = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      false,
      begin);
  ASSERT_FALSE(disabled_resolution);
  EXPECT_EQ(
      disabled_resolution.error(),
      arv::protocol::SkinUploadDenyReason::ResolutionNotAllowed);

  config.resolutions[1].enabled = true;
  config.resolutions[1].permission = "skin.128";
  auto missing_permission = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      false,
      begin);
  ASSERT_FALSE(missing_permission);
  EXPECT_EQ(
      missing_permission.error(),
      arv::protocol::SkinUploadDenyReason::ResolutionNotAllowed);

  permissions.custom["skin.128"] = arv::player::TriState::Allow;
  auto allowed = arv::server::validate_skin_upload_begin(
      config,
      permissions,
      true,
      false,
      false,
      false,
      begin);
  EXPECT_TRUE(allowed);
}

TEST(ServerSkinRuntimeTests, DerivesServerSkinStoragePath) {
  const std::string hash =
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d";

  EXPECT_EQ(
      arv::server::server_skin_storage_path("/tmp/save", hash),
      std::filesystem::path("/tmp/save/player_assets/skins") / (hash + ".png"));
}
