#include "web_client_assets.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/client_gameplay_assets.hpp"
#include "arv/client/client_pack_sync.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/particles.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/crafting/crafting.hpp"
#include "arv/render/render_types.hpp"
#include "web_application.hpp"
#include "web_platform.hpp"
#include "web_player_skins.hpp"
#include "web_render_host.hpp"
#include "web_ui_host.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace arv::web {
namespace {

void report_upload_status(std::string_view status) {
  set_status(std::string{status});
}

void upload_entity_models(const client::EntityRenderAssets& assets) {
  clear_web_entity_models();
  clear_web_entity_animations();
  const auto upload_model = [](const render::EntityModelAsset& model) {
    set_web_entity_model(model);
  };
  const auto upload_animation =
      [](const render::EntityAnimationAsset& animation) {
    set_web_entity_animation(animation);
  };
  (void)client::upload_entity_render_asset_records(
      assets.models,
      assets.animations,
      {.upload_model =
           FunctionRef<void(const render::EntityModelAsset&)>{upload_model},
       .upload_animation =
           FunctionRef<void(const render::EntityAnimationAsset&)>{
               upload_animation}});
}

[[nodiscard]] bool upload_chunk_texture_array(
    const render::TextureArrayUploadData& upload) {
  const auto upload_backend =
      [](const render::TextureArrayUploadData& data) -> bool {
    return set_web_chunk_texture_atlas(data);
  };
  return client::upload_texture_array_with_status(
      upload,
      "block texture atlas upload was empty",
      upload_backend,
      report_upload_status);
}

[[nodiscard]] bool upload_entity_texture_array(
    const render::TextureArrayUploadData& upload) {
  const auto upload_backend =
      [](const render::TextureArrayUploadData& data) -> bool {
    return set_web_entity_texture_atlas(data);
  };
  return client::upload_texture_array_with_status(
      upload,
      "entity texture atlas upload was empty",
      upload_backend,
      report_upload_status);
}

[[nodiscard]] bool upload_particle_texture_array(
    const render::TextureArrayUploadData& upload) {
  const auto upload_backend =
      [](const render::TextureArrayUploadData& data) -> bool {
    return set_web_particle_texture_atlas(data);
  };
  return client::upload_texture_array_with_status(
      upload,
      "particle texture atlas upload was empty",
      upload_backend,
      report_upload_status);
}

[[nodiscard]] bool upload_item_texture_array(
    const render::TextureArrayUploadData& upload) {
  const auto upload_backend =
      [](const render::TextureArrayUploadData& data) -> bool {
    return set_web_ui_item_atlas(data);
  };
  return client::upload_texture_array_with_status(
      upload,
      "item texture atlas upload was empty",
      upload_backend,
      report_upload_status);
}

[[nodiscard]] std::string web_gameplay_asset_error_status(
    const client::ClientGameplayAssetLoadError& error) {
  switch (error.stage) {
  case client::ClientGameplayAssetLoadStage::EntityRenderAssets:
    return "failed to load web entity assets: " +
        error.category +
        ": " +
        error.message;
  case client::ClientGameplayAssetLoadStage::ItemAnimations:
    return "failed to " +
        error.operation +
        " web item animations: " +
        error.message;
  case client::ClientGameplayAssetLoadStage::UiTextureAssets:
    return "failed to build web item atlas: " +
        error.operation +
        ": " +
        error.message;
  }
  return "failed to load web entity assets: " + error.message;
}

} // namespace

void clear_chunk_meshes() {
  for (const WebChunkDrawState& state : g_app.renderer.chunk_draws) {
    remove_web_chunk_mesh(state.draw_item.key);
  }
  g_app.renderer.chunk_draws.clear();
  clear_web_fallback_color_vertices();
  g_app.renderer.announced_first_upload = false;
  g_app.renderer.announced_first_entity_upload = false;
}

void clear_web_ui_assets() {
  g_app.ui.text_measurer.reset();
  g_app.ui.fonts = asset::FontSystem{};
  g_app.ui.font_stacks = {};
  g_app.ui.ready = false;
}

