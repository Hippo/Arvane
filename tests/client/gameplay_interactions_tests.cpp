#include "arv/client/gameplay_interactions.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/net_events.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::entity::EntityRegistry make_interaction_entity_registry() {
  arv::entity::EntityDefinition mob;
  mob.name = "arvane:test_mob";
  mob.components.collision = arv::entity::AuthoredCollisionComponent{{1.0F, 1.0F, 1.0F}};

  arv::entity::EntityDefinition mount;
  mount.name = "arvane:test_mount";
  mount.components.collision = arv::entity::AuthoredCollisionComponent{{1.0F, 1.0F, 1.0F}};
  mount.components.rideable =
      arv::entity::AuthoredRideableComponent{"arvane:test_mount"};

  arv::entity::EntityDefinition vehicle_only;
  vehicle_only.name = "arvane:test_vehicle_only";
  vehicle_only.components.collision =
      arv::entity::AuthoredCollisionComponent{{1.0F, 1.0F, 1.0F}};

  auto result = arv::entity::EntityRegistry::build(
      {std::move(mob), std::move(mount), std::move(vehicle_only)});
  EXPECT_TRUE(result);
  return std::move(result.value());
}

[[nodiscard]] arv::entity::VehicleDefinitionRegistry make_interaction_vehicle_registry() {
  arv::entity::VehicleDefinition vehicle;
  vehicle.name = "arvane:test_vehicle_only";
  vehicle.collision_size = {1.0F, 1.0F, 1.0F};
  vehicle.seats.push_back(
      {"driver",
       "Driver",
       arv::entity::SeatRole::Driver,
       {},
       {},
       true,
       arv::entity::CameraMode::ThirdPersonFollow});
  vehicle.authority.max_speed = 8.0F;
  vehicle.authority.max_acceleration = 12.0F;
  vehicle.authority.max_deceleration = 12.0F;
  vehicle.authority.max_angular_speed = arv::degrees_to_radians(90.0F);
  vehicle.authority.max_vertical_speed = 6.0F;
  vehicle.authority.max_step_height = 0.6F;

  auto result = arv::entity::VehicleDefinitionRegistry::build({std::move(vehicle)});
  EXPECT_TRUE(result);
  return std::move(result.value());
}

[[nodiscard]] arv::voxel::BlockRegistry make_container_interaction_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition chest;
  chest.name = "arvane:chest";
  chest.display_name = "Chest";
  chest.properties.solid = true;
  chest.properties.collidable = true;
  chest.properties.full_cube = true;
  chest.properties.hardness = 1.0F;
  chest.render_category = arv::voxel::RenderCategory::Opaque;
  chest.render_mode = arv::voxel::BlockRenderMode::BlockEntityModel;
  chest.container_type = "arvane:chest";
  chest.texture_assets.fill("arvane:texture/block/chest");
  definitions.push_back(std::move(chest));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

void send_to_client(
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::protocol::MessageType message_type,
    std::vector<arv::u8> payload) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  (void)server_transport.send(
      {connection_id, arv::protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

void enter_in_game(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id) {
  (void)server_transport.poll_events();

  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(hello));

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{1};
  join.local_entity_id = arv::EntityNetID{1};
  join.spawn_position = {0.0F, 0.0F, 0.0F};
  join.initial_tick = 1;
  join.tick_rate = arv::DefaultBaseTps;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));

  client.fixed_tick(1);
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
}

void spawn_entity(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::EntityNetID entity_id,
    arv::EntityTypeID entity_type,
    std::string entity_type_name,
    arv::Vec3f position) {
  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = entity_id;
  spawn.entity_type = entity_type;
  spawn.entity_type_name = std::move(entity_type_name);
  spawn.position = position;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(2);
  ASSERT_NE(client.find_replicated_entity(entity_id), nullptr);
}

void aim_forward(arv::client::ClientRuntime& client) {
  client.camera().set_position({0.5F, 1.5F, 0.5F});
  client.camera().set_rotation(0.0F, 0.0F);
}

void send_inventory_update(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    std::vector<std::optional<arv::player::ItemStack>> slots,
    arv::u32 revision = 2U) {
  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = revision;
  update.selected_hotbar_slot = 0;
  update.slots = std::move(slots);
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
  client.fixed_tick(revision + 10U);
  ASSERT_TRUE(client.has_authoritative_inventory());
}

