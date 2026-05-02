#include "client_protocol_send.hpp"

#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"

#include "client_connection.hpp"

#include <utility>

namespace arv::client::detail {

ClientProtocolSender::ClientProtocolSender(ClientConnectionRuntime& connection) : connection_(connection) {
}

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload) {
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  return transport.send(
      {connection_id,
       reliability,
       std::move(packet),
       message_type,
       lane});
}

net::NetworkResult ClientProtocolSender::send_client_hello() {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  protocol::ClientHello hello;
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ClientHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_client_hello(hello));
}

net::NetworkResult ClientProtocolSender::send_chunk_interest_update(
    const protocol::ChunkInterestUpdate& update) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ChunkInterestUpdate,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_chunk_interest_update(update));
}

net::NetworkResult ClientProtocolSender::send_input_batch(
    TickIndex tick_index,
    const player::PlayerCommand& command) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  protocol::ClientInputBatch batch;
  batch.first_input_tick = tick_index;
  batch.inputs.emplace_back(protocol::input_sample_from_command(command));

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ClientInputBatch,
      protocol::ReliabilityClass::UnreliableLatestWins,
      protocol::NetworkLane::Movement,
      protocol::serialize_client_input_batch(batch));
}

net::NetworkResult ClientProtocolSender::send_inventory_action(
    protocol::InventoryActionRequest request) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::InventoryActionRequest,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_inventory_action_request(request));
}

} // namespace arv::client::detail
