#include "dedicated_application.hpp"

#include "dedicated_launch_options.hpp"
#include "dedicated_lifetime_log.hpp"
#include "dedicated_registry_loader.hpp"
#include "dedicated_runtime_config.hpp"
#include "dedicated_save_state.hpp"
#include "dedicated_server_bootstrap.hpp"
#include "dedicated_signals.hpp"
#include "dedicated_tick_loop.hpp"
#include "dedicated_worldgen_loader.hpp"

#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"

#include <utility>

namespace arv::dedicated {

int DedicatedApplication::run(int argc, char** argv) {
  install_shutdown_signal_handlers();

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Starting);
  const DedicatedLaunchOptions launch_options = parse_launch_options(argc, argv);

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::LoadingConfig);
  auto config_result = io::load_config(launch_options.config);
  if (!config_result) {
    return fail(dedicated_error("config", config_result.error().message));
  }
  const io::EngineConfig config = config_result.value();
  core::info(
      "dedicated",
      "config={} arvane_home={} world={} listen={}:{} save_root={}",
      config.paths.config_file.string(),
      config.paths.arvane_home_path().string(),
      config.server.world,
      config.network.bind_address,
      config.network.port,
      config.paths.dedicated_server_save_path().string());

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::LoadingRegistries);
  auto registries_result = load_dedicated_registries(config);
  if (!registries_result) {
    return fail(registries_result.error());
  }
  DedicatedRegistries registries = std::move(registries_result.value());

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::LoadingWorlds);
  auto worldgen_result = load_dedicated_worldgen(config);
  if (!worldgen_result) {
    return fail(worldgen_result.error());
  }
  DedicatedWorldgen worldgen = std::move(worldgen_result.value());

  auto loaded_world_result = load_or_create_dedicated_world(
      config,
      worldgen.default_config,
      registries.block_registry,
      registries.block_registry_hash,
      registries.item_registry,
      registries.item_registry_hash,
      registries.language);
  if (!loaded_world_result) {
    return fail(loaded_world_result.error());
  }
  DedicatedLoadedWorld loaded_world = std::move(loaded_world_result.value());

  auto worldgen_palette = resolve_dedicated_worldgen_palette(
      loaded_world.active_generation_config,
      registries.block_registry);
  if (!worldgen_palette) {
    return fail(worldgen_palette.error());
  }

  const DedicatedRuntimeConfig runtime_config =
      make_dedicated_runtime_config(config);
  auto start_info_result = make_server_start_info(
      config,
      runtime_config,
      loaded_world,
      registries,
      std::move(worldgen_palette.value()));
  if (!start_info_result) {
    return fail(start_info_result.error());
  }
  server::ServerStartInfo server_start_info = std::move(start_info_result.value());

  net::GameNetworkingSocketsEnvironment gns;
  auto gns_result = initialize_game_networking_environment(gns);
  if (!gns_result) {
    return fail(gns_result.error());
  }

  server::ServerRuntime server_runtime(make_dedicated_server_transport());
  auto start_result = start_dedicated_server_runtime(
      server_runtime,
      server_start_info);
  if (!start_result) {
    return fail(start_result.error());
  }

  restore_loaded_world_into_server(
      server_runtime,
      loaded_world.loaded_save,
      registries.entity_registry);
  log_dedicated_server_ready(config, loaded_world, server_runtime);

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Running);
  (void)run_dedicated_tick_loop(
      server_runtime,
      config.simulation.base_tps,
      launch_options.max_ticks);

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Stopping);
  server_runtime.shutdown();

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Saving);
  auto save_result = save_dedicated_world(
      config,
      loaded_world.server_save_root,
      loaded_world.loaded_save,
      server_runtime,
      loaded_world.active_generation_config,
      loaded_world.world_seed,
      loaded_world.world_height,
      registries.block_registry,
      registries.block_registry_hash,
      registries.item_registry,
      registries.item_registry_hash,
      registries.language);
  if (!save_result) {
    return fail(save_result.error());
  }

  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Stopped);
  return 0;
}

int DedicatedApplication::fail(const DedicatedError& error) {
  core::error(error.category, error.message);
  transition_lifetime(lifetime_, server::DedicatedServerLifetimeState::Failed);
  return 1;
}

int run_dedicated_server(int argc, char** argv) {
  DedicatedApplication app;
  return app.run(argc, argv);
}

} // namespace arv::dedicated
