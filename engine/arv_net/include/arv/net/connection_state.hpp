#pragma once

#include "arv/core/id.hpp"

namespace arv::net {

struct ConnectionTag {};
using ConnectionID = ID<ConnectionTag, u32>;

enum class ConnectionState {
  Disconnected,
  Connecting,
  TransportConnected,
  ProtocolHandshake,
  RegistrySync,
  WorldJoin,
  InGame,
  Disconnecting,
};

} // namespace arv::net
