#pragma once

#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <memory>
#include <vector>

namespace arv::server {

enum class ServerMode {
  Integrated,
  Dedicated,
};

enum class ServerRuntimeState {
  Stopped,
  Running,
  Stopping,
};

struct ServerStartInfo {
  ServerMode mode{ServerMode::Integrated};
  TickRate base_tps{DefaultBaseTps};
  net::ListenAddress listen_address{};
};

class ServerRuntime {
public:
  explicit ServerRuntime(std::unique_ptr<net::ITransport> transport);

  net::NetworkResult start(const ServerStartInfo& start_info);

  void tick();

  void shutdown();

  [[nodiscard]] ServerRuntimeState state() const noexcept {
    return state_;
  }

  [[nodiscard]] TickIndex tick_index() const noexcept {
    return tick_index_;
  }

  [[nodiscard]] TickRate base_tps() const noexcept {
    return base_tps_;
  }

  [[nodiscard]] ServerMode mode() const noexcept {
    return mode_;
  }

  [[nodiscard]] const SessionRegistry& sessions() const noexcept {
    return sessions_;
  }

  [[nodiscard]] const WorldRegistry& worlds() const noexcept {
    return worlds_;
  }

  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept {
    return default_world_;
  }

  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept {
    return default_world_;
  }

private:
  void handle_network_event(net::NetworkEvent event);

  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet);

  std::unique_ptr<net::ITransport> transport_;
  ServerRuntimeState state_{ServerRuntimeState::Stopped};
  ServerMode mode_{ServerMode::Integrated};
  TickRate base_tps_{DefaultBaseTps};
  TickIndex tick_index_{};
  protocol::HandshakePolicy handshake_policy_{};
  SessionRegistry sessions_;
  WorldRegistry worlds_;
  world::WorldRuntime default_world_{DefaultWorldID};
  std::vector<net::NetworkEvent> inbound_events_;
};

} // namespace arv::server
