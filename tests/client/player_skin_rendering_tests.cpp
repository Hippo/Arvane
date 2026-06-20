#include "arv/client/player_skin_rendering.hpp"

#include "arv/asset/resource.hpp"
#include "arv/render/render_commands.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("arvane_player_skin_rendering_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

[[nodiscard]] std::vector<arv::u8> read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::string text{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  return {text.begin(), text.end()};
}

void write_binary_file(
    const std::filesystem::path& path,
    std::span<const arv::u8> bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] arv::asset::Image rgba_image(arv::u32 width, arv::u32 height) {
  arv::asset::Image image;
  image.width = width;
  image.height = height;
  image.channels = 4;
  image.pixels.assign(
      static_cast<arv::usize>(width) * static_cast<arv::usize>(height) * 4U,
      255U);
  return image;
}

} // namespace

TEST(PlayerSkinRenderingTests, UploadsValidNewReceiptsOnly) {
  arv::client::PlayerSkinReceiptMap receipts{
      {"existing", arv::render::TextureRegionHandle{1U, 1U}},
  };
  arv::HashMap<std::string, arv::asset::Image> images;
  images[""] = rgba_image(1U, 1U);
  images["existing"] = rgba_image(1U, 1U);
  images["invalid"] = arv::asset::Image{.width = 1U, .height = 1U, .pixels = {1U, 2U, 3U}};
  images["new"] = rgba_image(1U, 1U);

  std::vector<std::string> uploaded_hashes;
  const auto upload =
      [&](std::string_view skin_hash,
          const arv::asset::Image& image)
      -> std::optional<arv::render::TextureRegionHandle> {
    EXPECT_EQ(image.pixels.size(), 4U);
    uploaded_hashes.emplace_back(skin_hash);
    return arv::render::TextureRegionHandle{7U, 2U};
  };

  const arv::client::PlayerSkinUploadSummary summary =
      arv::client::upload_player_skin_render_images_to_receipts(
          receipts,
          images,
          arv::FunctionRef<std::optional<arv::render::TextureRegionHandle>(
              std::string_view,
              const arv::asset::Image&)>{upload});

  EXPECT_EQ(summary.attempted, 2U);
  EXPECT_EQ(summary.uploaded, 1U);
  ASSERT_EQ(uploaded_hashes.size(), 1U);
  EXPECT_EQ(uploaded_hashes.front(), "new");
  EXPECT_EQ(receipts.find("existing")->second, (arv::render::TextureRegionHandle{1U, 1U}));
  EXPECT_EQ(receipts.find("new")->second, (arv::render::TextureRegionHandle{7U, 2U}));
  EXPECT_EQ(receipts.find("invalid"), receipts.end());
  EXPECT_EQ(receipts.find(""), receipts.end());
}

TEST(PlayerSkinRenderingTests, PrunesInvalidAndNonResidentReceipts) {
  arv::client::PlayerSkinReceiptMap receipts{
      {"keep", arv::render::TextureRegionHandle{1U, 1U}},
      {"missing", arv::render::TextureRegionHandle{2U, 1U}},
      {"invalid", arv::render::TextureRegionHandle{}},
  };
  const auto is_resident =
      [](std::string_view skin_hash, arv::render::TextureRegionHandle handle) {
    return skin_hash == "keep" && handle == arv::render::TextureRegionHandle{1U, 1U};
  };

  const arv::usize removed = arv::client::prune_player_skin_receipts(
      receipts,
      arv::FunctionRef<bool(
          std::string_view,
          arv::render::TextureRegionHandle)>{is_resident});

  EXPECT_EQ(removed, 2U);
  ASSERT_EQ(receipts.size(), 1U);
  EXPECT_NE(receipts.find("keep"), receipts.end());
}

TEST(PlayerSkinRenderingTests, ReadyReceiptRequiresReadyCallbackAndMarksUsed) {
  const arv::render::TextureRegionHandle ready_handle{3U, 4U};
  const arv::render::TextureRegionHandle not_ready_handle{5U, 6U};
  const arv::client::PlayerSkinReceiptMap receipts{
      {"ready", ready_handle},
      {"not_ready", not_ready_handle},
  };
  std::vector<arv::render::TextureRegionHandle> marked_handles;
  const auto ready =
      [](std::string_view skin_hash, arv::render::TextureRegionHandle handle) {
    return skin_hash == "ready" && handle == arv::render::TextureRegionHandle{3U, 4U};
  };
  const auto mark_used = [&](arv::render::TextureRegionHandle handle) {
    marked_handles.push_back(handle);
  };

  const std::optional<arv::render::TextureRegionHandle> ready_receipt =
      arv::client::ready_player_skin_receipt(
          receipts,
          "ready",
          {.ready =
               arv::FunctionRef<bool(
                   std::string_view,
                   arv::render::TextureRegionHandle)>{ready},
           .mark_used =
               arv::FunctionRef<void(arv::render::TextureRegionHandle)>{mark_used}});
  EXPECT_EQ(ready_receipt, ready_handle);
  ASSERT_EQ(marked_handles.size(), 1U);
  EXPECT_EQ(marked_handles.front(), ready_handle);

  const std::optional<arv::render::TextureRegionHandle> not_ready_receipt =
      arv::client::ready_player_skin_receipt(
          receipts,
          "not_ready",
          {.ready =
               arv::FunctionRef<bool(
                   std::string_view,
                   arv::render::TextureRegionHandle)>{ready},
           .mark_used =
               arv::FunctionRef<void(arv::render::TextureRegionHandle)>{mark_used}});
  EXPECT_FALSE(not_ready_receipt.has_value());
  EXPECT_EQ(marked_handles.size(), 1U);
}