void send_modify_world_permission(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    bool can_modify_world) {
  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "test";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_modify_world = can_modify_world;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundPlayerPermissions,
      arv::protocol::serialize_clientbound_player_permissions(permissions));
  client.fixed_tick(can_modify_world ? 40U : 41U);
}

[[nodiscard]] std::optional<arv::protocol::DecodedMessage> first_serverbound_message(
    const std::vector<arv::net::NetworkEvent>& events,
    arv::protocol::MessageType type) {
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded && decoded.value().envelope.message_type == type) {
      return decoded.value();
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<arv::protocol::MessageType> serverbound_message_types(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::MessageType> types;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded) {
      types.push_back(decoded.value().envelope.message_type);
    }
  }
  return types;
}

void mark_local_player_mounted(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::EntityNetID rideable_id) {
  arv::protocol::ClientboundRideStarted ride;
  ride.passenger_id = arv::EntityNetID{1};
  ride.rideable_id = rideable_id;
  ride.seat_id = "driver";
  ride.controls_vehicle = true;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundRideStarted,
      arv::protocol::serialize_clientbound_ride_started(ride));
  client.fixed_tick(90);
  ASSERT_TRUE(arv::client::local_player_is_mounted(client));
}

} // namespace

TEST(ClientGameplayInteractionsTests, AttackTargetingUsesEntityBoundsAndBlockOcclusion) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::EntityTypeID mob_type = entities.find_type("arvane:test_mob").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{5},
      mob_type,
      "arvane:test_mob",
      {0.5F, 1.0F, 4.0F});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  arv::world::WorldCollisionQuery clear_query(world, blocks, false);
  EXPECT_EQ(
      arv::client::targeted_attack_entity(
          {client, clear_query, blocks, entities, vehicles},
          nullptr,
          5.0F),
      (arv::EntityNetID{5}));

  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{0, 1, 2}}, stone));
  arv::world::WorldCollisionQuery occluded_query(world, blocks, false);
  EXPECT_FALSE(
      arv::client::targeted_attack_entity(
          {client, occluded_query, blocks, entities, vehicles},
          nullptr,
          5.0F)
          .has_value());
}

TEST(ClientGameplayInteractionsTests, RideTargetingSkipsMobsAndAcceptsVehicleDefinitions) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::EntityTypeID mob_type = entities.find_type("arvane:test_mob").value();
  const arv::EntityTypeID vehicle_type =
      entities.find_type("arvane:test_vehicle_only").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{7},
      mob_type,
      "arvane:test_mob",
      {0.5F, 1.0F, 2.0F});
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{8},
      vehicle_type,
      "arvane:test_vehicle_only",
      {0.5F, 1.0F, 4.0F});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  arv::world::WorldCollisionQuery query(world, blocks, false);
  EXPECT_EQ(
      arv::client::targeted_rideable_entity(
          {client, query, blocks, entities, vehicles},
          5.0F),
      (arv::EntityNetID{8}));
}

TEST(ClientGameplayInteractionsTests, RequestHelpersSendAttackAndRidePackets) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::EntityTypeID mob_type = entities.find_type("arvane:test_mob").value();
  const arv::EntityTypeID vehicle_type =
      entities.find_type("arvane:test_vehicle_only").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{9},
      mob_type,
      "arvane:test_mob",
      {0.5F, 1.0F, 3.0F});
  aim_forward(client);
  (void)server_transport->poll_events();

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  arv::world::WorldCollisionQuery query(world, blocks, false);
  ASSERT_TRUE(arv::client::request_attack_entity_if_targeted(
      client,
      query,
      blocks,
      entities,
      vehicles,
      nullptr,
      5.0F));

  std::vector<arv::net::NetworkEvent> server_events =
      server_transport->poll_events();
  const arv::net::NetworkEvent* attack_message =
      arv::testsupport::find_event(server_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(attack_message, nullptr);
  const auto attack_envelope = arv::protocol::deserialize_envelope(attack_message->payload);
  ASSERT_TRUE(attack_envelope);
  EXPECT_EQ(
      attack_envelope.value().envelope.message_type,
      arv::protocol::MessageType::ServerboundAttackEntity);
  const auto attack = arv::protocol::deserialize_serverbound_attack_entity(
      attack_envelope.value().payload);
  ASSERT_TRUE(attack);
  EXPECT_EQ(attack.value().target_entity, (arv::EntityNetID{9}));

  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{10},
      vehicle_type,
      "arvane:test_vehicle_only",
      {0.5F, 1.0F, 3.0F});
  aim_forward(client);
  (void)server_transport->poll_events();
  ASSERT_TRUE(arv::client::request_ride_entity_if_targeted(
      client,
      query,
      blocks,
      entities,
      vehicles,
      5.0F));

  server_events = server_transport->poll_events();
  const auto ride_message = first_serverbound_message(
      server_events,
      arv::protocol::MessageType::ServerboundRideEntityRequest);
  ASSERT_TRUE(ride_message.has_value());
  const auto request = arv::protocol::deserialize_serverbound_ride_entity_request(
      ride_message->payload);
  ASSERT_TRUE(request);
  EXPECT_EQ(request.value().rideable_id, (arv::EntityNetID{10}));
}

