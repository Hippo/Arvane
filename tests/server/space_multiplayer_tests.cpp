#include "arv/base/base_commands.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/item.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/world/space_definition.hpp"
#include "support/temp_dir.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct MultiClientEndpoint {
  arv::net::ConnectionID id{};
  arv::net::ConnectionState state{arv::net::ConnectionState::Disconnected};
  std::deque<arv::net::NetworkEvent> inbound{};
};

struct MultiClientSharedState {
  bool listening{};
  std::deque<arv::net::NetworkEvent> server_inbound{};
  std::vector<MultiClientEndpoint> clients{};
};

class MultiClientInProcessTransport final : public arv::net::ITransport {
public:
  enum class Side {
    Server,
    Client,
  };

  MultiClientInProcessTransport(
      std::shared_ptr<MultiClientSharedState> shared,
      Side side,
      std::optional<arv::usize> client_index = std::nullopt)
      : shared_(std::move(shared)), side_(side), client_index_(client_index) {
  }

  static std::unique_ptr<arv::net::ITransport> make_server(
      std::shared_ptr<MultiClientSharedState> shared) {
    return std::make_unique<MultiClientInProcessTransport>(
        std::move(shared),
        Side::Server);
  }

  static std::unique_ptr<arv::net::ITransport> make_client(
      std::shared_ptr<MultiClientSharedState> shared) {
    const arv::usize index = shared->clients.size();
    MultiClientEndpoint endpoint;
    endpoint.id = arv::net::ConnectionID{
        static_cast<arv::net::ConnectionID::value_type>(index + 1U)};
    shared->clients.push_back(std::move(endpoint));
    return std::make_unique<MultiClientInProcessTransport>(
        std::move(shared),
        Side::Client,
        index);
  }

  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    if (side_ != Side::Server) {
      return arv::net::NetworkResult::failure(
          arv::net::NetworkError{"only the server endpoint can listen"});
    }
    shared_->listening = true;
    return arv::net::NetworkResult::success();
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    if (side_ != Side::Client || !client_index_.has_value()) {
      return arv::net::ConnectResult::failure(
          arv::net::NetworkError{"only a client endpoint can connect"});
    }
    if (!shared_->listening) {
      return arv::net::ConnectResult::failure(
          arv::net::NetworkError{"in-process server is not listening"});
    }
    MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
    endpoint.state = arv::net::ConnectionState::TransportConnected;
    shared_->server_inbound.push_back(
        arv::net::NetworkEvent{
            arv::net::NetworkEventType::ConnectionAccepted,
            endpoint.id,
            arv::protocol::ReliabilityClass::ReliableOrdered,
            {},
            {},
            std::chrono::steady_clock::now()});
    return arv::net::ConnectResult::success(endpoint.id);
  }

  arv::net::NetworkResult send(arv::net::TransportMessage message) override {
    if (side_ == Side::Client) {
      if (!client_index_.has_value()) {
        return arv::net::NetworkResult::failure(
            arv::net::NetworkError{"client endpoint is missing"});
      }
      MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
      if (!connection_is_active(endpoint.state)) {
        return arv::net::NetworkResult::failure(
            arv::net::NetworkError{"connection is not active"});
      }
      message.connection_id = endpoint.id;
      shared_->server_inbound.push_back(make_received_event(std::move(message)));
      return arv::net::NetworkResult::success();
    }

    MultiClientEndpoint* endpoint = find_client(message.connection_id);
    if (endpoint == nullptr || !connection_is_active(endpoint->state)) {
      return arv::net::NetworkResult::failure(
          arv::net::NetworkError{"connection is not active"});
    }
    endpoint->inbound.push_back(make_received_event(std::move(message)));
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID connection_id,
      std::string_view reason = {}) override {
    if (side_ == Side::Client) {
      if (!client_index_.has_value()) {
        return arv::net::NetworkResult::success();
      }
      MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
      const bool was_active = connection_is_active(endpoint.state);
      endpoint.state = arv::net::ConnectionState::Disconnected;
      if (was_active) {
        shared_->server_inbound.push_back(
            make_closed_event(endpoint.id, std::string{reason}));
      }
      return arv::net::NetworkResult::success();
    }

    MultiClientEndpoint* endpoint = find_client(connection_id);
    if (endpoint != nullptr) {
      const bool was_active = connection_is_active(endpoint->state);
      endpoint->state = arv::net::ConnectionState::Disconnected;
      if (was_active) {
        endpoint->inbound.push_back(
            make_closed_event(connection_id, std::string{reason}));
      }
    }
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    std::vector<arv::net::NetworkEvent> events;
    events.reserve(8U);
    if (side_ == Side::Server) {
      while (!shared_->server_inbound.empty()) {
        arv::net::NetworkEvent event = std::move(shared_->server_inbound.front());
        shared_->server_inbound.pop_front();
        if (event.type == arv::net::NetworkEventType::ConnectionAccepted) {
          if (MultiClientEndpoint* endpoint = find_client(event.connection_id);
              endpoint != nullptr) {
            endpoint->inbound.push_back(
                arv::net::NetworkEvent{
                    arv::net::NetworkEventType::ConnectionAccepted,
                    endpoint->id,
                    arv::protocol::ReliabilityClass::ReliableOrdered,
                    {},
                    {},
                    std::chrono::steady_clock::now()});
          }
        }
        events.push_back(std::move(event));
      }
      return events;
    }

    if (!client_index_.has_value()) {
      return events;
    }
    MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
    while (!endpoint.inbound.empty()) {
      events.push_back(std::move(endpoint.inbound.front()));
      endpoint.inbound.pop_front();
    }
    return events;
  }

  arv::net::ConnectionState state(arv::net::ConnectionID connection_id) const override {
    if (side_ == Side::Client) {
      if (!client_index_.has_value()) {
        return arv::net::ConnectionState::Disconnected;
      }
      return shared_->clients[*client_index_].state;
    }
    const MultiClientEndpoint* endpoint = find_client(connection_id);
    return endpoint != nullptr ? endpoint->state
                               : arv::net::ConnectionState::Disconnected;
  }

  void shutdown() override {
    if (side_ == Side::Server) {
      shared_->listening = false;
      shared_->server_inbound.clear();
      for (MultiClientEndpoint& endpoint : shared_->clients) {
        endpoint.state = arv::net::ConnectionState::Disconnected;
        endpoint.inbound.clear();
      }
      return;
    }
    if (client_index_.has_value()) {
      MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
      endpoint.state = arv::net::ConnectionState::Disconnected;
      endpoint.inbound.clear();
    }
  }