void clear_entity_render_assets() {
  clear_web_fallback_color_vertices();
  clear_web_entity_models();
  clear_web_entity_animations();
  reset_web_player_skin_render_assets();
  g_app.entity_assets = {};
  g_app.renderer.announced_first_entity_upload = false;
}

void clear_block_render_assets() {
  if (g_app.session != nullptr) {
    g_app.session->configure_particles(nullptr);
  }
  clear_chunk_meshes();
  clear_entity_render_assets();
  clear_web_ui_assets();
  g_app.block_assets = {};
}

bool ensure_block_render_assets_loaded() {
  const std::string* registry_snapshot = g_app.session == nullptr
      ? nullptr
      : &g_app.session->multiplayer_registry_snapshot_json();
  if (g_app.session == nullptr ||
      registry_snapshot->empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.block_assets.ready &&
      client::client_pack_cache_identity_matches(
          g_app.block_assets.registry_snapshot_json,
          std::span<const std::filesystem::path>{g_app.block_assets.pack_cache_paths},
          *registry_snapshot,
          cache_paths)) {
    if (g_app.session != nullptr) {
      g_app.session->configure_particles(&g_app.block_assets.particle_assets.registry);
    }
    return true;
  }

  clear_block_render_assets();
  std::vector<std::filesystem::path> paths =
      client::copy_client_pack_cache_paths(cache_paths);
  auto packs_result = asset::PackManager::load_client_bundle_roots(paths);
  if (!packs_result) {
    set_status("failed to mount web block assets: " + packs_result.error().describe());
    return false;
  }

  asset::PackManager packs = std::move(packs_result.value());
  asset::ResourceManager resources(packs);
  if (!load_web_language(packs)) {
    return false;
  }
  auto loaded_assets = client::load_client_block_render_assets(packs, resources);
  if (!loaded_assets) {
    set_status(
        "failed to load web block assets: " +
        loaded_assets.error().category +
        ": " +
        loaded_assets.error().message);
    return false;
  }

  client::ClientBlockRenderAssets block_assets = std::move(loaded_assets.value());
  if (auto names = client::apply_client_block_display_names(
          g_app.ui.language,
          block_assets.block_registry);
      !names) {
    set_status(
        "failed to localize web block names: " +
        names.error().category +
        ": " +
        names.error().message);
    return false;
  }
  const render::TextureArrayUploadData atlas_upload =
      client::texture_array_upload_from_atlas(block_assets.block_atlas.atlas);
  if (!upload_chunk_texture_array(atlas_upload)) {
    return false;
  }

  const std::vector<std::string> block_particle_textures =
      client::block_particle_texture_assets(block_assets.block_registry);
  auto particle_assets = client::load_particle_assets_from_packs(
      packs,
      resources,
      client::default_particle_atlas_options(),
      block_particle_textures);
  if (!particle_assets) {
    set_status(
        "failed to load web particle assets: " +
        particle_assets.error().source +
        ": " +
        particle_assets.error().message);
    return false;
  }
  const render::TextureArrayUploadData particle_atlas_upload =
      client::texture_array_upload_from_atlas(particle_assets.value().atlas);
  if (!upload_particle_texture_array(particle_atlas_upload)) {
    return false;
  }

  g_app.block_assets.loaded = std::move(block_assets);
  g_app.block_assets.particle_assets = std::move(particle_assets.value());
  g_app.block_assets.registry_snapshot_json = *registry_snapshot;
  g_app.block_assets.pack_cache_paths = std::move(paths);
  g_app.block_assets.ready = true;
  if (g_app.session != nullptr) {
    g_app.session->configure_particles(&g_app.block_assets.particle_assets.registry);
  }
  set_status("web block textures and models ready");
  return true;
}

