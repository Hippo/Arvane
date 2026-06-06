#include "client_protocol_send.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/vehicles.hpp"

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
  protocol::ServerboundHello hello;
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
      protocol::MessageType::ServerboundHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_serverbound_hello(hello));
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
      protocol::MessageType::ServerboundAuthResponse,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_auth_response(response));
}

net::NetworkResult ClientProtocolSender::send_keep_alive(
    const protocol::ServerboundKeepAlive& keep_alive) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundKeepAlive,
      protocol::ReliabilityClass::UnreliableEvent,
      protocol::NetworkLane::Session,
      protocol::serialize_serverbound_keep_alive(keep_alive));
}

net::NetworkResult ClientProtocolSender::send_chat_message(
    const protocol::ServerboundChatMessage& message) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_chat_message(message));
}

net::NetworkResult ClientProtocolSender::send_chunk_interest_update(
    const protocol::ServerboundChunkInterestUpdate& update) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundChunkInterestUpdate,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_chunk_interest_update(update));
}

net::NetworkResult ClientProtocolSender::send_player_move(
    const protocol::ServerboundPlayerMove& move) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundPlayerMove,
      protocol::ReliabilityClass::UnreliableLatestWins,
      protocol::NetworkLane::Movement,
      protocol::serialize_serverbound_player_move(move));
}

net::NetworkResult ClientProtocolSender::send_vehicle_move(
    const protocol::ServerboundVehicleMove& move) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundVehicleMove,
      protocol::ReliabilityClass::UnreliableLatestWins,
      protocol::NetworkLane::Movement,
      protocol::serialize_serverbound_vehicle_move(move));
}

net::NetworkResult ClientProtocolSender::send_ride_entity_request(
    const protocol::ServerboundRideEntityRequest& request) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundRideEntityRequest,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_ride_entity_request(request));
}

net::NetworkResult ClientProtocolSender::send_dismount_request(
    const protocol::ServerboundDismountRequest& request) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundDismountRequest,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_dismount_request(request));
}

net::NetworkResult ClientProtocolSender::send_vehicle_action(
    const protocol::ServerboundVehicleAction& action) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }

  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundVehicleAction,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_vehicle_action(action));
}

net::NetworkResult ClientProtocolSender::send_inventory_action(
    protocol::ServerboundInventoryActionRequest request) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundInventoryActionRequest,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_inventory_action_request(request));
}

net::NetworkResult ClientProtocolSender::send_attack_entity(
    const protocol::ServerboundAttackEntity& attack) {
  net::ITransport* transport = connection_.transport();
  if (!transport) {
    return net::NetworkResult::failure(net::NetworkError{"client has no transport"});
  }
  return send_protocol_message(
      *transport,
      connection_.connection_id(),
      protocol::MessageType::ServerboundAttackEntity,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_serverbound_attack_entity(attack));
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
      protocol::MessageType::ServerboundSkinRequest,
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
      protocol::MessageType::ServerboundSkinUploadBegin,
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
      protocol::MessageType::ServerboundSkinUploadData,
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
      protocol::MessageType::ServerboundSkinUploadChunk,
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
      protocol::MessageType::ServerboundSkinUploadEnd,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_upload_end(end));
}

} // namespace arv::client::detail
