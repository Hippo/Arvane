#pragma once

#include "arv/client/local_identity.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/protocol/crafting.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/vehicles.hpp"

#include <vector>

namespace arv::client::detail {

class ClientConnectionRuntime;

class ClientProtocolSender {
public:
  explicit ClientProtocolSender(ClientConnectionRuntime& connection);

  net::NetworkResult send_client_hello(const LocalPlayerIdentity& identity);
  net::NetworkResult send_auth_response(const protocol::ServerboundAuthResponse& response);
  net::NetworkResult send_keep_alive(const protocol::ServerboundKeepAlive& keep_alive);
  net::NetworkResult send_chat_message(const protocol::ServerboundChatMessage& message);

  net::NetworkResult send_chunk_interest_update(
      const protocol::ServerboundChunkInterestUpdate& update);

  net::NetworkResult send_player_move(const protocol::ServerboundPlayerMove& move);
  net::NetworkResult send_vehicle_move(const protocol::ServerboundVehicleMove& move);
  net::NetworkResult send_ride_entity_request(
      const protocol::ServerboundRideEntityRequest& request);
  net::NetworkResult send_dismount_request(
      const protocol::ServerboundDismountRequest& request);
  net::NetworkResult send_vehicle_action(
      const protocol::ServerboundVehicleAction& action);

  net::NetworkResult send_inventory_action(
      protocol::ServerboundInventoryActionRequest request);
  net::NetworkResult send_craft_recipe(
      const protocol::ServerboundCraftRecipe& request);
  net::NetworkResult send_open_container_block(
      const protocol::ServerboundOpenContainerBlock& request);
  net::NetworkResult send_container_action(
      const protocol::ServerboundContainerAction& action);
  net::NetworkResult send_close_container(
      const protocol::ServerboundCloseContainer& request);
  net::NetworkResult send_attack_entity(
      const protocol::ServerboundAttackEntity& attack);

  net::NetworkResult send_skin_request(const protocol::ServerboundSkinRequest& request);
  net::NetworkResult send_skin_upload_begin(
      const protocol::ServerboundSkinUploadBegin& begin);
  net::NetworkResult send_skin_upload_data(
      const protocol::ServerboundSkinUploadData& data);
  net::NetworkResult send_skin_upload_chunk(
      const protocol::ServerboundSkinUploadChunk& chunk);
  net::NetworkResult send_skin_upload_end(
      const protocol::ServerboundSkinUploadEnd& end);
  net::NetworkResult send_pack_sync_status(
      const protocol::ServerboundPackSyncStatus& status);
  net::NetworkResult send_pack_request(
      const protocol::ServerboundPackRequest& request);
  net::NetworkResult send_pack_ready(
      const protocol::ServerboundPackReady& ready);

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
