#include "arv/base/base_commands.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/item.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/world/space_definition.hpp"
#include "arv/world/world_queries.hpp"
#include "support/temp_dir.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
  bool deliver_gameplay_before_session_messages{};
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
    arv::net::NetworkEvent event = make_received_event(std::move(message));
    if (shared_->deliver_gameplay_before_session_messages &&
        event.lane == arv::protocol::NetworkLane::Gameplay) {
      endpoint->inbound.push_front(std::move(event));
    } else {
      endpoint->inbound.push_back(std::move(event));
    }
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
  pilot.local_position = {0.0F, 3.2F, 0.0F};
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
      90.0F,
      0.0F,
      0.0F,
      0.35F,
      52.0F,
      32.0F,
      0.0F,
      0.90F,
      true};
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
      0.997F,
      true};
  rocket.fuel = arv::entity::VehicleFuelDefinition{
      "arvane:rocket_fuel/basic",
      120.0F,
      60.0F,
      1.0F,
      10.0F};
  rocket.cargo = arv::entity::VehicleCargoDefinition{9};
  rocket.authority.max_speed = 105.0F;
  rocket.authority.max_acceleration = 42.0F;
  rocket.authority.max_deceleration = 54.0F;
  rocket.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  rocket.authority.max_vertical_speed = 100.0F;
  rocket.authority.requires_swept_collision = true;
  rocket.authority.max_unloaded_chunk_entry_distance = 0.0F;

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

  arv::world::EnvironmentDefinition luna_env;
  luna_env.id = "arvane:environment/luna";
  luna_env.gravity_scale = 0.18F;
  luna_env.has_atmosphere = false;
  luna_env.has_oxygen = false;
  luna_env.sky = "space";
  registry.environments.emplace(luna_env.id, std::move(luna_env));

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
  luna_world.environment = "arvane:environment/luna";
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
  luna.entry.spawn_position = {0.0F, 118.0F, 0.0F};
  luna.entry.spawn_velocity = {0.0F, -4.0F, 0.0F};
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

  arv::EntityID create_launch_ready_rocket(
      arv::Vec3f position,
      arv::Vec3f launch_velocity = {0.0F, 6.0F, 0.0F}) {
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
      physics->linear_velocity = launch_velocity;
    }
    if (arv::entity::EntityVelocity* velocity =
            world.find_entity_velocity(rocket);
        velocity != nullptr) {
      velocity->linear = launch_velocity;
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

void expect_client_mirror_mount(
    const arv::client::ClientRuntime& client,
    arv::EntityNetID passenger_net,
    arv::EntityNetID rocket_net) {
  const arv::client::ReplicatedEntityMirror* passenger =
      client.find_replicated_entity(passenger_net);
  ASSERT_NE(passenger, nullptr);
  EXPECT_EQ(passenger->parent_rideable, std::optional<arv::EntityNetID>{rocket_net});
  EXPECT_EQ(passenger->parent_seat_id, "pilot");
  EXPECT_TRUE(passenger->controls_parent_vehicle);

  const arv::client::ReplicatedEntityMirror* rocket =
      client.find_replicated_entity(rocket_net);
  ASSERT_NE(rocket, nullptr);
  const auto mounted_passenger = std::find_if(
      rocket->passengers.begin(),
      rocket->passengers.end(),
      [&](const arv::client::ReplicatedPassengerMirror& entry) {
        return entry.passenger_id == passenger_net;
      });
  ASSERT_NE(mounted_passenger, rocket->passengers.end());
  EXPECT_EQ(mounted_passenger->seat_id, "pilot");
  EXPECT_TRUE(mounted_passenger->controls_vehicle);
}

void drive_space_rocket_and_expect_move(
    SpaceMultiplayerFixture& fixture,
    arv::world::WorldRuntime& space_world,
    arv::EntityID rocket,
    arv::EntityNetID rocket_net,
    arv::u32 ticks = 16U) {
  const arv::entity::EntityTransform* before_transform =
      space_world.find_entity_transform(rocket);
  ASSERT_NE(before_transform, nullptr);
  const arv::Vec3f before_position = before_transform->position;
  for (arv::u32 i = 0; i < ticks; ++i) {
    arv::client::InputFrame input;
    input.move_forward = true;
    if (i == 0U) {
      input.mouse_delta_y = -120.0F;
    }
    fixture.alice.submit_input(input);
    fixture.pump(1U);
  }
  const arv::entity::EntityTransform* after_transform =
      space_world.find_entity_transform(rocket);
  ASSERT_NE(after_transform, nullptr);
  EXPECT_GT(
      arv::distance_squared(after_transform->position, before_position),
      0.0001F);
  const std::optional<arv::server::VehicleCrashDebugInfo> move_debug =
      fixture.server.last_vehicle_crash_debug(rocket_net);
  ASSERT_TRUE(move_debug.has_value());
  EXPECT_TRUE(move_debug->accepted_move);
  EXPECT_FALSE(move_debug->sent_correction);
  EXPECT_EQ(
      move_debug->validation_reason,
      arv::protocol::VehicleCorrectionReason::Unknown);
}

TEST(SpaceMultiplayerTests, OnePlayerLaunchesAlone) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
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
  EXPECT_EQ(space_world->loaded_chunk_count(), 0U);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  EXPECT_TRUE(space_world->entity_exists(target_rocket.value()));
  const std::optional<arv::EntityNetID> target_rocket_net =
      space_world->network_id_for_entity(target_rocket.value());
  ASSERT_TRUE(target_rocket_net.has_value());
  EXPECT_EQ(fixture.alice.mounted_vehicle_id(), target_rocket_net);
  EXPECT_EQ(std::string{fixture.alice.mounted_seat_id()}, "pilot");
  EXPECT_TRUE(fixture.alice.mounted_controls_vehicle());
  EXPECT_TRUE(fixture.alice.space_environment());
  EXPECT_EQ(fixture.alice.world_environment_sky(), "space");
  EXPECT_EQ(fixture.alice.camera().world_id(), space_record->id);
  EXPECT_EQ(fixture.bob.camera().world_id(), arv::server::DefaultWorldID);
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.alice));
  EXPECT_TRUE(mirrors_only_reference_camera_world(fixture.bob));

  const arv::entity::EntityTransform* before_transform =
      space_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(before_transform, nullptr);
  const arv::Vec3f before_position = before_transform->position;
  arv::world::WorldRuntime empty_client_space{space_record->id};
  const arv::world::WorldCollisionQuery missing_chunks_are_solid{
      empty_client_space,
      fixture.block_registry};
  for (arv::u32 i = 0; i < 12U; ++i) {
    arv::client::InputFrame input;
    input.move_forward = true;
    if (i == 0U) {
      input.mouse_delta_y = -120.0F;
    }
    fixture.alice.submit_input(input);
    fixture.server.tick();
    fixture.alice.fixed_tick(
        fixture.server.tick_index(),
        &missing_chunks_are_solid);
    fixture.bob.fixed_tick(fixture.server.tick_index());
  }
  const arv::entity::EntityTransform* after_transform =
      space_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(after_transform, nullptr);
  EXPECT_EQ(space_world->loaded_chunk_count(), 0U);
  EXPECT_GT(
      arv::distance_squared(after_transform->position, before_position),
      0.0001F);
  const std::optional<arv::server::VehicleCrashDebugInfo> move_debug =
      fixture.server.last_vehicle_crash_debug(target_rocket_net.value());
  ASSERT_TRUE(move_debug.has_value());
  EXPECT_TRUE(move_debug->accepted_move);
  EXPECT_FALSE(move_debug->sent_correction);
  EXPECT_EQ(
      move_debug->validation_reason,
      arv::protocol::VehicleCorrectionReason::Unknown);
}

