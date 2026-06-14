#include "arv/client/gameplay_interactions.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"
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
