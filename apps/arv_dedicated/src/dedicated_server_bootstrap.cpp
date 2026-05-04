#include "dedicated_server_bootstrap.hpp"

#include "arv/core/log.hpp"
#include "arv/net/gns_transport.hpp"

#include <memory>

namespace arv::dedicated {

Expected<void, DedicatedError> initialize_game_networking_environment(
    const net::GameNetworkingSocketsEnvironment& gns) {
  if (!gns.initialized()) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("net", gns.error_message()));
  }
  return Expected<void, DedicatedError>::success();
}

std::unique_ptr<net::ITransport> make_dedicated_server_transport() {
  return std::make_unique<net::GameNetworkingSocketsTransport>();
}

Expected<void, DedicatedError> start_dedicated_server_runtime(
    server::ServerRuntime& server,
    const server::ServerStartInfo& start_info) {
  auto start_result = server.start(start_info);
  if (!start_result) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error("server", start_result.error().message));
  }
  return Expected<void, DedicatedError>::success();
}

void restore_loaded_world_into_server(
    server::ServerRuntime& server,
    const persistence::LoadedSaveData& loaded_save,
    const entity::EntityRegistry& entity_registry) {
  if (loaded_save.has_world_metadata) {
    server.voxel_world().set_clock(
        loaded_save.world.clock,
        server.base_tps());
  }
  for (const persistence::SavedChunkSection& chunk : loaded_save.chunks) {
    server.voxel_world().restore_saved_chunk(
        chunk.coord,
        chunk.blocks,
        chunk.lights,
        chunk.fluids);
  }
  server.voxel_world().restore_persistent_entities(
      loaded_save.entities,
      &entity_registry);
}

void log_dedicated_server_ready(
    const io::EngineConfig& config,
    const DedicatedLoadedWorld& loaded_world,
    const server::ServerRuntime& server) {
  core::info(
      "dedicated",
      "world '{}' ready: loaded_chunks={} entities={} restored_player_profiles={} chunk_y=[{}, {}]",
      config.server.world,
      server.voxel_world().loaded_chunk_count(),
      server.voxel_world().entity_count(),
      loaded_world.saved_players.size(),
      loaded_world.visible_chunk_y.first,
      loaded_world.visible_chunk_y.second);
  core::info(
      "dedicated",
      "listening on {}:{} at {} TPS",
      config.network.bind_address,
      config.network.port,
      config.simulation.base_tps);
}

} // namespace arv::dedicated