TEST(SpaceMultiplayerTests, SpaceLaunchSurvivesGameplayLaneBeforeSessionLane) {
  SpaceMultiplayerFixture fixture;
  fixture.shared->deliver_gameplay_before_session_messages = true;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id &&
        fixture.alice.connection_state() == arv::net::ConnectionState::InGame) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_EQ(alice_session->world_id, space_record->id);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  const std::optional<arv::EntityNetID> target_rocket_net =
      space_world->network_id_for_entity(target_rocket.value());
  ASSERT_TRUE(target_rocket_net.has_value());
  EXPECT_EQ(fixture.alice.mounted_vehicle_id(), target_rocket_net);
  EXPECT_TRUE(fixture.alice.mounted_controls_vehicle());
  ASSERT_NE(fixture.alice.find_replicated_entity(target_rocket_net.value()), nullptr);

  const arv::entity::EntityTransform* before_transform =
      space_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(before_transform, nullptr);
  const arv::Vec3f before_position = before_transform->position;
  arv::world::WorldRuntime empty_client_space{space_record->id};
  const arv::world::WorldCollisionQuery missing_chunks_are_solid{
      empty_client_space,
      fixture.block_registry};
  for (arv::u32 i = 0; i < 12U; ++i) {
    fixture.alice.submit_input(arv::client::InputFrame{.move_forward = true});
    fixture.server.tick();
    fixture.alice.fixed_tick(
        fixture.server.tick_index(),
        &missing_chunks_are_solid);
    fixture.bob.fixed_tick(fixture.server.tick_index());
  }
  const arv::entity::EntityTransform* after_transform =
      space_world->find_entity_transform(target_rocket.value());
  ASSERT_NE(after_transform, nullptr);
  EXPECT_GT(
      arv::distance_squared(after_transform->position, before_position),
      0.0001F);
  const std::optional<arv::server::VehicleCrashDebugInfo> move_debug =
      fixture.server.last_vehicle_crash_debug(target_rocket_net.value());
  ASSERT_TRUE(move_debug.has_value());
  EXPECT_TRUE(move_debug->accepted_move);
  EXPECT_FALSE(move_debug->sent_correction);
}