TEST(ClientGameplayInteractionsTests, RequestHelperSendsOpenContainerBlockPacket) {
  const arv::voxel::BlockRegistry blocks =
      make_container_interaction_block_registry();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::BlockPos target_block{{0, 1, 2}};
  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block(target_block, stone));
  arv::world::WorldCollisionQuery query(world, blocks, false);

  EXPECT_FALSE(
      arv::client::targeted_container_block({client, query, blocks, 5.0F})
          .has_value());

  const arv::voxel::BlockVariantID chest =
      blocks.find_variant("arvane:chest").value();
  ASSERT_TRUE(world.set_block(target_block, chest));
  const std::optional<arv::BlockPos> targeted_container =
      arv::client::targeted_container_block({client, query, blocks, 5.0F});
  ASSERT_TRUE(targeted_container.has_value());
  EXPECT_EQ(targeted_container.value(), target_block);

  (void)server_transport->poll_events();
  ASSERT_TRUE(arv::client::request_open_container_block_if_targeted(
      client,
      query,
      blocks,
      5.0F));

  const std::vector<arv::net::NetworkEvent> server_events =
      server_transport->poll_events();
  const auto open_message = first_serverbound_message(
      server_events,
      arv::protocol::MessageType::ServerboundOpenContainerBlock);
  ASSERT_TRUE(open_message.has_value());
  const auto request = arv::protocol::deserialize_serverbound_open_container_block(
      open_message->payload);
  ASSERT_TRUE(request);
  EXPECT_EQ(request.value().world_id, (arv::WorldID{1}));
  EXPECT_EQ(request.value().block, target_block);
}

TEST(ClientGameplayInteractionsTests, DispatchOrdersContainerAttackRideRequests) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks =
      make_container_interaction_block_registry();
  const arv::EntityTypeID vehicle_type =
      entities.find_type("arvane:test_vehicle_only").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{10},
      vehicle_type,
      "arvane:test_vehicle_only",
      {0.5F, 1.0F, 1.3F});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::BlockPos container_block{{0, 1, 3}};
  const arv::voxel::BlockVariantID chest =
      blocks.find_variant("arvane:chest").value();
  ASSERT_TRUE(world.set_block(container_block, chest));
  arv::world::WorldCollisionQuery query(world, blocks, false);
  (void)server_transport->poll_events();

  const arv::client::GameplayInteractionDispatchResult result =
      arv::client::dispatch_gameplay_interactions(
          {.client = client,
           .collision_query = query,
           .block_registry = blocks,
           .entity_registry = entities,
           .vehicle_registry = vehicles,
           .secondary_pressed = true,
           .primary_pressed = true,
           .mount_pressed = true,
           .reach_distance = 5.0F,
           .default_attack_range = 5.0F,
           .ride_range = 5.0F});
  EXPECT_TRUE(result.opened_container);
  EXPECT_TRUE(result.attacked_entity);
  EXPECT_TRUE(result.requested_ride);
  EXPECT_TRUE(result.consume_secondary_use);
  EXPECT_TRUE(result.stop_held_item_animation);

  const std::vector<arv::protocol::MessageType> types =
      serverbound_message_types(server_transport->poll_events());
  ASSERT_EQ(types.size(), 3U);
  EXPECT_EQ(types[0], arv::protocol::MessageType::ServerboundOpenContainerBlock);
  EXPECT_EQ(types[1], arv::protocol::MessageType::ServerboundAttackEntity);
  EXPECT_EQ(types[2], arv::protocol::MessageType::ServerboundRideEntityRequest);
}

