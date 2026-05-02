#pragma once

#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_type.hpp"

#include <vector>

namespace arv::client::detail {

class ClientConnectionRuntime;

class ClientProtocolSender {
public:
  explicit ClientProtocolSender(ClientConnectionRuntime& connection);

  net::NetworkResult send_client_hello();

  net::NetworkResult send_chunk_interest_update(
      const protocol::ChunkInterestUpdate& update);

  net::NetworkResult send_input_batch(
      TickIndex tick_index,
      const player::PlayerCommand& command);

  net::NetworkResult send_inventory_action(
      protocol::InventoryActionRequest request);

private:
  ClientConnectionRuntime& connection_;
};

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload);

} // namespace arv::client::detail