TEST(SpaceMultiplayerTests, SpaceLaunchWithLiveUpwardVelocityDoesNotCorrectionLoop) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket(
          {2.0F, 21.0F, 0.0F},
          {0.0F, 24.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id &&
        fixture.alice.connection_state() == arv::net::ConnectionState::InGame) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_EQ(alice_session->world_id, space_record->id);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  const std::optional<arv::EntityNetID> target_rocket_net =
      space_world->network_id_for_entity(target_rocket.value());
  ASSERT_TRUE(target_rocket_net.has_value());
  ASSERT_EQ(fixture.alice.mounted_vehicle_id(), target_rocket_net);
  ASSERT_TRUE(fixture.alice.mounted_controls_vehicle());

  drive_space_rocket_and_expect_move(
      fixture,
      *space_world,
      target_rocket.value(),
      target_rocket_net.value());
}

TEST(SpaceMultiplayerTests, SpaceLaunchRocketCanDismountRemountAndMoveAgain) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id &&
        fixture.alice.connection_state() == arv::net::ConnectionState::InGame) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_EQ(alice_session->world_id, space_record->id);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const std::optional<arv::EntityID> target_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(target_alice.has_value());
  const std::optional<arv::EntityID> target_rocket =
      space_world->mounted_rideable(target_alice.value());
  ASSERT_TRUE(target_rocket.has_value());
  const std::optional<arv::EntityNetID> target_alice_net =
      space_world->network_id_for_entity(target_alice.value());
  const std::optional<arv::EntityNetID> target_rocket_net =
      space_world->network_id_for_entity(target_rocket.value());
  ASSERT_TRUE(target_alice_net.has_value());
  ASSERT_TRUE(target_rocket_net.has_value());
  ASSERT_EQ(fixture.alice.local_entity_id(), target_alice_net);
  EXPECT_EQ(fixture.alice.mounted_vehicle_id(), target_rocket_net);
  EXPECT_TRUE(fixture.alice.mounted_controls_vehicle());
  expect_client_mirror_mount(
      fixture.alice,
      target_alice_net.value(),
      target_rocket_net.value());

  drive_space_rocket_and_expect_move(
      fixture,
      *space_world,
      target_rocket.value(),
      target_rocket_net.value());

  ASSERT_TRUE(fixture.alice.request_dismount());
  for (arv::u32 i = 0; i < 40U; ++i) {
    fixture.pump(1U);
    if (!fixture.alice.mounted_vehicle_id().has_value() &&
        !space_world->mounted_rideable(target_alice.value()).has_value()) {
      break;
    }
  }
  EXPECT_FALSE(fixture.alice.mounted_vehicle_id().has_value());
  EXPECT_FALSE(space_world->mounted_rideable(target_alice.value()).has_value());
  const arv::client::ReplicatedEntityMirror* dismounted_passenger =
      fixture.alice.find_replicated_entity(target_alice_net.value());
  ASSERT_NE(dismounted_passenger, nullptr);
  EXPECT_FALSE(dismounted_passenger->parent_rideable.has_value());

  ASSERT_TRUE(
      fixture.alice.request_ride_entity(target_rocket_net.value(), "pilot"));
  for (arv::u32 i = 0; i < 40U; ++i) {
    fixture.pump(1U);
    if (fixture.alice.mounted_vehicle_id() == target_rocket_net &&
        space_world->mounted_rideable(target_alice.value()) == target_rocket) {
      break;
    }
  }
  EXPECT_EQ(fixture.alice.mounted_vehicle_id(), target_rocket_net);
  EXPECT_TRUE(fixture.alice.mounted_controls_vehicle());
  EXPECT_EQ(
      space_world->mounted_rideable(target_alice.value()),
      target_rocket);
  expect_client_mirror_mount(
      fixture.alice,
      target_alice_net.value(),
      target_rocket_net.value());

  drive_space_rocket_and_expect_move(
      fixture,
      *space_world,
      target_rocket.value(),
      target_rocket_net.value());
}