bool ensure_entity_render_assets_loaded() {
  if (!ensure_block_render_assets_loaded()) {
    return false;
  }
  const std::string* registry_snapshot = g_app.session == nullptr
      ? nullptr
      : &g_app.session->multiplayer_registry_snapshot_json();
  if (g_app.session == nullptr ||
      registry_snapshot->empty()) {
    return false;
  }
  const std::span<const std::filesystem::path> cache_paths =
      g_app.session->multiplayer_pack_cache_paths();
  if (cache_paths.empty()) {
    return false;
  }
  if (g_app.entity_assets.ready &&
      client::client_pack_cache_identity_matches(
          g_app.entity_assets.registry_snapshot_json,
          std::span<const std::filesystem::path>{g_app.entity_assets.pack_cache_paths},
          *registry_snapshot,
          cache_paths)) {
    return true;
  }

  clear_entity_render_assets();
  std::vector<std::filesystem::path> paths =
      client::copy_client_pack_cache_paths(cache_paths);
  auto packs_result = asset::PackManager::load_client_bundle_roots(paths);
  if (!packs_result) {
    set_status("failed to mount web entity assets: " + packs_result.error().describe());
    return false;
  }

  asset::PackManager packs = std::move(packs_result.value());
  asset::ResourceManager resources(packs);
  if (!load_web_language(packs)) {
    return false;
  }
  client::ClientGameplayAssetLoadOptions gameplay_asset_options;
  gameplay_asset_options.ui_texture_atlas_options =
      {.max_page_size = 1024,
       .max_pages = 2,
       .padding = 2,
       .require_uniform_non_fallback_size = false};
  auto gameplay_assets_result = client::load_client_gameplay_assets(
      packs,
      resources,
      g_app.block_assets.loaded.block_registry,
      g_app.block_assets.loaded.block_atlas.render_data,
      g_app.block_assets.loaded.block_models,
      gameplay_asset_options);
  if (!gameplay_assets_result) {
    set_status(web_gameplay_asset_error_status(gameplay_assets_result.error()));
    return false;
  }
  client::ClientGameplayAssets gameplay_assets =
      std::move(gameplay_assets_result.value());
  client::ClientEntityRenderAssets& loaded_assets =
      gameplay_assets.entity_assets;
  if (auto names = client::apply_client_item_display_names(
          g_app.ui.language,
          g_app.block_assets.loaded.block_registry,
          loaded_assets.item_registry);
      !names) {
    set_status(
        "failed to localize web item names: " +
        names.error().category +
        ": " +
        names.error().message);
    return false;
  }
  auto crafting_result =
      crafting::load_crafting_registry_from_packs(packs, loaded_assets.item_registry);
  if (!crafting_result) {
    set_status("failed to load web crafting recipes: " + crafting_result.error().message);
    return false;
  }
  g_app.entity_assets.crafting_registry = std::move(crafting_result.value().registry);
  if (auto names = client::apply_client_entity_display_names(
          g_app.ui.language,
          loaded_assets.entity_registry);
      !names) {
    set_status(
        "failed to localize web entity names: " +
        names.error().category +
        ": " +
        names.error().message);
    return false;
  }
  client::ClientUiTextureAssets ui_assets =
      std::move(gameplay_assets.ui_texture_assets);
  const render::TextureArrayUploadData item_atlas_upload =
      client::texture_array_upload_from_atlas(ui_assets.item_atlas);
  if (!upload_item_texture_array(item_atlas_upload)) {
    return false;
  }
  g_app.entity_assets.crosshair_image = ui_assets.crosshair_image;

  const render::TextureArrayUploadData atlas_upload =
      client::texture_array_upload_from_atlas(
          loaded_assets.render_assets.texture_atlas);
  if (!upload_entity_texture_array(atlas_upload)) {
    return false;
  }
  upload_entity_models(loaded_assets.render_assets);

  g_app.entity_assets.loaded = std::move(loaded_assets);
  if (g_app.session != nullptr) {
    g_app.session->configure_vehicles(&g_app.entity_assets.loaded.vehicle_registry);
  }
  g_app.entity_assets.item_animation_library =
      std::move(gameplay_assets.item_animation_library);
  g_app.entity_assets.item_atlas = std::move(ui_assets.item_atlas);
  g_app.entity_assets.registry_snapshot_json = *registry_snapshot;
  g_app.entity_assets.pack_cache_paths = std::move(paths);
  g_app.entity_assets.ready = true;
  set_status("web entity models, textures, and animations ready");
  return true;
}

} // namespace arv::web
