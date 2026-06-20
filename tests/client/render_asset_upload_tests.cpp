#include "arv/client/render_asset_upload.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] arv::render::TextureArrayUploadData valid_texture_array_upload() {
  return {
      .width = 1U,
      .height = 1U,
      .layers = 1U,
      .rgba_pixels = {255U, 255U, 255U, 255U}};
}

} // namespace

TEST(RenderAssetUploadTests, UploadsEntityModelsAndAnimationsThroughCallbacks) {
  const std::vector<arv::render::EntityModelAsset> models{
      {.id = arv::render::EntityModelID{2U}, .name = "alpha"},
      {.id = arv::render::EntityModelID{3U}, .name = "beta"}};
  const std::vector<arv::render::EntityAnimationAsset> animations{
      {.id = arv::render::EntityAnimationID{4U}, .name = "open"},
      {.id = arv::render::EntityAnimationID{5U}, .name = "close"}};
  std::vector<std::string> calls;

  const auto upload_model = [&](const arv::render::EntityModelAsset& model) {
    calls.push_back("model:" + model.name);
  };
  const auto upload_animation =
      [&](const arv::render::EntityAnimationAsset& animation) {
    calls.push_back("animation:" + animation.name);
  };

  const arv::client::EntityRenderAssetUploadSummary summary =
      arv::client::upload_entity_render_asset_records(
          models,
          animations,
          {.upload_model =
               arv::FunctionRef<void(const arv::render::EntityModelAsset&)>{
                   upload_model},
           .upload_animation =
               arv::FunctionRef<void(
                   const arv::render::EntityAnimationAsset&)>{
                   upload_animation}});

  EXPECT_EQ(summary.models, 2U);
  EXPECT_EQ(summary.animations, 2U);
  const std::vector<std::string> expected{
      "model:alpha",
      "model:beta",
      "animation:open",
      "animation:close"};
  EXPECT_EQ(calls, expected);
}

TEST(RenderAssetUploadTests, SkipsMissingCallbacks) {
  const std::vector<arv::render::EntityModelAsset> models{
      {.id = arv::render::EntityModelID{2U}, .name = "alpha"}};
  const std::vector<arv::render::EntityAnimationAsset> animations{
      {.id = arv::render::EntityAnimationID{4U}, .name = "open"}};
  std::vector<std::string> calls;

  const auto upload_animation =
      [&](const arv::render::EntityAnimationAsset& animation) {
    calls.push_back(animation.name);
  };

  const arv::client::EntityRenderAssetUploadSummary summary =
      arv::client::upload_entity_render_asset_records(
          models,
          animations,
          {.upload_animation =
               arv::FunctionRef<void(
                   const arv::render::EntityAnimationAsset&)>{
                   upload_animation}});

  EXPECT_EQ(summary.models, 0U);
  EXPECT_EQ(summary.animations, 1U);
  const std::vector<std::string> expected{"open"};
  EXPECT_EQ(calls, expected);
}

TEST(RenderAssetUploadTests, UploadTextureArrayWithStatusCallsBackendWhenValid) {
  const arv::render::TextureArrayUploadData upload = valid_texture_array_upload();
  int backend_calls = 0;
  int status_calls = 0;

  const bool uploaded = arv::client::upload_texture_array_with_status(
      upload,
      "invalid",
      [&backend_calls](const arv::render::TextureArrayUploadData& data) -> bool {
        ++backend_calls;
        return data.valid();
      },
      [&status_calls](std::string_view) {
        ++status_calls;
      });

  EXPECT_TRUE(uploaded);
  EXPECT_EQ(backend_calls, 1);
  EXPECT_EQ(status_calls, 0);
}

TEST(RenderAssetUploadTests, UploadTextureArrayWithStatusReportsInvalidUpload) {
  int backend_calls = 0;
  std::string status;

  const bool uploaded = arv::client::upload_texture_array_with_status(
      {},
      "atlas upload was empty",
      [&backend_calls](const arv::render::TextureArrayUploadData&) -> bool {
        ++backend_calls;
        return true;
      },
      [&status](std::string_view text) {
        status = std::string{text};
      });

  EXPECT_FALSE(uploaded);
  EXPECT_EQ(backend_calls, 0);
  EXPECT_EQ(status, "atlas upload was empty");
}

TEST(RenderAssetUploadTests, UploadTextureArrayWithStatusFailsWithoutBackend) {
  const arv::render::TextureArrayUploadData upload = valid_texture_array_upload();

  EXPECT_FALSE(arv::client::upload_texture_array_with_status(
      upload,
      "unused",
      {},
      {}));
}