TEST(SpaceMultiplayerTests, SpaceLaunchStartsAsyncLoadBeforeTransition) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));
  const arv::entity::EntityTransform* before_transform =
      fixture.server.voxel_world().find_entity_transform(rocket);
  ASSERT_NE(before_transform, nullptr);
  const arv::f32 before_y = before_transform->position.y;

  fixture.pump(1U);

  const arv::server::WorldRecord* loading_space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(loading_space_record, nullptr);
  EXPECT_EQ(
      loading_space_record->state,
      arv::server::WorldRuntimeState::Loading);
  EXPECT_EQ(
      fixture.server.world_runtime(loading_space_record->id),
      nullptr);
  const arv::server::ServerSession* loading_alice_session =
      fixture.session("Alice");
  ASSERT_NE(loading_alice_session, nullptr);
  EXPECT_EQ(loading_alice_session->world_id, arv::server::DefaultWorldID);
  EXPECT_TRUE(fixture.server.voxel_world().entity_exists(rocket));
  const arv::entity::EntityTransform* loading_transform =
      fixture.server.voxel_world().find_entity_transform(rocket);
  ASSERT_NE(loading_transform, nullptr);
  EXPECT_GE(loading_transform->position.y, before_y);

  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    const arv::server::WorldRecord* space_record =
        fixture.server.worlds().find("solar_system");
    ASSERT_NE(alice_session, nullptr);
    ASSERT_NE(space_record, nullptr);
    if (alice_session->world_id == space_record->id) {
      break;
    }
  }

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(space_record, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, space_record->id);
  EXPECT_EQ(space_record->state, arv::server::WorldRuntimeState::Running);
  EXPECT_FALSE(space_record->load_on_start);
  EXPECT_NE(fixture.server.world_runtime(space_record->id), nullptr);
}

TEST(SpaceMultiplayerTests, UnmountedRocketSpawnedInSpaceReplicatesWithoutChunks) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  ASSERT_EQ(space_world->loaded_chunk_count(), 0U);

  const arv::EntityID spawned_rocket =
      create_space_test_rocket(
          *space_world,
          fixture.vehicle_registry,
          {120.0F, 42.0F, 100.0F});
  ASSERT_NE(spawned_rocket, (arv::EntityID{}));
  const std::optional<arv::EntityNetID> spawned_net =
      space_world->network_id_for_entity(spawned_rocket);
  ASSERT_TRUE(spawned_net.has_value());

  fixture.pump(3U);

  const arv::client::ReplicatedEntityMirror* mirror =
      fixture.alice.find_replicated_entity(spawned_net.value());
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->world_id, space_record->id);
  EXPECT_EQ(mirror->entity_type_name, "arvane:test_rocket");
  EXPECT_EQ(space_world->loaded_chunk_count(), 0U);
}

TEST(SpaceMultiplayerTests, UnmountedPlayerCanMoveInVoidSpaceWorld) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_EQ(alice_session->world_id, space_record->id);
  const std::optional<arv::EntityID> space_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(space_alice.has_value());
  ASSERT_TRUE(space_world->mounted_rideable(space_alice.value()).has_value());

  ASSERT_TRUE(fixture.alice.request_dismount());
  fixture.pump(3U);
  EXPECT_FALSE(fixture.alice.mounted_vehicle_id().has_value());
  EXPECT_FALSE(space_world->mounted_rideable(space_alice.value()).has_value());

  const arv::player::PlayerState* before = fixture.player("Alice");
  ASSERT_NE(before, nullptr);
  const arv::Vec3f before_position = before->position;
  for (arv::u32 i = 0; i < 10U; ++i) {
    fixture.alice.submit_input(arv::client::InputFrame{
        .move_forward = true,
        .jump_held = true});
    fixture.pump(1U);
  }

  const arv::player::PlayerState* after = fixture.player("Alice");
  ASSERT_NE(after, nullptr);
  EXPECT_GT(arv::distance_squared(after->position, before_position), 0.0001F);
  EXPECT_GT(after->position.y, before_position.y);
}