TEST(ClientGameplayInteractionsTests, DispatchBlockedByGameplayUiDoesNothing) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks =
      make_container_interaction_block_registry();
  const arv::EntityTypeID vehicle_type =
      entities.find_type("arvane:test_vehicle_only").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{10},
      vehicle_type,
      "arvane:test_vehicle_only",
      {0.5F, 1.0F, 1.3F});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::BlockVariantID chest =
      blocks.find_variant("arvane:chest").value();
  ASSERT_TRUE(world.set_block({{0, 1, 3}}, chest));
  arv::world::WorldCollisionQuery query(world, blocks, false);
  (void)server_transport->poll_events();

  const arv::client::GameplayInteractionDispatchResult result =
      arv::client::dispatch_gameplay_interactions(
          {.client = client,
           .collision_query = query,
           .block_registry = blocks,
           .entity_registry = entities,
           .vehicle_registry = vehicles,
           .gameplay_blocked = true,
           .secondary_pressed = true,
           .primary_pressed = true,
           .mount_pressed = true,
           .reach_distance = 5.0F,
           .default_attack_range = 5.0F,
           .ride_range = 5.0F});
  EXPECT_FALSE(result.opened_container);
  EXPECT_FALSE(result.attacked_entity);
  EXPECT_FALSE(result.requested_ride);
  EXPECT_FALSE(result.consume_secondary_use);
  EXPECT_FALSE(result.stop_held_item_animation);
  EXPECT_TRUE(serverbound_message_types(server_transport->poll_events()).empty());
}

TEST(ClientGameplayInteractionsTests, DispatchContainerMissDoesNotConsumeSecondary) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks =
      make_container_interaction_block_registry();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{0, 1, 2}}, stone));
  arv::world::WorldCollisionQuery query(world, blocks, false);
  (void)server_transport->poll_events();

  const arv::client::GameplayInteractionDispatchResult result =
      arv::client::dispatch_gameplay_interactions(
          {.client = client,
           .collision_query = query,
           .block_registry = blocks,
           .entity_registry = entities,
           .vehicle_registry = vehicles,
           .secondary_pressed = true,
           .reach_distance = 5.0F});
  EXPECT_FALSE(result.opened_container);
  EXPECT_FALSE(result.consume_secondary_use);
  EXPECT_FALSE(result.stop_held_item_animation);
  EXPECT_TRUE(serverbound_message_types(server_transport->poll_events()).empty());
}

TEST(ClientGameplayInteractionsTests, DispatchRequestsDismountWhenMounted) {
  const arv::entity::EntityRegistry entities = make_interaction_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_interaction_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::EntityTypeID mob_type = entities.find_type("arvane:test_mob").value();
  const arv::EntityTypeID vehicle_type =
      entities.find_type("arvane:test_vehicle_only").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{1},
      mob_type,
      "arvane:test_mob",
      {0.5F, 1.0F, -4.0F});
  spawn_entity(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{10},
      vehicle_type,
      "arvane:test_vehicle_only",
      {0.5F, 1.0F, 1.3F});
  mark_local_player_mounted(
      client,
      *server_transport,
      connection_id.value(),
      arv::EntityNetID{99});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  arv::world::WorldCollisionQuery query(world, blocks, false);
  (void)server_transport->poll_events();

  const arv::client::GameplayInteractionDispatchResult result =
      arv::client::dispatch_gameplay_interactions(
          {.client = client,
           .collision_query = query,
           .block_registry = blocks,
           .entity_registry = entities,
           .vehicle_registry = vehicles,
           .mount_pressed = true,
           .ride_range = 5.0F});
  EXPECT_FALSE(result.requested_ride);
  EXPECT_TRUE(result.requested_dismount);
  EXPECT_FALSE(result.stop_held_item_animation);
  const auto dismount_message = first_serverbound_message(
      server_transport->poll_events(),
      arv::protocol::MessageType::ServerboundDismountRequest);
  ASSERT_TRUE(dismount_message.has_value());
  const auto request = arv::protocol::deserialize_serverbound_dismount_request(
      dismount_message->payload);
  ASSERT_TRUE(request);
  EXPECT_EQ(request.value().rideable_id, (arv::EntityNetID{99}));
}

TEST(ClientGameplayInteractionsTests, HeldItemPlacementPreviewUsesSharedBlockPrediction) {
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry items = arv::testsupport::make_test_item_registry(blocks);
  const arv::player::ItemID stone_item_id =
      items.find_item("arvane:stone").value();
  const arv::player::ItemRecord* stone_item = items.item(stone_item_id);
  ASSERT_NE(stone_item, nullptr);
  ASSERT_TRUE(stone_item->placement_variant.has_value());

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{stone_item_id, 4U}});
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{0, 1, 2}}, stone));
  arv::world::WorldCollisionQuery query(world, blocks, false);

  EXPECT_TRUE(arv::client::held_item_placement_preview_succeeds(
      {.client = client,
       .collision_query = query,
       .block_registry = blocks,
       .item_registry = items},
      stone_item));

  send_modify_world_permission(
      client,
      *server_transport,
      connection_id.value(),
      false);
  EXPECT_FALSE(arv::client::held_item_placement_preview_succeeds(
      {.client = client,
       .collision_query = query,
       .block_registry = blocks,
       .item_registry = items},
      stone_item));
}

