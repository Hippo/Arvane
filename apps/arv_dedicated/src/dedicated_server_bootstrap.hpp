#pragma once

#include "dedicated_error.hpp"
#include "dedicated_save_state.hpp"

#include "arv/core/expected.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/transport.hpp"
#include "arv/server/server_runtime.hpp"

#include <memory>

namespace arv::net {
class GameNetworkingSocketsEnvironment;
} // namespace arv::net

namespace arv::dedicated {

[[nodiscard]] Expected<void, DedicatedError> initialize_game_networking_environment(
    const net::GameNetworkingSocketsEnvironment& gns);

[[nodiscard]] std::unique_ptr<net::ITransport> make_dedicated_server_transport();

[[nodiscard]] Expected<void, DedicatedError> start_dedicated_server_runtime(
    server::ServerRuntime& server,
    const server::ServerStartInfo& start_info);

void restore_loaded_world_into_server(
    server::ServerRuntime& server,
    const persistence::LoadedSaveData& loaded_save,
    const entity::EntityRegistry& entity_registry);

void log_dedicated_server_ready(
    const io::EngineConfig& config,
    const DedicatedLoadedWorld& loaded_world,
    const server::ServerRuntime& server);

} // namespace arv::dedicated