private:
  MultiClientEndpoint* find_client(arv::net::ConnectionID connection_id) noexcept {
    auto it = std::find_if(
        shared_->clients.begin(),
        shared_->clients.end(),
        [connection_id](const MultiClientEndpoint& endpoint) {
          return endpoint.id == connection_id;
        });
    return it != shared_->clients.end() ? &*it : nullptr;
  }

  const MultiClientEndpoint* find_client(
      arv::net::ConnectionID connection_id) const noexcept {
    auto it = std::find_if(
        shared_->clients.begin(),
        shared_->clients.end(),
        [connection_id](const MultiClientEndpoint& endpoint) {
          return endpoint.id == connection_id;
        });
    return it != shared_->clients.end() ? &*it : nullptr;
  }

  static arv::net::NetworkEvent make_received_event(
      arv::net::TransportMessage message) {
    arv::net::NetworkEvent event{
        arv::net::NetworkEventType::MessageReceived,
        message.connection_id,
        message.reliability,
        std::move(message.payload),
        {},
        std::chrono::steady_clock::now()};
    event.message_type = message.message_type;
    event.lane = message.lane;
    return event;
  }

  static arv::net::NetworkEvent make_closed_event(
      arv::net::ConnectionID connection_id,
      std::string reason) {
    return arv::net::NetworkEvent{
        arv::net::NetworkEventType::ConnectionClosed,
        connection_id,
        arv::protocol::ReliabilityClass::ReliableOrdered,
        {},
        std::move(reason),
        std::chrono::steady_clock::now()};
  }

  static bool connection_is_active(arv::net::ConnectionState state) noexcept {
    return state != arv::net::ConnectionState::Disconnected &&
           state != arv::net::ConnectionState::Disconnecting;
  }

  std::shared_ptr<MultiClientSharedState> shared_;
  Side side_{Side::Server};
  std::optional<arv::usize> client_index_{};
};

