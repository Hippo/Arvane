#pragma once

#include "arv/net/transport.hpp"

#include <vector>

namespace arv::client::detail {

struct ClientRuntimeContext;

class ClientNetworkRouter {
public:
  void process_events(ClientRuntimeContext& runtime);
  void handle_event(ClientRuntimeContext& runtime, net::NetworkEvent event);
  void handle_message(ClientRuntimeContext& runtime, const std::vector<u8>& packet);
};

} // namespace arv::client::detail
