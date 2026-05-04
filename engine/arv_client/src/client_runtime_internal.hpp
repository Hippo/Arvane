#pragma once

#include "arv/client/chunk_interest.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/local_player_controller.hpp"

#include "client_chunk_inbox.hpp"
#include "client_connection.hpp"
#include "client_dropped_item_mirror.hpp"
#include "client_entity_mirror.hpp"
#include "client_input_runtime.hpp"
#include "client_inventory_runtime.hpp"
#include "client_network_messages.hpp"
#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

namespace arv::client::detail {

struct ClientRuntimeContext {
  ClientConnectionRuntime& connection;
  ClientProtocolSender& sender;
  LocalPlayerIdentity& identity;

  LocalPlayerController& player_controller;
  ClientChunkInterest& chunk_interest;

  ClientUiState& ui;
  ClientChunkInbox& chunks;
  ClientInventoryRuntime& inventory;
  ClientEntityMirrorStore& entities;
  ClientDroppedItemMirrorStore& dropped_items;
};

} // namespace arv::client::detail