arv::entity::VehicleDefinitionRegistry make_space_test_vehicle_registry() {
  arv::entity::VehicleDefinition rocket;
  rocket.name = "arvane:test_rocket";
  rocket.kind = arv::entity::VehicleKind::Vehicle;
  rocket.physics = arv::entity::VehiclePhysicsKind::Aircraft;
  rocket.tags = {"arvane:vehicle", "arvane:rocket", "arvane:space_capable"};
  rocket.collision_size = {1.2F, 3.0F, 1.2F};
  arv::entity::VehicleSeatDefinition pilot;
  pilot.id = "pilot";
  pilot.role = arv::entity::SeatRole::Driver;
  pilot.local_position = {0.0F, 1.45F, 0.0F};
  pilot.local_rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  pilot.controls_vehicle = true;
  pilot.camera_mode = arv::entity::CameraMode::VehicleChase;
  rocket.seats.push_back(std::move(pilot));
  arv::entity::VehicleSeatDefinition passenger;
  passenger.id = "passenger";
  passenger.role = arv::entity::SeatRole::Passenger;
  passenger.local_position = {0.0F, 0.75F, 0.0F};
  passenger.local_rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  passenger.controls_vehicle = false;
  passenger.camera_mode = arv::entity::CameraMode::VehicleChase;
  rocket.seats.push_back(std::move(passenger));
  rocket.aircraft = {
      0.0F,
      58.0F,
      30.0F,
      18.0F,
      0.975F,
      arv::degrees_to_radians(68.0F),
      arv::degrees_to_radians(44.0F),
      arv::degrees_to_radians(70.0F),
      arv::degrees_to_radians(45.0F),
      36.0F,
      8.0F,
      1.4F,
      0.8F,
      30.0F,
      32.0F,
      8.0F,
      0.90F};
  rocket.space_aircraft = arv::entity::AircraftMovementDefinition{
      0.0F,
      95.0F,
      24.0F,
      12.0F,
      0.997F,
      arv::degrees_to_radians(95.0F),
      arv::degrees_to_radians(95.0F),
      arv::degrees_to_radians(120.0F),
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      95.0F,
      95.0F,
      0.0F,
      0.997F};
  rocket.fuel = arv::entity::VehicleFuelDefinition{
      "arvane:rocket_fuel/basic",
      120.0F,
      60.0F,
      1.0F,
      10.0F};
  rocket.authority.max_speed = 120.0F;
  rocket.authority.max_acceleration = 80.0F;
  rocket.authority.max_deceleration = 80.0F;
  rocket.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  rocket.authority.max_vertical_speed = 120.0F;

  auto result = arv::entity::VehicleDefinitionRegistry::build({std::move(rocket)});
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

arv::entity::EntityRegistry make_space_test_entity_registry() {
  arv::entity::EntityDefinition cow;
  cow.name = "arvane:cow";
  cow.spawnable = true;
  cow.persistent_by_default = true;
  cow.replicated_by_default = true;
  auto result = arv::base::build_base_entity_registry({std::move(cow)});
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

arv::world::SpaceDefinitionRegistry make_space_test_definitions() {
  arv::world::SpaceDefinitionRegistry registry;

  arv::world::EnvironmentDefinition overworld_env;
  overworld_env.id = "arvane:environment/overworld";
  overworld_env.gravity_scale = 1.0F;
  overworld_env.has_atmosphere = true;
  overworld_env.has_oxygen = true;
  overworld_env.sky = "overworld";
  registry.environments.emplace(overworld_env.id, std::move(overworld_env));

  arv::world::EnvironmentDefinition deep_space;
  deep_space.id = "arvane:environment/deep_space";
  deep_space.gravity_scale = 0.0F;
  deep_space.has_atmosphere = false;
  deep_space.has_oxygen = false;
  deep_space.sky = "space";
  registry.environments.emplace(deep_space.id, std::move(deep_space));

  arv::world::WorldDefinition default_world;
  default_world.id = "arvane:world/default";
  default_world.kind = arv::world::WorldDefinitionKind::Planet;
  default_world.environment = "arvane:environment/overworld";
  default_world.worldgen = "arvane:worldgen/dimension/overworld";
  default_world.rules.allow_vehicle_launch = true;
  arv::world::SpaceExitDefinition exit;
  exit.enabled = true;
  exit.target_space_world = "arvane:space/solar_system";
  exit.min_y = 20;
  exit.min_vertical_speed = 5.0F;
  exit.require_vehicle_tags = {"arvane:space_capable"};
  exit.consume_fuel = true;
  exit.transition_spawn.position = {100.0F, 40.0F, 100.0F};
  exit.transition_spawn.velocity = {0.0F, 1.0F, 0.0F};
  exit.transition_spawn.velocity_scale = 0.25F;
  default_world.space_exit = std::move(exit);
  registry.worlds.emplace(default_world.id, std::move(default_world));

  arv::world::WorldDefinition solar_system;
  solar_system.id = "arvane:space/solar_system";
  solar_system.kind = arv::world::WorldDefinitionKind::Space;
  solar_system.environment = "arvane:environment/deep_space";
  solar_system.worldgen = std::string{arv::world::VoidWorldGenerationID};
  solar_system.rules.allow_space_entry = true;
  solar_system.rules.allow_block_placement = false;
  arv::world::SpaceWorldDefinition space;
  space.bodies.push_back("arvane:space_body/luna");
  solar_system.space = std::move(space);
  registry.worlds.emplace(solar_system.id, std::move(solar_system));

  arv::world::WorldDefinition luna_world;
  luna_world.id = "arvane:world/luna";
  luna_world.kind = arv::world::WorldDefinitionKind::Planet;
  luna_world.environment = "arvane:environment/overworld";
  luna_world.worldgen = "arvane:worldgen/dimension/overworld";
  registry.worlds.emplace(luna_world.id, std::move(luna_world));

  arv::world::SpaceBodyDefinition luna;
  luna.id = "arvane:space_body/luna";
  luna.type = arv::world::SpaceBodyType::Moon;
  luna.display_name = "Luna";
  luna.space_world = "arvane:space/solar_system";
  luna.target_world = "arvane:world/luna";
  luna.position = {2000.0F, 0.0F, 0.0F};
  luna.visual_radius = 80.0F;
  luna.entry_radius = 120.0F;
  luna.entry.enabled = true;
  luna.entry.mode = arv::world::SpaceBodyEntryMode::FixedSpawn;
  luna.entry.require_vehicle_tags = {"arvane:space_capable"};
  luna.entry.spawn_position = {0.0F, 300.0F, 0.0F};
  luna.entry.spawn_velocity = {0.0F, -8.0F, 0.0F};
  registry.space_bodies.emplace(luna.id, std::move(luna));

  return registry;
}

arv::EntityID create_space_test_rocket(
    arv::world::WorldRuntime& world,
    const arv::entity::VehicleDefinitionRegistry& vehicles,
    arv::Vec3f position) {
  arv::entity::EntityComponentSet components;
  components.collision =
      arv::entity::AuthoredCollisionComponent{{1.2F, 3.0F, 1.2F}};
  components.health = arv::entity::AuthoredHealthComponent{60.0F};
  components.rideable =
      arv::entity::AuthoredRideableComponent{"arvane:test_rocket"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:test_rocket"};

  arv::world::EntityCreateInfo info;
  info.type_id = arv::EntityTypeID{42};
  info.type_name = "arvane:test_rocket";
  info.position = position;
  info.authored_components = components;
  info.vehicle_definitions = &vehicles;
  return world.create_entity(std::move(info));
}

struct SpaceMultiplayerFixture {
  arv::testsupport::ScopedTempDir temp{"arvane_space_multiplayer_"};
  arv::voxel::BlockRegistry block_registry{
      arv::testsupport::make_test_block_registry()};
  arv::player::ItemRegistry item_registry{
      arv::testsupport::make_test_item_registry(block_registry)};
  arv::entity::EntityRegistry entity_registry{make_space_test_entity_registry()};
  arv::entity::VehicleDefinitionRegistry vehicle_registry{
      make_space_test_vehicle_registry()};
  arv::world::SpaceDefinitionRegistry space_definitions{
      make_space_test_definitions()};
  std::shared_ptr<MultiClientSharedState> shared{
      std::make_shared<MultiClientSharedState>()};
  arv::server::ServerRuntime server{
      MultiClientInProcessTransport::make_server(shared)};
  arv::client::ClientRuntime alice{
      MultiClientInProcessTransport::make_client(shared)};
  arv::client::ClientRuntime bob{
      MultiClientInProcessTransport::make_client(shared)};

  SpaceMultiplayerFixture() {
    arv::server::ServerStartInfo start_info;
    start_info.mode = arv::server::ServerMode::Integrated;
    start_info.base_tps = arv::DefaultBaseTps;
    start_info.listen_address = {"in-process", 0};
    start_info.block_registry = &block_registry;
    start_info.item_registry = &item_registry;
    start_info.entity_registry = &entity_registry;
    start_info.vehicle_registry = &vehicle_registry;
    start_info.space_definitions = &space_definitions;
    start_info.register_commands = arv::base::register_server_commands;
    start_info.world_name = "default";
    start_info.server_save_root = temp.path / "save";
    EXPECT_TRUE(server.start(start_info));

    alice.set_local_identity(arv::client::make_ephemeral_local_identity("Alice"));
    bob.set_local_identity(arv::client::make_ephemeral_local_identity("Bob"));
    alice.configure_vehicles(&vehicle_registry);
    bob.configure_vehicles(&vehicle_registry);
    EXPECT_TRUE(alice.connect_integrated());
    EXPECT_TRUE(bob.connect_integrated());
    pump_until_ingame(alice, bob);
  }

  void tick_all() {
    server.tick();
    alice.fixed_tick(server.tick_index());
    bob.fixed_tick(server.tick_index());
  }

  void pump(arv::u32 ticks = 1U) {
    for (arv::u32 i = 0; i < ticks; ++i) {
      tick_all();
    }
  }

  void pump_until_ingame(
      arv::client::ClientRuntime& lhs,
      arv::client::ClientRuntime& rhs,
      arv::u32 max_ticks = 20U) {
    for (arv::u32 i = 0; i < max_ticks; ++i) {
      if (lhs.connection_state() == arv::net::ConnectionState::InGame &&
          rhs.connection_state() == arv::net::ConnectionState::InGame) {
        return;
      }
      tick_all();
    }
  }

  const arv::server::ServerSession* session(std::string_view name) const {
    for (const arv::SessionID session_id : server.sessions().session_ids()) {
      const arv::server::ServerSession* found = server.sessions().find(session_id);
      if (found != nullptr && found->display_name == name) {
        return found;
      }
    }
    return nullptr;
  }

  arv::player::PlayerState* player(std::string_view name) {
    const arv::server::ServerSession* found = session(name);
    if (found == nullptr) {
      return nullptr;
    }
    arv::world::WorldRuntime* world = server.world_runtime(found->world_id);
    return world == nullptr ? nullptr : world->find_player(found->player_id);
  }

  std::optional<arv::EntityID> player_entity(std::string_view name) {
    const arv::server::ServerSession* found = session(name);
    if (found == nullptr) {
      return std::nullopt;
    }
    arv::world::WorldRuntime* world = server.world_runtime(found->world_id);
    return world == nullptr ? std::nullopt : world->entity_for_player(found->player_id);
  }

  arv::EntityID mount_player_on_rocket(
      std::string_view player_name,
      arv::EntityID rocket,
      std::string_view seat_id) {
    arv::player::PlayerState* state = player(player_name);
    EXPECT_NE(state, nullptr);
    std::optional<arv::EntityID> entity = player_entity(player_name);
    EXPECT_TRUE(entity.has_value());
    if (state == nullptr || !entity.has_value()) {
      return {};
    }
    arv::world::WorldRuntime* world = server.world_runtime(state->world_id);
    EXPECT_NE(world, nullptr);
    if (world == nullptr) {
      return {};
    }
    state->position = {0.0F, 21.0F, 0.0F};
    (void)world->set_entity_position(entity.value(), state->position);
    EXPECT_TRUE(world->mount_entity(rocket, entity.value(), std::string{seat_id}));
    return entity.value();
  }

  arv::EntityID create_launch_ready_rocket(arv::Vec3f position) {
    arv::world::WorldRuntime& world = server.voxel_world();
    const arv::EntityID rocket =
        create_space_test_rocket(world, vehicle_registry, position);
    EXPECT_NE(rocket, (arv::EntityID{}));
    arv::entity::VehiclePhysicsComponent* physics =
        world.find_entity_vehicle_physics(rocket);
    EXPECT_NE(physics, nullptr);
    if (physics != nullptr) {
      physics->fuel_seconds = 30.0F;
      physics->fuel_capacity_seconds = 120.0F;
      physics->fuel_burn_per_second = 1.0F;
      physics->linear_velocity = {0.0F, 6.0F, 0.0F};
    }
    if (arv::entity::EntityVelocity* velocity =
            world.find_entity_velocity(rocket);
        velocity != nullptr) {
      velocity->linear = {0.0F, 6.0F, 0.0F};
    }
    return rocket;
  }
};

bool mirrors_only_reference_camera_world(const arv::client::ClientRuntime& client) {
  const arv::WorldID world_id = client.camera().world_id();
  for (const arv::client::ReplicatedEntityMirror& mirror :
       client.replicated_entity_mirrors()) {
    if (mirror.world_id != world_id) {
      return false;
    }
  }
  return true;
}

TEST(SpaceMultiplayerTests, OnePlayerLaunchesAlone) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  fixture.pump(3U);

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  const arv::server::ServerSession* bob_session = fixture.session("Bob");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_NE(bob_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(bob_session->world_id, arv::server::DefaultWorldID);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  EXPECT_TRUE(space_world->entity_exists(target_rocket.value()));
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
  EXPECT_EQ(fixture.bob.camera().world_id(), arv::server::DefaultWorldID);
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.alice));
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.bob));
}