TEST(PlayerSkinRenderingTests, RefreshReceiptsPrunesCollectsAndUploads) {
  const std::filesystem::path source_skin =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT} /
      "textures/default_skin/default.png";
  const std::vector<arv::u8> bytes = read_binary_file(source_skin);
  ASSERT_FALSE(bytes.empty());
  const std::string skin_hash = arv::protocol::skin_hash_for_bytes(bytes);
  auto decoded = arv::asset::decode_png_image_bytes(bytes, {"test:skin"});
  ASSERT_TRUE(decoded);

  TempDir temp;
  write_binary_file(
      arv::client::cached_skin_path(temp.path, skin_hash),
      bytes);

  arv::client::LocalPlayerIdentity identity;
  identity.arvane_home = temp.path;
  const arv::protocol::PlayerProfileInfo profile{
      .display_name = "Hippo",
      .skin =
          {.skin_hash = skin_hash,
           .format = arv::protocol::SkinImageFormat::Png,
           .width = static_cast<arv::u16>(decoded.value().width),
           .height = static_cast<arv::u16>(decoded.value().height),
           .slim_model = false},
  };
  arv::client::PlayerSkinReceiptMap receipts{
      {"stale", arv::render::TextureRegionHandle{9U, 1U}},
  };
  const auto is_resident =
      [](std::string_view, arv::render::TextureRegionHandle) {
    return false;
  };
  std::vector<std::string> uploaded_hashes;
  const auto upload =
      [&](std::string_view uploaded_skin_hash,
          const arv::asset::Image& image)
      -> std::optional<arv::render::TextureRegionHandle> {
    uploaded_hashes.emplace_back(uploaded_skin_hash);
    EXPECT_EQ(image.width, decoded.value().width);
    EXPECT_EQ(image.height, decoded.value().height);
    return arv::render::TextureRegionHandle{7U, 1U};
  };

  const arv::client::PlayerSkinReceiptRefreshSummary summary =
      arv::client::refresh_player_skin_render_receipts(
          receipts,
          identity,
          std::span<const arv::protocol::PlayerProfileInfo>{&profile, 1U},
          {.is_resident =
               arv::FunctionRef<bool(
                   std::string_view,
                   arv::render::TextureRegionHandle)>{is_resident},
           .upload =
               arv::FunctionRef<std::optional<arv::render::TextureRegionHandle>(
                   std::string_view,
                   const arv::asset::Image&)>{upload}});

  EXPECT_EQ(summary.pruned, 1U);
  EXPECT_EQ(summary.upload.attempted, 1U);
  EXPECT_EQ(summary.upload.uploaded, 1U);
  ASSERT_EQ(uploaded_hashes.size(), 1U);
  EXPECT_EQ(uploaded_hashes.front(), skin_hash);
  EXPECT_EQ(receipts.find("stale"), receipts.end());
  EXPECT_EQ(receipts.find(skin_hash)->second, (arv::render::TextureRegionHandle{7U, 1U}));
}

TEST(PlayerSkinRenderingTests, ConvertsRenderSnapshotToUiTextureView) {
  arv::render::PlayerSkinTextureSnapshot snapshot;
  snapshot.region = arv::render::AtlasRegion{
      .page_index = 2U,
      .x = 4U,
      .y = 6U,
      .width = 64U,
      .height = 32U,
  };
  snapshot.atlas_width = 512U;
  snapshot.atlas_height = 1024U;
  snapshot.image_width = 64U;
  snapshot.image_height = 32U;

  const arv::ui::PlayerSkinTextureView view =
      arv::client::player_skin_texture_view_from_snapshot(snapshot);

  EXPECT_TRUE(view.valid);
  EXPECT_EQ(view.page, 2U);
  EXPECT_EQ(view.atlas_width, 512U);
  EXPECT_EQ(view.atlas_height, 1024U);
  EXPECT_EQ(view.x, 4U);
  EXPECT_EQ(view.y, 6U);
  EXPECT_EQ(view.width, 64U);
  EXPECT_EQ(view.height, 32U);
  EXPECT_EQ(view.image_width, 64U);
  EXPECT_EQ(view.image_height, 32U);
}