TEST(ClientGameplayInteractionsTests, VisibleHeldItemStackRequiresVisibleGameplayState) {
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry items = arv::testsupport::make_test_item_registry(blocks);
  const arv::player::ItemID stone_item_id =
      items.find_item("arvane:stone").value();

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  EXPECT_FALSE(arv::client::visible_held_item_stack(client).has_value());

  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {std::nullopt},
      2U);
  EXPECT_FALSE(arv::client::visible_held_item_stack(client).has_value());

  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{stone_item_id, 4U}},
      3U);
  const std::optional<arv::player::ItemStack> visible =
      arv::client::visible_held_item_stack(client);
  ASSERT_TRUE(visible.has_value());
  EXPECT_EQ(visible->item, stone_item_id);
  EXPECT_EQ(visible->count, 4U);

  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  EXPECT_TRUE(client.inventory_open());
  EXPECT_TRUE(arv::client::visible_held_item_stack(client).has_value());

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  EXPECT_TRUE(client.chat_open());
  EXPECT_TRUE(arv::client::visible_held_item_stack(client).has_value());
}

TEST(ClientGameplayInteractionsTests, HeldItemPlacementPreviewRejectsPlayerOverlap) {
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry items = arv::testsupport::make_test_item_registry(blocks);
  const arv::player::ItemID stone_item_id =
      items.find_item("arvane:stone").value();
  const arv::player::ItemRecord* stone_item = items.item(stone_item_id);
  ASSERT_NE(stone_item, nullptr);

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{stone_item_id, 1U}});
  client.camera().set_position({0.5F, 0.5F, -1.5F});
  client.camera().set_rotation(0.0F, 0.0F);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{0, 0, 1}}, stone));
  arv::world::WorldCollisionQuery query(world, blocks, false);

  EXPECT_FALSE(arv::client::held_item_placement_preview_succeeds(
      {.client = client,
       .collision_query = query,
       .block_registry = blocks,
       .item_registry = items},
      stone_item));
}

TEST(ClientGameplayInteractionsTests, HeldItemPlacementPreviewChecksWaterBucketCapacityAndFluid) {
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry items = arv::testsupport::make_test_item_registry(blocks);
  const arv::player::ItemID water_bucket_item_id =
      items.find_item("arvane:water_bucket").value();
  const arv::player::ItemID dirt_item_id =
      items.find_item("arvane:dirt").value();
  const arv::player::ItemRecord* water_bucket = items.item(water_bucket_item_id);
  ASSERT_NE(water_bucket, nullptr);

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  aim_forward(client);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::BlockVariantID stone =
      blocks.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{0, 1, 2}}, stone));
  arv::world::WorldCollisionQuery query(world, blocks, false);
  const arv::client::HeldItemPlacementPreviewQuery preview{
      .client = client,
      .collision_query = query,
      .block_registry = blocks,
      .item_registry = items,
      .config = {.empty_bucket_item_name = "arvane:bucket",
                 .water_bucket_item_name = "arvane:water_bucket"}};

  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{water_bucket_item_id, 1U}},
      2U);
  EXPECT_TRUE(arv::client::held_item_placement_preview_succeeds(
      preview,
      water_bucket));

  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{water_bucket_item_id, 2U},
       arv::player::ItemStack{dirt_item_id, 64U}},
      3U);
  EXPECT_FALSE(arv::client::held_item_placement_preview_succeeds(
      preview,
      water_bucket));

  send_inventory_update(
      client,
      *server_transport,
      connection_id.value(),
      {arv::player::ItemStack{water_bucket_item_id, 2U},
       std::nullopt},
      4U);
  ASSERT_TRUE(arv::client::held_item_placement_preview_succeeds(
      preview,
      water_bucket));

  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  ASSERT_TRUE(world.set_fluid(
      {{0, 1, 1}},
      arv::voxel::make_fluid_source(water),
      water));
  EXPECT_FALSE(arv::client::held_item_placement_preview_succeeds(
      preview,
      water_bucket));
}
