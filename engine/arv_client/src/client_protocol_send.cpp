#include "client_protocol_send.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/skins.hpp"

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

net::NetworkResult ClientProtocolSender::send_client_hello(const LocalPlayerIdentity& identity) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  protocol::ClientHello hello;
  hello.requested_username_or_profile_name = identity.display_name;
  hello.auth_begin = protocol::ServerboundAuthBegin{
      identity.key_pair.public_key,
      identity.display_name,
      identity.profile_id.empty()
          ? crypto::derive_profile_id_from_public_key(identity.key_pair.public_key)
          : identity.profile_id};
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ClientHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_client_hello(hello));
}

net::NetworkResult ClientProtocolSender::send_auth_response(
    const protocol::ServerboundAuthResponse& response) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::AuthResponse,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_auth_response(response));
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
  return send_input_batch(tick_index, protocol::input_sample_from_command(command));
}

net::NetworkResult ClientProtocolSender::send_input_batch(
    TickIndex tick_index,
    const protocol::InputSample& input) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  protocol::ClientInputBatch batch;
  batch.first_input_tick = tick_index;
  batch.inputs.emplace_back(input);

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

net::NetworkResult ClientProtocolSender::send_skin_request(
    const protocol::ServerboundSkinRequest& request) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::SkinRequest,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_request(request));
}

net::NetworkResult ClientProtocolSender::send_skin_upload_begin(
    const protocol::ServerboundSkinUploadBegin& begin) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::SkinUploadBegin,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_begin(begin));
}

net::NetworkResult ClientProtocolSender::send_skin_upload_data(
    const protocol::ServerboundSkinUploadData& data) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::SkinUploadData,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_upload_data(data));
}

net::NetworkResult ClientProtocolSender::send_skin_upload_chunk(
    const protocol::ServerboundSkinUploadChunk& chunk) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::SkinUploadChunk,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_upload_chunk(chunk));
}

net::NetworkResult ClientProtocolSender::send_skin_upload_end(
    const protocol::ServerboundSkinUploadEnd& end) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::SkinUploadEnd,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_upload_end(end));
}

} // namespace arv::client::detail