TEST(SpaceMultiplayerTests, TwoPlayersInOneRocketLaunchTogether) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  const arv::EntityID bob_entity =
      fixture.mount_player_on_rocket("Bob", rocket, "passenger");
  ASSERT_NE(alice_entity, (arv::EntityID{}));
  ASSERT_NE(bob_entity, (arv::EntityID{}));

  fixture.pump_until_ingame(fixture.alice, fixture.bob, 1U);
  fixture.pump(3U);

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  const arv::server::ServerSession* bob_session = fixture.session("Bob");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_NE(bob_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(bob_session->world_id, space_record->id);

  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  const std::optional<arv::EntityID> target_bob =
      space_world->entity_for_player(bob_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  ASSERT_TRUE(target_bob.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  EXPECT_EQ(
      space_world->mounted_rideable(target_bob.value()),
      target_rocket);
  EXPECT_EQ(space_world->mounted_seat(target_alice.value()), "pilot");
  EXPECT_EQ(space_world->mounted_seat(target_bob.value()), "passenger");
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
  EXPECT_EQ(fixture.bob.camera().world_id(), space_record->id);
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.alice));
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.bob));
}

TEST(SpaceMultiplayerTests, TwoRocketsLaunchIntoSameSpaceWorldWithoutMixingSeats) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID alice_rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID bob_rocket =
      fixture.create_launch_ready_rocket({6.0F, 21.0F, 0.0F});
  fixture.mount_player_on_rocket("Alice", alice_rocket, "pilot");
  fixture.mount_player_on_rocket("Bob", bob_rocket, "pilot");

  fixture.pump(3U);

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  const arv::server::ServerSession* bob_session = fixture.session("Bob");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_NE(bob_session, nullptr);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  const std::optional<arv::EntityID> target_bob =
      space_world->entity_for_player(bob_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  ASSERT_TRUE(target_bob.has_value());
  const std::optional<arv::EntityID> target_alice_rocket =
      space_world->mounted_rideable(target_alice.value());
  const std::optional<arv::EntityID> target_bob_rocket =
      space_world->mounted_rideable(target_bob.value());
  ASSERT_TRUE(target_alice_rocket.has_value());
  ASSERT_TRUE(target_bob_rocket.has_value());
  EXPECT_NE(target_alice_rocket.value(), target_bob_rocket.value());
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
  EXPECT_EQ(fixture.bob.camera().world_id(), space_record->id);
}

