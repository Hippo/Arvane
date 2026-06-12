#include "desktop_frame_loop.hpp"

#include "arv/asset/pack.hpp"
#include "arv/core/log.hpp"
#include "arv/net/connection_state.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

namespace arv::desktop {
namespace {

[[nodiscard]] std::vector<std::filesystem::path> copy_paths(
    std::span<const std::filesystem::path> paths) {
  return {paths.begin(), paths.end()};
}

[[nodiscard]] bool same_paths(
    std::span<const std::filesystem::path> left,
    const std::vector<std::filesystem::path>& right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin());
}

} // namespace

bool DesktopFrameLoop::reload_desktop_assets(
    asset::PackManager packs,
    std::optional<asset::PackManager> fallback_packs,
    DesktopAssetLoadOptions options,
    std::string_view registry_snapshot,
    DesktopAssetSource source,
    std::vector<std::filesystem::path> remote_pack_cache_paths) {
  asset::ResourceManager next_resources =
      fallback_packs.has_value()
          ? asset::ResourceManager(packs, fallback_packs.value())
          : asset::ResourceManager(packs);

  renderer_.clear_entity_models();
  renderer_.clear_entity_animations();
  clear_player_skin_render_cache();
  auto loaded_assets = load_desktop_assets(
      next_resources,
      renderer_,
      config_,
      packs,
      ui_theme_,
      ui_metrics_,
      block_behaviors_,
      item_behaviors_,
      entity_goals_,
      options);
  if (!loaded_assets) {
    core::error(
        loaded_assets.error().category,
        loaded_assets.error().message);
    return false;
  }

  if (!registry_snapshot.empty()) {
    auto validation = validate_desktop_assets_against_registry_snapshot(
        registry_snapshot,
        loaded_assets.value());
    if (!validation) {
      core::error(
          validation.error().category,
          validation.error().message);
      return false;
    }
  }

  resources_ = std::move(next_resources);
  assets_ = std::move(loaded_assets.value());
  ui_text_measurer_ = ui::FontUiTextMeasurer(
      assets_.font_system,
      assets_.ui_font_stacks,
      ui_metrics_.scalar_to_framebuffer(1.0F));
  asset_source_ = source;
  active_remote_registry_snapshot_json_ = std::string{registry_snapshot};
  active_remote_pack_cache_paths_ = std::move(remote_pack_cache_paths);
  invalidate_ui_tree_cache();
  return true;
}

bool DesktopFrameLoop::ensure_local_assets(bool force) {
  if (!force && asset_source_ == DesktopAssetSource::Local) {
    return true;
  }

  auto local_packs = asset::PackManager::load(config_.paths.arvane_home_path());
  if (!local_packs) {
    core::error("packs", local_packs.error().describe());
    return false;
  }

  if (!reload_desktop_assets(
          std::move(local_packs.value()),
          std::nullopt,
          {},
          {},
          DesktopAssetSource::Local)) {
    return false;
  }
  core::info("packs", "restored local desktop assets and registries");
  return true;
}

bool DesktopFrameLoop::ensure_remote_assets(DesktopGameSession& active_session) {
  if (active_session.client == nullptr ||
      !active_session.remote_server.has_value() ||
      active_session.client->connection_state() != net::ConnectionState::InGame) {
    return true;
  }

  const std::span<const std::filesystem::path> cache_paths =
      active_session.client->multiplayer_pack_cache_paths();
  const std::string& registry_snapshot =
      active_session.client->multiplayer_registry_snapshot_json();
  if (cache_paths.empty() && registry_snapshot.empty()) {
    return true;
  }
  if (cache_paths.empty() || registry_snapshot.empty()) {
    core::error(
        "packs",
        "remote session entered the world without a complete pack cache and registry snapshot");
    (void)ensure_local_assets(true);
    return false;
  }

  if (asset_source_ == DesktopAssetSource::Remote &&
      registry_snapshot == active_remote_registry_snapshot_json_ &&
      same_paths(cache_paths, active_remote_pack_cache_paths_)) {
    return true;
  }

  std::vector<std::filesystem::path> remote_paths = copy_paths(cache_paths);
  auto remote_packs = asset::PackManager::load_client_bundle_roots(remote_paths);
  if (!remote_packs) {
    core::error("packs", remote_packs.error().describe());
    (void)ensure_local_assets(true);
    return false;
  }

  auto local_fallback_packs =
      asset::PackManager::load(config_.paths.arvane_home_path());
  if (!local_fallback_packs) {
    core::error("packs", local_fallback_packs.error().describe());
    (void)ensure_local_assets(true);
    return false;
  }

  DesktopAssetLoadOptions options;
  options.validate_behavior_ids = false;
  options.load_worldgen = false;
  if (!reload_desktop_assets(
          std::move(remote_packs.value()),
          std::move(local_fallback_packs.value()),
          options,
          registry_snapshot,
          DesktopAssetSource::Remote,
          std::move(remote_paths))) {
    (void)ensure_local_assets(true);
    return false;
  }

  active_session.client->configure_language(&assets_.language);
  active_session.client->configure_particles(&assets_.particle_assets.registry);
  active_session.client->configure_vehicles(&assets_.vehicle_registry);
  sessions_.refresh_inventory_slots(active_session);
  core::info(
      "packs",
      "mounted {} multiplayer pack cache root(s) for remote session",
      active_remote_pack_cache_paths_.size());
  return true;
}

} // namespace arv::desktop