TEST(PlayerSkinRenderingTests, CollectsMissingMenuDefaultSkinsThroughCallback) {
  arv::client::PlayerSkinReceiptMap receipts{
      {std::string{arv::client::DefaultMenuPlayerSlimSkinKey},
       arv::render::TextureRegionHandle{2U, 1U}},
  };
  const arv::client::LocalProfileStore profiles{};
  const std::vector<arv::client::LocalProfileSkin> pending_create{};
  const arv::HashMap<
      std::string,
      std::vector<arv::client::LocalProfileSkin>> pending_edit{};
  std::vector<std::string> loaded_assets;
  const auto load_default =
      [&](std::string_view texture_asset) -> std::optional<arv::asset::Image> {
    loaded_assets.emplace_back(texture_asset);
    return rgba_image(64U, 64U);
  };

  const arv::HashMap<std::string, arv::asset::Image> images =
      arv::client::collect_missing_menu_player_skin_render_images(
          {},
          profiles,
          pending_create,
          pending_edit,
          receipts,
          arv::FunctionRef<std::optional<arv::asset::Image>(std::string_view)>{
              load_default});

  ASSERT_EQ(loaded_assets.size(), 1U);
  EXPECT_EQ(
      loaded_assets.front(),
      std::string{arv::client::DefaultMenuPlayerSkinTextureAsset});
  EXPECT_NE(
      images.find(std::string{arv::client::DefaultMenuPlayerSkinKey}),
      images.end());
  EXPECT_EQ(
      images.find(std::string{arv::client::DefaultMenuPlayerSlimSkinKey}),
      images.end());
}

TEST(PlayerSkinRenderingTests, BuildsMenuTextureViewsAndExcludesSlimDefaultFromSkinMap) {
  const arv::render::TextureRegionHandle default_handle{1U, 1U};
  const arv::render::TextureRegionHandle slim_handle{2U, 1U};
  const arv::render::TextureRegionHandle custom_handle{3U, 1U};
  const arv::client::PlayerSkinReceiptMap receipts{
      {std::string{arv::client::DefaultMenuPlayerSkinKey}, default_handle},
      {std::string{arv::client::DefaultMenuPlayerSlimSkinKey}, slim_handle},
      {"custom", custom_handle},
      {"invalid", arv::render::TextureRegionHandle{}},
  };
  std::vector<arv::render::TextureRegionHandle> marked_handles;
  const auto texture_view =
      [](std::string_view,
         arv::render::TextureRegionHandle handle)
      -> std::optional<arv::ui::PlayerSkinTextureView> {
    return arv::ui::PlayerSkinTextureView{
        .valid = true,
        .page = handle.id,
        .atlas_width = 128U,
        .atlas_height = 128U,
        .width = 64U,
        .height = 64U,
        .image_width = 64U,
        .image_height = 64U,
    };
  };
  const auto mark_used = [&](arv::render::TextureRegionHandle handle) {
    marked_handles.push_back(handle);
  };

  const arv::client::MenuPlayerSkinTextureViews views =
      arv::client::menu_player_skin_texture_views_from_receipts(
          receipts,
          {.texture_view =
               arv::FunctionRef<std::optional<arv::ui::PlayerSkinTextureView>(
                   std::string_view,
                   arv::render::TextureRegionHandle)>{texture_view},
           .mark_used =
               arv::FunctionRef<void(arv::render::TextureRegionHandle)>{mark_used}});

  EXPECT_TRUE(views.default_texture.valid);
  EXPECT_EQ(views.default_texture.page, default_handle.id);
  EXPECT_NE(views.textures.find("custom"), views.textures.end());
  EXPECT_EQ(
      views.textures.find(std::string{arv::client::DefaultMenuPlayerSlimSkinKey}),
      views.textures.end());
  EXPECT_EQ(marked_handles.size(), 3U);
}

TEST(PlayerSkinRenderingTests, ResolvesMenuPreviewSkinWithDefaultFallbacks) {
  const arv::render::TextureRegionHandle default_handle{1U, 1U};
  const arv::render::TextureRegionHandle slim_handle{2U, 1U};
  const arv::render::TextureRegionHandle custom_handle{3U, 1U};
  const arv::client::PlayerSkinReceiptMap receipts{
      {std::string{arv::client::DefaultMenuPlayerSkinKey}, default_handle},
      {std::string{arv::client::DefaultMenuPlayerSlimSkinKey}, slim_handle},
      {"custom", custom_handle},
  };

  EXPECT_EQ(
      arv::client::menu_player_skin_preview_region(receipts, "custom", false),
      custom_handle);
  EXPECT_EQ(
      arv::client::menu_player_skin_preview_region(receipts, "missing", true),
      slim_handle);
  EXPECT_EQ(
      arv::client::menu_player_skin_preview_region(receipts, "missing", false),
      default_handle);
  EXPECT_EQ(
      arv::client::menu_player_skin_preview_region(receipts, "", true),
      slim_handle);

  const arv::client::PlayerSkinReceiptMap only_slim{
      {std::string{arv::client::DefaultMenuPlayerSlimSkinKey}, slim_handle},
  };
  EXPECT_FALSE(
      arv::client::menu_player_skin_preview_region(only_slim, "missing", false)
          .has_value());
}