TEST(SpaceMultiplayerTests, PlanetPlayerDoesNotSeeSpacePlayerWorldState) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  fixture.mount_player_on_rocket("Alice", rocket, "pilot");

  fixture.pump(4U);

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  const arv::server::ServerSession* bob_session = fixture.session("Bob");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_NE(bob_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(bob_session->world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
  EXPECT_EQ(fixture.bob.camera().world_id(), arv::server::DefaultWorldID);
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.alice));
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.bob));
  EXPECT_NE(fixture.server.voxel_world().find_player(bob_session->player_id), nullptr);
  EXPECT_EQ(fixture.server.voxel_world().find_player(alice_session->player_id), nullptr);
}

TEST(SpaceMultiplayerTests, ReconnectWhileInSpaceRestoresSpaceWorld) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  fixture.pump(4U);
  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);

  const arv::client::LocalPlayerIdentity identity = fixture.alice.local_identity();
  fixture.alice.disconnect();
  fixture.pump(4U);

  arv::client::ClientRuntime reconnected{
      MultiClientInProcessTransport::make_client(fixture.shared)};
  reconnected.set_local_identity(identity);
  reconnected.configure_vehicles(&fixture.vehicle_registry);
  ASSERT_TRUE(reconnected.connect_integrated());
  for (int i = 0; i < 20 &&
                  reconnected.connection_state() != arv::net::ConnectionState::InGame;
       ++i) {
    fixture.server.tick();
    fixture.bob.fixed_tick(fixture.server.tick_index());
    reconnected.fixed_tick(fixture.server.tick_index());
  }

  ASSERT_EQ(reconnected.connection_state(), arv::net::ConnectionState::InGame);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(reconnected.camera().world_id(), space_record->id);
}

