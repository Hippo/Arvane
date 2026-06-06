#pragma once

#include "arv/client/chunk_interest.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/time.hpp"
#include "arv/physics/collision.hpp"

#include "client_chunk_inbox.hpp"
#include "client_connection.hpp"
#include "client_dropped_item_mirror.hpp"
#include "client_entity_mirror.hpp"
#include "client_input_runtime.hpp"
#include "client_inventory_runtime.hpp"
#include "client_network_messages.hpp"
#include "client_protocol_send.hpp"
#include "client_skin_runtime.hpp"
#include "client_ui_state.hpp"

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::entity {
class VehicleDefinitionRegistry;
} // namespace arv::entity

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
  ClientInputRuntime& input;
  ClientEntityMirrorStore& entities;
  ClientDroppedItemMirrorStore& dropped_items;
  ClientSkinRuntime& skins;
  ParticleSystem& particles;
  const arv::text::LanguageCatalog* language{};
  const arv::entity::VehicleDefinitionRegistry* vehicle_registry{};
  const physics::ICollisionQuery* collision_query{};
  Seconds tick_seconds{};
};

} // namespace arv::client::detail