TEST(SpaceMultiplayerTests, LaunchedRocketCanEnterMoonAndStayMounted) {
  SpaceMultiplayerFixture fixture;
  const arv::EntityID rocket =
      fixture.create_launch_ready_rocket({2.0F, 21.0F, 0.0F});
  const arv::player::ItemID stone_item =
      fixture.item_registry.find_item("arvane:stone").value();
  const arv::voxel::BlockVariantID stone_block =
      fixture.block_registry.find_variant("arvane:stone").value();
  arv::entity::VehicleCargoComponent* launch_cargo =
      fixture.server.voxel_world().find_entity_vehicle_cargo(rocket);
  ASSERT_NE(launch_cargo, nullptr);
  ASSERT_TRUE(launch_cargo->inventory.set_slot(
      4,
      arv::player::ItemStack{stone_item, 11}));
  const arv::EntityID alice_entity =
      fixture.mount_player_on_rocket("Alice", rocket, "pilot");
  ASSERT_NE(alice_entity, (arv::EntityID{}));

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  arv::world::WorldRuntime* space_world =
      fixture.server.world_runtime(space_record->id);
  ASSERT_NE(space_world, nullptr);
  const arv::server::ServerSession* alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  ASSERT_EQ(alice_session->world_id, space_record->id);
  const std::optional<arv::EntityID> space_alice =
      space_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(space_alice.has_value());
  const std::optional<arv::EntityID> space_rocket =
      space_world->mounted_rideable(space_alice.value());
  ASSERT_TRUE(space_rocket.has_value());
  const arv::entity::VehicleCargoComponent* space_cargo =
      space_world->find_entity_vehicle_cargo(space_rocket.value());
  ASSERT_NE(space_cargo, nullptr);
  ASSERT_TRUE(space_cargo->inventory.slot(4).has_value());
  EXPECT_EQ(space_cargo->inventory.slot(4)->item, stone_item);
  EXPECT_EQ(space_cargo->inventory.slot(4)->count, 11U);
  ASSERT_EQ(std::string{fixture.alice.mounted_seat_id()}, "pilot");
  ASSERT_TRUE(fixture.alice.mounted_controls_vehicle());

  for (arv::u32 i = 0; i < 6U; ++i) {
    arv::client::InputFrame input;
    input.move_forward = true;
    input.jump_held = true;
    fixture.alice.submit_input(input);
    fixture.pump(1U);
  }

  const arv::entity::EntityTransform* space_transform =
      space_world->find_entity_transform(space_rocket.value());
  ASSERT_NE(space_transform, nullptr);
  auto luna_body =
      fixture.space_definitions.space_bodies.find("arvane:space_body/luna");
  ASSERT_NE(luna_body, fixture.space_definitions.space_bodies.end());
  luna_body->second.position = space_transform->position;
  luna_body->second.entry_radius = 500.0F;

  fixture.pump(3U);

  const arv::server::WorldRecord* luna_record =
      fixture.server.worlds().find("luna");
  ASSERT_NE(luna_record, nullptr);
  arv::world::WorldRuntime* luna_world =
      fixture.server.world_runtime(luna_record->id);
  ASSERT_NE(luna_world, nullptr);
  alice_session = fixture.session("Alice");
  ASSERT_NE(alice_session, nullptr);
  EXPECT_EQ(alice_session->world_id, luna_record->id);
  const arv::server::WorldRecord* stopped_space_record =
      fixture.server.worlds().find("solar_system");
  ASSERT_NE(stopped_space_record, nullptr);
  EXPECT_EQ(stopped_space_record->state, arv::server::WorldRuntimeState::Stopped);
  EXPECT_EQ(fixture.server.world_runtime(stopped_space_record->id), nullptr);

  const std::optional<arv::EntityID> luna_alice =
      luna_world->entity_for_player(alice_session->player_id);
  ASSERT_TRUE(luna_alice.has_value());
  const std::optional<arv::EntityID> luna_rocket =
      luna_world->mounted_rideable(luna_alice.value());
  ASSERT_TRUE(luna_rocket.has_value());
  EXPECT_EQ(luna_world->mounted_seat(luna_alice.value()), "pilot");
  const arv::entity::VehicleCargoComponent* luna_cargo =
      luna_world->find_entity_vehicle_cargo(luna_rocket.value());
  ASSERT_NE(luna_cargo, nullptr);
  ASSERT_TRUE(luna_cargo->inventory.slot(4).has_value());
  EXPECT_EQ(luna_cargo->inventory.slot(4)->item, stone_item);
  EXPECT_EQ(luna_cargo->inventory.slot(4)->count, 11U);
  const std::optional<arv::EntityNetID> luna_rocket_net =
      luna_world->network_id_for_entity(luna_rocket.value());
  ASSERT_TRUE(luna_rocket_net.has_value());
  EXPECT_EQ(fixture.alice.camera().world_id(), luna_record->id);
  EXPECT_EQ(fixture.alice.mounted_vehicle_id(), luna_rocket_net);
  EXPECT_EQ(std::string{fixture.alice.mounted_seat_id()}, "pilot");
  EXPECT_TRUE(fixture.alice.mounted_controls_vehicle());

  const arv::entity::EntityTransform* luna_transform =
      luna_world->find_entity_transform(luna_rocket.value());
  ASSERT_NE(luna_transform, nullptr);
  EXPECT_GT(luna_transform->position.y, -64.0F);
  EXPECT_LT(luna_transform->position.y, 128.0F);
  const arv::f32 entry_y = luna_transform->position.y;
  const arv::f32 entry_z = luna_transform->position.z;
  const arv::i32 wall_x =
      static_cast<arv::i32>(std::floor(luna_transform->position.x));
  const arv::i32 wall_y =
      static_cast<arv::i32>(std::floor(luna_transform->position.y));
  const arv::i32 wall_z =
      static_cast<arv::i32>(std::floor(luna_transform->position.z));
  ASSERT_TRUE(luna_world->set_block({{wall_x, wall_y, wall_z + 3}}, stone_block));
  ASSERT_TRUE(luna_world->set_block({{wall_x, wall_y, wall_z - 3}}, stone_block));
  for (arv::u32 i = 0; i < 24U; ++i) {
    fixture.alice.submit_input(arv::client::InputFrame{.move_forward = true});
    fixture.pump(1U);
  }
  luna_transform = luna_world->find_entity_transform(luna_rocket.value());
  ASSERT_NE(luna_transform, nullptr);
  EXPECT_LE(std::abs(luna_transform->position.z - entry_z), 2.401F);

  for (arv::u32 i = 0; i < 10U; ++i) {
    fixture.alice.submit_input(arv::client::InputFrame{.sneak_held = true});
    fixture.pump(1U);
  }
  luna_transform = luna_world->find_entity_transform(luna_rocket.value());
  ASSERT_NE(luna_transform, nullptr);
  EXPECT_LT(luna_transform->position.y, entry_y - 0.01F);
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

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    const arv::server::ServerSession* bob_session = fixture.session("Bob");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        bob_session != nullptr &&
        alice_session->world_id == space_record->id &&
        bob_session->world_id == space_record->id) {
      break;
    }
  }
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

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    const arv::server::ServerSession* bob_session = fixture.session("Bob");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        bob_session != nullptr &&
        alice_session->world_id == space_record->id &&
        bob_session->world_id == space_record->id) {
      break;
    }
  }
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

  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
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
  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        fixture.server.world_runtime(space_record->id) != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);

  const arv::client::LocalPlayerIdentity identity = fixture.alice.local_identity();
  fixture.alice.disconnect();
  fixture.pump(4U);

  arv::client::ClientRuntime reconnected{
      MultiClientInProcessTransport::make_client(fixture.shared)};
  reconnected.set_local_identity(identity);
  reconnected.configure_vehicles(&fixture.vehicle_registry);
  ASSERT_TRUE(reconnected.connect_integrated());
  for (int i = 0; i < 200 &&
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
  const arv::server::WorldRecord* space_record =
      fixture.server.worlds().find("solar_system");
  for (arv::u32 i = 0; i < 200U; ++i) {
    fixture.pump(1U);
    space_record = fixture.server.worlds().find("solar_system");
    const arv::server::ServerSession* alice_session = fixture.session("Alice");
    if (space_record != nullptr &&
        alice_session != nullptr &&
        alice_session->world_id == space_record->id) {
      break;
    }
  }
  ASSERT_NE(space_record, nullptr);
  const arv::client::LocalPlayerIdentity identity = fixture.alice.local_identity();

  fixture.alice.disconnect();
  fixture.pump(2U);

  arv::client::ClientRuntime reconnected{
      MultiClientInProcessTransport::make_client(fixture.shared)};
  reconnected.set_local_identity(identity);
  reconnected.configure_vehicles(&fixture.vehicle_registry);
  ASSERT_TRUE(reconnected.connect_integrated());
  for (int i = 0; i < 200 &&
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