TEST(SpaceMultiplayerTests, ReconnectImmediatelyAfterTransitionRestoresSpaceWorld) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  fixture.pump(1U);
  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  const arv::client::LocalPlayerIdentity identity = fixture.alice.local_identity();

  fixture.alice.disconnect();
  fixture.pump(2U);

  arv::client::ClientRuntime reconnected{
      MultiClientInProcessTransport::make_client(fixture.shared)};
  reconnected.set_local_identity(identity);
  reconnected.configure_vehicles(&fixture.vehicle_registry);
  ASSERT_TRUE(reconnected.connect_integrated());
  for (int i = 0; i < 20 &&
                  reconnected.connection_state() != arv::net::ConnectionState::InGame;
       ++i) {
    fixture.server.tick();
    fixture.bob.fixed_tick(fixture.server.tick_index());
    reconnected.fixed_tick(fixture.server.tick_index());
  }

  ASSERT_EQ(reconnected.connection_state(), arv::net::ConnectionState::InGame);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(reconnected.camera().world_id(), space_record->id);
}

TEST(SpaceMultiplayerTests, PassengerDisconnectAfterTransitionDoesNotDropPilotOrRocket) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  fixture.mount_player_on_rocket("Bob", rocket, "passenger");
  fixture.pump(4U);
  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);

  fixture.bob.disconnect();
  fixture.pump(4U);

  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> alice_entity =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(alice_entity.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(alice_entity.value());
  ASSERT_TRUE(target_rocket.has_value());
  EXPECT_TRUE(space_world->entity_exists(target_rocket.value()));
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
}

TEST(SpaceMultiplayerTests, PlayerOnlyAtLaunchThresholdCannotForceTransition) {
  SpaceMultiplayerFixture fixture;
  arv::player::PlayerState* alice = fixture.player("Alice");
  ASSERT_NE(alice, nullptr);
  alice->position = {0.0F, 128.0F, 0.0F};
  alice->velocity = {0.0F, 500.0F, 0.0F};
  const std::optional<arv::EntityID> alice_entity = fixture.player_entity("Alice");
  ASSERT_TRUE(alice_entity.has_value());
  ASSERT_TRUE(fixture.server.voxel_world().set_entity_position(
      alice_entity.value(),
      alice->position));

  fixture.pump(3U);

  EXPECT_EQ(fixture.server.worlds().find("solar_system"), nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, arv::server::DefaultWorldID);
  EXPECT_EQ(fixture.alice.camera().world_id(), arv::server::DefaultWorldID);
}

} // namespace
