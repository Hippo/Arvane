#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/server/dedicated_lifetime.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>

namespace {

std::atomic_bool g_shutdown_requested{false};

void handle_shutdown_signal(int) {
  g_shutdown_requested.store(true);
}

struct DedicatedLaunchOptions {
  arv::io::ConfigLoadOptions config{};
  arv::u64 max_ticks{};
};

DedicatedLaunchOptions parse_launch_options(int argc, char** argv) {
  DedicatedLaunchOptions options;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ticks") {
      options.max_ticks = static_cast<arv::u64>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--config") {
      options.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.config.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.config.missing_config_is_error = explicit_config;
  return options;
}

void transition(
    arv::server::DedicatedServerLifetime& lifetime,
    arv::server::DedicatedServerLifetimeState state,
    arv::core::Logger& logger) {
  lifetime.transition(state);
  logger.info("dedicated", arv::server::to_string(state));
}

} // namespace

int main(int argc, char** argv) {
  arv::core::Logger logger;
  arv::server::DedicatedServerLifetime lifetime;
  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGTERM, handle_shutdown_signal);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Starting, logger);

  const DedicatedLaunchOptions launch_options = parse_launch_options(argc, argv);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingConfig, logger);
  auto config_result = arv::io::load_config(launch_options.config);
  if (!config_result) {
    logger.error("config", config_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingRegistries, logger);
  auto block_definition_text = arv::io::read_text_file(
      config.paths.assets_path() / "config/blocks.json",
      "dedicated block definitions");
  if (!block_definition_text) {
    logger.error("blocks", block_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }

  auto block_definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    logger.error("blocks", block_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    logger.error("blocks", block_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry =
      std::move(block_registry_result.value());

  auto item_registry_result =
      arv::player::ItemRegistry::build_default_block_items(block_registry);
  if (!item_registry_result) {
    logger.error("items", item_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }
  const arv::player::ItemRegistry item_registry =
      std::move(item_registry_result.value());

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingWorlds, logger);

  arv::net::GameNetworkingSocketsEnvironment gns;
  if (!gns.initialized()) {
    logger.error("net", gns.error_message());
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }

  arv::server::ServerRuntime server(std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  auto start_result = server.start({
      arv::server::ServerMode::Dedicated,
      config.simulation.base_tps,
      {config.network.bind_address, config.network.port},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition()});
  if (!start_result) {
    logger.error("server", start_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed, logger);
    return 1;
  }

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Running, logger);
  const auto sleep_duration = std::chrono::duration<arv::Seconds>(
      arv::tick_duration_seconds(config.simulation.base_tps));

  arv::u64 ticks_run = 0;
  while (!g_shutdown_requested.load() &&
         (launch_options.max_ticks == 0 || ticks_run < launch_options.max_ticks)) {
    server.tick();
    ++ticks_run;
    std::this_thread::sleep_for(sleep_duration);
  }

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopping, logger);
  server.shutdown();
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Saving, logger);
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopped, logger);
  return 0;
}
