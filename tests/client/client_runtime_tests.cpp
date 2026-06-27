#include "arv/client/client_runtime.hpp"
#include "arv/client/command_chat.hpp"
#include "arv/command/command.hpp"
#include "arv/core/text_edit.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/particles.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/text_variables.hpp"
#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/text/language.hpp"
#include "arv/text/variables.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/net_events.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using arv::testsupport::find_event;

[[nodiscard]] arv::core::TextEditInput text_edit(
    arv::core::TextEditCommand command,
    bool extend_selection = false,
    std::string clipboard_text = {}) {
  return {command, extend_selection, std::move(clipboard_text)};
}

arv::u32 player_move_count(const std::vector<arv::net::NetworkEvent>& events) {
  arv::u32 count = 0;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded &&
        decoded.value().envelope.message_type ==
            arv::protocol::MessageType::ServerboundPlayerMove) {
      ++count;
    }
  }
  return count;
}

std::vector<arv::protocol::ServerboundPlayerMove> serverbound_player_moves(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundPlayerMove> moves;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundPlayerMove) {
      continue;
    }
    const auto move =
        arv::protocol::deserialize_serverbound_player_move(decoded.value().payload);
    if (move) {
      moves.push_back(move.value());
    }
  }
  return moves;
}

std::vector<arv::protocol::ServerboundVehicleMove> serverbound_vehicle_moves(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundVehicleMove> moves;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundVehicleMove) {
      continue;
    }
    const auto move =
        arv::protocol::deserialize_serverbound_vehicle_move(decoded.value().payload);
    if (move) {
      moves.push_back(move.value());
    }
  }
  return moves;
}

std::vector<arv::protocol::ServerboundDismountRequest> serverbound_dismount_requests(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundDismountRequest> requests;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundDismountRequest) {
      continue;
    }
    const auto request =
        arv::protocol::deserialize_serverbound_dismount_request(decoded.value().payload);
    if (request) {
      requests.push_back(request.value());
    }
  }
  return requests;
}

const arv::protocol::EntityAnimationPlayback* find_mirror_animation(
    const arv::client::ReplicatedEntityMirror& mirror,
    std::string_view name) {
  for (const arv::protocol::EntityAnimationPlayback& animation : mirror.animations) {
    if (animation.name == name) {
      return &animation;
    }
  }
  return nullptr;
}

std::vector<arv::protocol::ServerboundKeepAlive> serverbound_keep_alives(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundKeepAlive> keep_alives;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundKeepAlive) {
      continue;
    }
    const auto keep_alive =
        arv::protocol::deserialize_serverbound_keep_alive(decoded.value().payload);
    if (keep_alive) {
      keep_alives.push_back(keep_alive.value());
    }
  }
  return keep_alives;
}

std::vector<arv::protocol::ServerboundPackSyncStatus> serverbound_pack_sync_statuses(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundPackSyncStatus> statuses;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundPackSyncStatus) {
      continue;
    }
    const auto status =
        arv::protocol::deserialize_serverbound_pack_sync_status(decoded.value().payload);
    if (status) {
      statuses.push_back(status.value());
    }
  }
  return statuses;
}

std::vector<arv::protocol::ServerboundPackReady> serverbound_pack_ready_messages(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundPackReady> ready_messages;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundPackReady) {
      continue;
    }
    const auto ready =
        arv::protocol::deserialize_serverbound_pack_ready(decoded.value().payload);
    if (ready) {
      ready_messages.push_back(ready.value());
    }
  }
  return ready_messages;
}

std::vector<arv::protocol::ServerboundChatMessage> serverbound_chat_messages(
    const std::vector<arv::net::NetworkEvent>& events) {
  std::vector<arv::protocol::ServerboundChatMessage> messages;
  for (const arv::net::NetworkEvent& event : events) {
    if (event.type != arv::net::NetworkEventType::MessageReceived) {
      continue;
    }
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded ||
        decoded.value().envelope.message_type !=
            arv::protocol::MessageType::ServerboundChatMessage) {
      continue;
    }
    const auto chat =
        arv::protocol::deserialize_serverbound_chat_message(decoded.value().payload);
    if (chat) {
      messages.push_back(chat.value());
    }
  }
  return messages;
}

arv::client::ParticleRegistry make_runtime_particle_registry() {
  arv::client::ParticleRegistry registry;
  arv::client::ParticleType type;
  type.name = "arvane:block_dust";
  type.frames.push_back(
      {"arvane:texture/particle/test", {0.0F, 0.0F}, {16.0F, 16.0F}, 0U, 16U, 16U});
  type.count = {1U, 1U};
  type.lifetime_seconds = {1.0F, 1.0F};
  type.size_start = {0.1F, 0.1F};
  type.size_end = {0.1F, 0.1F};
  type.color = {{1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}};
  (void)registry.add(std::move(type));
  registry.add_frame_reference(
      {"arvane:texture/block/dirt", {32.0F, 0.0F}, {48.0F, 16.0F}, 0U, 16U, 16U});
  return registry;
}

arv::entity::VehicleDefinitionRegistry make_client_buggy_registry() {
  arv::entity::VehicleDefinition buggy;
  buggy.name = "arvane:test_buggy";
  buggy.kind = arv::entity::VehicleKind::Vehicle;
  buggy.physics = arv::entity::VehiclePhysicsKind::GroundVehicle;
  buggy.collision_size = {1.8F, 1.2F, 3.2F};
  buggy.seats.push_back({"driver",
                         "driver",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 0.8F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::ThirdPersonFollow});
  buggy.ground = {
      24.0F,
      8.0F,
      18.0F,
      30.0F,
      0.92F,
      arv::degrees_to_radians(80.0F),
      0.85F,
      1.0F,
      0.5F};
  buggy.authority.max_speed = 28.0F;
  buggy.authority.max_acceleration = 26.0F;
  buggy.authority.max_deceleration = 45.0F;
  buggy.authority.max_angular_speed = arv::degrees_to_radians(130.0F);
  buggy.authority.max_vertical_speed = 30.0F;
  buggy.authority.max_step_height = 0.6F;
  buggy.authority.requires_ground = true;
  buggy.authority.requires_water = false;
  auto registry = arv::entity::VehicleDefinitionRegistry::build({std::move(buggy)});
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

arv::entity::VehicleDefinitionRegistry make_client_living_mount_registry() {
  arv::entity::VehicleDefinition horse;
  horse.name = "arvane:test_horse";
  horse.kind = arv::entity::VehicleKind::Mount;
  horse.physics = arv::entity::VehiclePhysicsKind::LivingMount;
  horse.collision_size = {1.4F, 1.6F, 1.4F};
  horse.seats.push_back({"saddle",
                         "saddle",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 1.1F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::MountedThirdPerson});
  horse.living_mount.walk_speed = 4.0F;
  horse.living_mount.sprint_speed = 7.0F;
  horse.living_mount.acceleration = 16.0F;
  horse.living_mount.turn_rate = arv::degrees_to_radians(180.0F);
  horse.living_mount.jump_strength = 7.0F;
  horse.living_mount.gravity_scale = 1.0F;
  horse.living_mount.step_height = 1.0F;
  horse.living_mount.friction = 0.82F;
  horse.authority.max_speed = 8.0F;
  horse.authority.max_acceleration = 16.0F;
  horse.authority.max_deceleration = 22.0F;
  horse.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  horse.authority.max_vertical_speed = 30.0F;
  horse.authority.max_step_height = 1.05F;
  horse.authority.requires_ground = false;
  auto registry = arv::entity::VehicleDefinitionRegistry::build({std::move(horse)});
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

arv::entity::VehicleDefinitionRegistry make_client_air_vehicle_registry() {
  arv::entity::VehicleDefinition plane;
  plane.name = "arvane:test_plane";
  plane.kind = arv::entity::VehicleKind::Vehicle;
  plane.physics = arv::entity::VehiclePhysicsKind::Aircraft;
  plane.tags.push_back("arvane:test_plane/basic");
  plane.collision_size = {4.0F, 1.5F, 3.0625F};
  plane.seats.push_back({"pilot",
                         "pilot",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 0.75F, 0.35F},
                         {},
                         true,
                         arv::entity::CameraMode::VehicleChase});
  plane.aircraft = {
      0.0F,
      42.0F,
      18.0F,
      24.0F,
      0.985F,
      arv::degrees_to_radians(75.0F),
      arv::degrees_to_radians(55.0F),
      arv::degrees_to_radians(110.0F),
      arv::degrees_to_radians(60.0F),
      24.0F,
      9.0F,
      1.8F,
      0.65F,
      18.0F,
      28.0F,
      10.0F,
      0.92F};
  plane.authority.max_speed = 52.0F;
  plane.authority.max_acceleration = 36.0F;
  plane.authority.max_deceleration = 48.0F;
  plane.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  plane.authority.max_vertical_speed = 36.0F;
  plane.authority.requires_swept_collision = true;
  plane.authority.max_unloaded_chunk_entry_distance = 0.0F;

  arv::entity::VehicleDefinition rocket;
  rocket.name = "arvane:test_rocket";
  rocket.kind = arv::entity::VehicleKind::Vehicle;
  rocket.physics = arv::entity::VehiclePhysicsKind::Aircraft;
  rocket.tags = {"arvane:test_rocket/basic", "arvane:rocket", "arvane:space_capable"};
  rocket.collision_size = {1.2F, 3.0F, 1.2F};
  rocket.seats.push_back({"pilot",
                          "pilot",
                          arv::entity::SeatRole::Driver,
                          {0.0F, 3.2F, 0.0F},
                          {},
                          true,
                          arv::entity::CameraMode::VehicleChase});
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
  rocket.authority.max_speed = 105.0F;
  rocket.authority.max_acceleration = 42.0F;
  rocket.authority.max_deceleration = 54.0F;
  rocket.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  rocket.authority.max_vertical_speed = 100.0F;
  rocket.authority.requires_swept_collision = true;
  rocket.authority.max_unloaded_chunk_entry_distance = 0.0F;

  arv::entity::VehicleDefinition submarine;
  submarine.name = "arvane:test_submarine";
  submarine.kind = arv::entity::VehicleKind::Vehicle;
  submarine.physics = arv::entity::VehiclePhysicsKind::Submersible;
  submarine.collision_size = {2.0F, 1.5F, 2.25F};
  submarine.seats.push_back({"pilot",
                             "pilot",
                             arv::entity::SeatRole::Driver,
                             {0.0F, 0.85F, 0.3F},
                             {},
                             true,
                             arv::entity::CameraMode::VehicleChase});
  submarine.submersible = {
      24.0F,
      8.0F,
      28.0F,
      24.0F,
      arv::degrees_to_radians(65.0F),
      arv::degrees_to_radians(35.0F),
      10.0F,
      10.0F,
      0.97F,
      0.45F,
      1.0F,
      0.45F,
      0.75F,
      0.15F};
  submarine.authority.max_speed = 28.0F;
  submarine.authority.max_acceleration = 36.0F;
  submarine.authority.max_deceleration = 36.0F;
  submarine.authority.max_angular_speed = arv::degrees_to_radians(110.0F);
  submarine.authority.max_vertical_speed = 14.0F;
  submarine.authority.min_water_occupancy = 0.35F;

  auto registry =
      arv::entity::VehicleDefinitionRegistry::build(
          {std::move(plane), std::move(rocket), std::move(submarine)});
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

void send_to_client(
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::protocol::MessageType message_type,
    std::vector<arv::u8> payload,
    arv::protocol::ReliabilityClass reliability =
        arv::protocol::ReliabilityClass::ReliableOrdered) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  (void)server_transport.send({connection_id, reliability, std::move(packet)});
}

void enter_in_game(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::Vec3f spawn_position = {8.0F, 20.0F, -20.0F}) {
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
  join.spawn_position = spawn_position;
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

void spawn_and_mount_test_buggy(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::EntityNetID vehicle_id = arv::EntityNetID{10},
    arv::Vec3f position = {0.0F, 1.0F, 0.0F}) {
  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = vehicle_id;
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_buggy";
  spawn.position = position;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundRideStarted ride;
  ride.passenger_id = arv::EntityNetID{1};
  ride.rideable_id = vehicle_id;
  ride.seat_id = "driver";
  ride.controls_vehicle = true;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundRideStarted,
      arv::protocol::serialize_clientbound_ride_started(ride));
  client.fixed_tick(2);
  ASSERT_TRUE(client.find_replicated_entity(vehicle_id) != nullptr);
  (void)server_transport.poll_events();
}

void spawn_and_mount_test_vehicle(
    arv::client::ClientRuntime& client,
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    std::string_view entity_type_name,
    std::string_view seat_id,
    arv::EntityNetID vehicle_id,
    arv::Vec3f position) {
  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = vehicle_id;
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = std::string{entity_type_name};
  spawn.position = position;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundRideStarted ride;
  ride.passenger_id = arv::EntityNetID{1};
  ride.rideable_id = vehicle_id;
  ride.seat_id = std::string{seat_id};
  ride.controls_vehicle = true;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundRideStarted,
      arv::protocol::serialize_clientbound_ride_started(ride));
  client.fixed_tick(2);
  ASSERT_TRUE(client.find_replicated_entity(vehicle_id) != nullptr);
  (void)server_transport.poll_events();
}

class HangingConnectTransport final : public arv::net::ITransport {
public:
  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    return arv::net::NetworkResult::failure({"client test transport cannot listen"});
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    state_ = arv::net::ConnectionState::Connecting;
    return arv::net::ConnectResult::success(arv::net::ConnectionID{7});
  }

  arv::net::NetworkResult send(arv::net::TransportMessage) override {
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID,
      std::string_view reason = {}) override {
    close_reason = std::string{reason};
    state_ = arv::net::ConnectionState::Disconnected;
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    return {};
  }

  arv::net::ConnectionState state(arv::net::ConnectionID) const override {
    return state_;
  }

  void shutdown() override {
    state_ = arv::net::ConnectionState::Disconnected;
  }

  std::string close_reason{};

private:
  arv::net::ConnectionState state_{arv::net::ConnectionState::Disconnected};
};

} // namespace

TEST(ClientRuntimeTests, ChatDraftIgnoresOpeningKeyThenAcceptsTextEditingAndSubmitWithoutLocalEcho) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{
      .open_chat_pressed = true,
      .text_input = "t"});
  EXPECT_TRUE(client.chat_open());
  EXPECT_TRUE(client.chat_draft().empty());

  client.submit_input(arv::client::InputFrame{.text_input = "hello"});
  EXPECT_EQ(client.chat_draft(), "hello");

  client.submit_input(arv::client::InputFrame{.text_backspace_pressed = true});
  EXPECT_EQ(client.chat_draft(), "hell");

  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
  EXPECT_FALSE(client.chat_open());
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, ChatDraftSupportsCursorSelectionClipboardAndPaste) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = "Alpha"});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {
              text_edit(arv::core::TextEditCommand::MoveLeft),
              text_edit(arv::core::TextEditCommand::MoveLeft),
          }});
  client.submit_input(arv::client::InputFrame{.text_input = "X"});
  EXPECT_EQ(client.chat_draft(), "AlpXha");
  EXPECT_EQ(client.chat_draft_cursor(), 4U);

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveLeft, true)}});
  EXPECT_EQ(client.chat_draft_selection_start(), 3U);
  EXPECT_EQ(client.chat_draft_selection_end(), 4U);

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::Copy)}});
  EXPECT_EQ(client.consume_pending_clipboard_text(), std::optional<std::string>{"X"});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::Cut)}});
  EXPECT_EQ(client.chat_draft(), "Alpha");
  EXPECT_EQ(client.consume_pending_clipboard_text(), std::optional<std::string>{"X"});

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {
              text_edit(arv::core::TextEditCommand::SelectAll),
              text_edit(arv::core::TextEditCommand::Paste, false, "World"),
          }});
  EXPECT_EQ(client.chat_draft(), "World");
}

TEST(ClientRuntimeTests, PlayerMenuSearchSupportsCursorSelectionClipboardAndPaste) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  ASSERT_TRUE(client.inventory_open());
  client.set_player_menu_tab(arv::client::PlayerMenuTab::Crafting);
  client.submit_input(arv::client::InputFrame{.text_input = "Alpha"});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {
              text_edit(arv::core::TextEditCommand::MoveLeft),
              text_edit(arv::core::TextEditCommand::MoveLeft),
          }});
  client.submit_input(arv::client::InputFrame{.text_input = "X"});
  EXPECT_EQ(client.player_menu_search(), "AlpXha");
  EXPECT_EQ(client.player_menu_search_cursor(), 4U);

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveLeft, true)}});
  EXPECT_EQ(client.player_menu_search_selection_start(), 3U);
  EXPECT_EQ(client.player_menu_search_selection_end(), 4U);

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::Copy)}});
  EXPECT_EQ(client.consume_pending_clipboard_text(), std::optional<std::string>{"X"});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::Cut)}});
  EXPECT_EQ(client.player_menu_search(), "Alpha");
  EXPECT_EQ(client.consume_pending_clipboard_text(), std::optional<std::string>{"X"});

  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {
              text_edit(arv::core::TextEditCommand::SelectAll),
              text_edit(arv::core::TextEditCommand::Paste, false, "World"),
          }});
  EXPECT_EQ(client.player_menu_search(), "World");
}

TEST(ClientRuntimeTests, PlayerMenuSearchTypingDoesNotToggleInventoryOnBoundKey) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  ASSERT_TRUE(client.inventory_open());
  client.set_player_menu_tab(arv::client::PlayerMenuTab::Crafting);

  client.submit_input(
      arv::client::InputFrame{
          .open_inventory_pressed = true,
          .text_input = "e",
      });

  EXPECT_TRUE(client.inventory_open());
  EXPECT_EQ(client.player_menu_search(), "e");
  EXPECT_EQ(client.player_menu_search_cursor(), 1U);
}

TEST(ClientRuntimeTests, EscapeAndInventoryKeyBackOutOfPlayerMenuSettingsSubpageBeforeClosing) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  ASSERT_TRUE(client.inventory_open());
  client.set_player_menu_tab(arv::client::PlayerMenuTab::Settings);
  client.set_player_menu_settings_section(arv::client::PlayerMenuSettingsSection::Render);

  client.submit_input(arv::client::InputFrame{.text_cancel_pressed = true});
  EXPECT_TRUE(client.inventory_open());
  EXPECT_EQ(
      client.player_menu_settings_section(),
      arv::client::PlayerMenuSettingsSection::Overview);

  client.set_player_menu_settings_section(arv::client::PlayerMenuSettingsSection::KeyBinds);
  client.submit_input(arv::client::InputFrame{.open_inventory_pressed = true});
  EXPECT_TRUE(client.inventory_open());
  EXPECT_EQ(
      client.player_menu_settings_section(),
      arv::client::PlayerMenuSettingsSection::Overview);

  client.submit_input(arv::client::InputFrame{.text_cancel_pressed = true});
  EXPECT_FALSE(client.inventory_open());
}

TEST(ClientRuntimeTests, EscapeOpensInventoryWhenGameplayHasNoOverlay) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(
      arv::client::InputFrame{
          .open_inventory_pressed = true,
          .text_cancel_pressed = true,
      });

  EXPECT_TRUE(client.inventory_open());
}

TEST(ClientRuntimeTests, ChatHistoryNavigationBottomsOutAtUncommittedDraft) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = "one"});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = "two"});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = "draft"});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveUp)}});
  EXPECT_EQ(client.chat_draft(), "two");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveUp)}});
  EXPECT_EQ(client.chat_draft(), "one");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveUp)}});
  EXPECT_EQ(client.chat_draft(), "one");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveDown)}});
  EXPECT_EQ(client.chat_draft(), "two");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveDown)}});
  EXPECT_EQ(client.chat_draft(), "draft");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveDown)}});
  EXPECT_EQ(client.chat_draft(), "draft");

  client.submit_input(arv::client::InputFrame{.text_cancel_pressed = true});
  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveDown)}});
  EXPECT_TRUE(client.chat_draft().empty());
}

TEST(ClientRuntimeTests, CommittedChatCommandAppearsInSubmittedHistoryNavigation) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.text_input = "/"});
  client.submit_input(arv::client::InputFrame{.text_input = "flyspeed 10"});
  const std::optional<std::string> command = client.commit_chat_draft_to_history();
  ASSERT_EQ(command, std::optional<std::string>{"/flyspeed 10"});
  client.close_chat();

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveUp)}});
  EXPECT_EQ(client.chat_draft(), "/flyspeed 10");
  client.submit_input(
      arv::client::InputFrame{
          .text_edit_events = {text_edit(arv::core::TextEditCommand::MoveDown)}});
  EXPECT_TRUE(client.chat_draft().empty());
}

TEST(ClientRuntimeTests, SlashTextOpensChatWithCommandPrefix) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{.text_input = "/"});

  EXPECT_TRUE(client.chat_open());
  EXPECT_EQ(client.chat_draft(), "/");
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, LocalCommandResultParsesAuthoredChatMarkup) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  arv::text::LanguageCatalog language;
  const arv::command::CommandResult result =
      arv::command::CommandResult::permission_denied("<red>No permission");

  arv::client::append_command_result_to_chat(client, result, language);

  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "No permission");
  ASSERT_EQ(client.chat_history().size(), 1U);
  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(client.chat_history().front().message);
  ASSERT_FALSE(runs.empty());
  EXPECT_EQ(runs.back().text, "No permission");
  EXPECT_EQ(runs.back().style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
}

TEST(ClientRuntimeTests, ChatSubmitSendsRawServerboundIntentWhenInGame) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  client.submit_input(arv::client::InputFrame{.open_chat_pressed = true});
  client.submit_input(arv::client::InputFrame{.text_input = " hello "});
  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});

  const std::vector<arv::protocol::ServerboundChatMessage> messages =
      serverbound_chat_messages(server_transport->poll_events());
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message, "hello");
  EXPECT_FALSE(messages.front().channel.has_value());
  EXPECT_TRUE(client.chat_messages().empty());
}

TEST(ClientRuntimeTests, DirectChatSendSendsSlashCommandIntentWhenInGame) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  ASSERT_TRUE(client.send_chat_message("/gmc"));

  const std::vector<arv::protocol::ServerboundChatMessage> messages =
      serverbound_chat_messages(server_transport->poll_events());
  ASSERT_EQ(messages.size(), 1U);
  EXPECT_EQ(messages.front().message, "/gmc");
  EXPECT_FALSE(messages.front().channel.has_value());
}

TEST(ClientRuntimeTests, ClientboundChatAppendsRichHistory) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::text::TextStylePatch style;
  style.color = arv::text::RgbColor{0x20U, 0x80U, 0xFFU};
  arv::protocol::ClientboundChatMessage chat;
  chat.message_id = 9U;
  chat.channel = "arvane:global";
  chat.kind = arv::protocol::ChatMessageKind::Chat;
  chat.message = arv::text::plain_rich_text("server says hi", style);
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChatMessage,
      arv::protocol::serialize_clientbound_chat_message(chat));

  client.fixed_tick(2);

  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "server says hi");
  ASSERT_EQ(client.chat_history().size(), 1U);
  EXPECT_EQ(client.chat_history().front().message_id, 9U);
  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(client.chat_history().front().message);
  ASSERT_EQ(runs.size(), 1U);
  EXPECT_EQ(runs.front().style.color, (arv::text::RgbColor{0x20U, 0x80U, 0xFFU}));
}

TEST(ClientRuntimeTests, TextVariableUpdatesApplyResetRemovalAndPlayerIsolation) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundTextVariableUpdate initial;
  initial.reset = true;
  initial.entries.push_back(
      {arv::protocol::TextVariableScope::Global, "online_player_count", "2"});
  initial.entries.push_back(
      {arv::protocol::TextVariableScope::Player, "player_name", "Hippo"});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundTextVariableUpdate,
      arv::protocol::serialize_clientbound_text_variable_update(initial));
  client.fixed_tick(2);

  EXPECT_EQ(client.text_variables().global("online_player_count"), "2");
  EXPECT_EQ(client.text_variables().player(arv::PlayerID{1}, "player_name"), "Hippo");
  EXPECT_EQ(client.text_variables().player(arv::PlayerID{7}, "player_name"), std::nullopt);
  EXPECT_EQ(
      client.text_variables().global_binding("online_player_count"),
      arv::text::TextVariableBinding::Resolved);

  arv::protocol::ClientboundTextVariableUpdate removal;
  removal.entries.push_back(
      {arv::protocol::TextVariableScope::Global, "online_player_count", std::nullopt});
  removal.entries.push_back(
      {arv::protocol::TextVariableScope::Player, "player_name", std::nullopt});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundTextVariableUpdate,
      arv::protocol::serialize_clientbound_text_variable_update(removal));
  client.fixed_tick(3);

  EXPECT_EQ(client.text_variables().global("online_player_count"), std::nullopt);
  EXPECT_EQ(client.text_variables().player(arv::PlayerID{1}, "player_name"), std::nullopt);

  arv::protocol::ClientboundTextVariableUpdate reset;
  reset.reset = true;
  reset.entries.push_back(
      {arv::protocol::TextVariableScope::Player, "player_health", "18"});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundTextVariableUpdate,
      arv::protocol::serialize_clientbound_text_variable_update(reset));
  client.fixed_tick(4);

  EXPECT_EQ(client.text_variables().player(arv::PlayerID{1}, "player_health"), "18");
  EXPECT_EQ(client.text_variables().player(arv::PlayerID{7}, "player_health"), std::nullopt);
  EXPECT_EQ(client.text_variables().global("online_player_count"), std::nullopt);
}

TEST(ClientRuntimeTests, LiveTextVariablesResolveReceivedChatOnClient) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundTextVariableUpdate variables;
  variables.reset = true;
  variables.entries.push_back(
      {arv::protocol::TextVariableScope::Player,
       "player_countdown",
       "1",
       arv::protocol::TextVariableBinding::Live});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundTextVariableUpdate,
      arv::protocol::serialize_clientbound_text_variable_update(variables));
  client.fixed_tick(2);

  arv::protocol::ClientboundChatMessage chat;
  chat.message_id = 10U;
  chat.channel = "arvane:system";
  chat.kind = arv::protocol::ChatMessageKind::System;
  chat.message = arv::text::plain_rich_text("countdown: {player_countdown}");
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChatMessage,
      arv::protocol::serialize_clientbound_chat_message(chat));
  client.fixed_tick(3);

  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "countdown: 1");
  ASSERT_EQ(client.chat_history().size(), 1U);
  EXPECT_EQ(
      arv::text::to_plain_text(client.chat_history().front().message),
      "countdown: 1");
}

TEST(ClientRuntimeTests, ChatHistoryLimitIsConfigurable) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};
  client.configure_chat({2U});

  client.add_chat_message("one");
  client.add_chat_message("two");
  client.add_chat_message("three");

  ASSERT_EQ(client.chat_history().size(), 2U);
  EXPECT_EQ(arv::text::to_plain_text(client.chat_history()[0].message), "two");
  EXPECT_EQ(arv::text::to_plain_text(client.chat_history()[1].message), "three");
}

TEST(ClientRuntimeTests, WorldJoinKeepsLocalPlayerBodyAtServerSpawn) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);

  enter_in_game(client, *server_transport, connection_id.value(), {80.0F, 40.0F, -96.0F});
  client.fixed_tick(2);

  EXPECT_FLOAT_EQ(client.camera().position().x, 80.0F);
  EXPECT_FLOAT_EQ(
      client.camera().position().y,
      40.0F + arv::player::DefaultStandingEyeHeight);
  EXPECT_FLOAT_EQ(client.camera().position().z, -96.0F);
  ASSERT_TRUE(client.local_player_id().has_value());
  EXPECT_EQ(client.local_player_id().value(), (arv::PlayerID{1}));
  ASSERT_TRUE(client.local_entity_id().has_value());
  EXPECT_EQ(client.local_entity_id().value(), (arv::EntityNetID{1}));
}

TEST(ClientRuntimeTests, EntitySpawnReceivedBeforeWorldJoinSurvivesJoin) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{42};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:remote_player";
  spawn.position = {3.0F, 4.0F, 5.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  spawn.owning_player = arv::PlayerID{2};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(hello));

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{1};
  join.local_entity_id = arv::EntityNetID{1};
  join.spawn_position = {8.0F, 20.0F, -20.0F};
  join.initial_tick = 1;
  join.tick_rate = arv::DefaultBaseTps;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));

  client.fixed_tick(1);
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{42});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->world_id, arv::WorldID{1});
  EXPECT_EQ(mirror->position, (arv::Vec3f{3.0F, 4.0F, 5.0F}));
  ASSERT_TRUE(mirror->owning_player.has_value());
  EXPECT_EQ(mirror->owning_player.value(), arv::PlayerID{2});

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 2;
  snapshot.entities.push_back({arv::EntityNetID{42},
                               {6.0F, 7.0F, 8.0F},
                               {1.0F, 0.0F, 0.0F},
                               {0.0F, 0.0F, 0.0F, 1.0F},
                               0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);

  mirror = client.find_replicated_entity(arv::EntityNetID{42});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{6.0F, 7.0F, 8.0F}));
  EXPECT_EQ(mirror->last_server_tick, 2U);
}

TEST(ClientRuntimeTests, KickPacketDisconnectsAndStoresReason) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundKickPlayer,
      arv::protocol::serialize_clientbound_kick_player({"Duplicate profile."}),
      arv::protocol::ReliabilityClass::ReliableImmediate);

  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Duplicate profile.");
  EXPECT_FALSE(client.local_player_id().has_value());
}

TEST(ClientRuntimeTests, KeepAlivePacketGetsImmediateEcho) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundKeepAlive,
      arv::protocol::serialize_clientbound_keep_alive({42U}),
      arv::protocol::ReliabilityClass::UnreliableEvent);

  client.fixed_tick(2);

  const std::vector<arv::protocol::ServerboundKeepAlive> replies =
      serverbound_keep_alives(server_transport->poll_events());
  ASSERT_EQ(replies.size(), 1U);
  EXPECT_EQ(replies.front().id, 42U);
  EXPECT_FALSE(client.disconnect_reason().has_value());
}

TEST(ClientRuntimeTests, ServerKeepAliveTimeoutDisconnectsClient) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  client.configure_networking({1U, 10U});
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Lost connection to the server.");
}

TEST(ClientRuntimeTests, DisabledServerKeepAliveTimeoutDoesNotDisconnectClient) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  client.configure_networking({1U, 10U, false});
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(2);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_FALSE(client.disconnect_reason().has_value());
}

TEST(ClientRuntimeTests, ConnectionAttemptTimeoutDisconnectsClient) {
  auto transport = std::make_unique<HangingConnectTransport>();
  HangingConnectTransport* raw_transport = transport.get();
  arv::client::ClientRuntime client(std::move(transport));
  client.configure_networking({30U, 1U});
  const auto connection = client.connect({"127.0.0.1", 42069});
  ASSERT_TRUE(connection);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Connecting);

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  client.fixed_tick(1);

  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(client.disconnect_reason().has_value());
  EXPECT_EQ(client.disconnect_reason().value(), "Server is offline or unreachable.");
  EXPECT_EQ(raw_transport->close_reason, "Server is offline or unreachable.");
}

TEST(ClientRuntimeTests, ReplicatedEntitiesIgnoreUnknownAndStaleSnapshots) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySnapshot unknown_snapshot;
  unknown_snapshot.world_id = arv::WorldID{1};
  unknown_snapshot.server_tick = 10;
  unknown_snapshot.entities.push_back({arv::EntityNetID{99},
                                       {1.0F, 2.0F, 3.0F},
                                       {},
                                       {},
                                       0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(unknown_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);
  EXPECT_EQ(client.replicated_entity_count(), 0U);

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {0.0F, 1.0F, 2.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(3);
  ASSERT_EQ(client.replicated_entity_count(), 1U);

  arv::protocol::ClientboundEntitySnapshot fresh_snapshot;
  fresh_snapshot.world_id = arv::WorldID{1};
  fresh_snapshot.server_tick = 20;
  fresh_snapshot.entities.push_back({arv::EntityNetID{5},
                                     {10.0F, 11.0F, 12.0F},
                                     {1.0F, 0.0F, 0.0F},
                                     {0.0F, 0.0F, 0.0F, 1.0F},
                                     0,
                                     {{"walking", 0.5F}, {"hit_right", 0.125F}},
                                     {5.0F, 15.0F, 0.0F},
                                     8.0F,
                                     10.0F});
  fresh_snapshot.entities.back().state_flags =
      arv::protocol::EntityStateFlagHasHealth;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(4);
  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));
  EXPECT_EQ(mirror->last_server_tick, 20U);
  ASSERT_EQ(mirror->animations.size(), 2U);
  EXPECT_EQ(mirror->animations[0].name, "walking");
  EXPECT_FLOAT_EQ(mirror->animations[0].time_seconds, 0.5F);
  EXPECT_EQ(mirror->animations[1].name, "hit_right");
  EXPECT_FLOAT_EQ(mirror->animations[1].time_seconds, 0.125F);
  EXPECT_EQ(mirror->head_rotation_degrees, (arv::Vec3f{5.0F, 15.0F, 0.0F}));
  EXPECT_TRUE(mirror->has_health);
  EXPECT_FALSE(mirror->dead);
  EXPECT_FALSE(mirror->invulnerable);
  EXPECT_FLOAT_EQ(mirror->health_current, 8.0F);
  EXPECT_FLOAT_EQ(mirror->health_max, 10.0F);

  fresh_snapshot.server_tick = 19;
  fresh_snapshot.entities[0].position = {30.0F, 31.0F, 32.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(fresh_snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(5);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntityDespawn,
      arv::protocol::serialize_clientbound_entity_despawn(
          {arv::WorldID{1}, arv::EntityNetID{5}, 0}));
  client.fixed_tick(6);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
}

TEST(ClientRuntimeTests, AttackRequestSendsEntityPacketAndQueuesFeedback) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {3.0F, 1.0F, 2.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(2);
  ASSERT_NE(client.find_replicated_entity(arv::EntityNetID{5}), nullptr);
  (void)server_transport->poll_events();

  const auto result = client.request_attack_entity(arv::EntityNetID{5});
  ASSERT_TRUE(result);

  const std::vector<arv::client::ClientAttackFeedback> feedback =
      client.drain_attack_feedback();
  ASSERT_EQ(feedback.size(), 1U);
  EXPECT_EQ(feedback.front().target_entity, (arv::EntityNetID{5}));
  EXPECT_EQ(feedback.front().target_position, (arv::Vec3f{3.0F, 1.0F, 2.0F}));
  EXPECT_EQ(feedback.front().sequence, 1U);

  const std::vector<arv::net::NetworkEvent> server_events =
      server_transport->poll_events();
  const arv::net::NetworkEvent* message =
      find_event(server_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(message->lane, arv::protocol::NetworkLane::Gameplay);

  const auto decoded = arv::protocol::deserialize_envelope(message->payload);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(
      decoded.value().envelope.message_type,
      arv::protocol::MessageType::ServerboundAttackEntity);
  const auto attack =
      arv::protocol::deserialize_serverbound_attack_entity(decoded.value().payload);
  ASSERT_TRUE(attack);
  EXPECT_EQ(attack.value().target_entity, (arv::EntityNetID{5}));
  EXPECT_EQ(attack.value().sequence, 1U);
}

TEST(ClientRuntimeTests, RideRequestSendsRideEntityPacket) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const auto result =
      client.request_ride_entity(arv::EntityNetID{42}, "driver");
  ASSERT_TRUE(result);

  const std::vector<arv::net::NetworkEvent> server_events =
      server_transport->poll_events();
  const arv::net::NetworkEvent* message =
      find_event(server_events, arv::net::NetworkEventType::MessageReceived);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(message->lane, arv::protocol::NetworkLane::Gameplay);

  const auto decoded = arv::protocol::deserialize_envelope(message->payload);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(
      decoded.value().envelope.message_type,
      arv::protocol::MessageType::ServerboundRideEntityRequest);
  const auto request =
      arv::protocol::deserialize_serverbound_ride_entity_request(decoded.value().payload);
  ASSERT_TRUE(request);
  EXPECT_EQ(request.value().rideable_id, (arv::EntityNetID{42}));
  EXPECT_EQ(request.value().seat_id, "driver");
}

TEST(ClientRuntimeTests, MountActionRequestsDismountWhenMounted) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.mount_pressed = true});
  client.fixed_tick(3);

  const std::vector<arv::protocol::ServerboundDismountRequest> requests =
      serverbound_dismount_requests(server_transport->poll_events());
  ASSERT_EQ(requests.size(), 1U);
  EXPECT_EQ(requests.front().rideable_id, (arv::EntityNetID{10}));
}

TEST(ClientRuntimeTests, GroundVehiclePredictionClampsAgainstWallAndMovesSeatAnchor) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {4.0F, 1.0F, 4.0F});

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{10};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_buggy";
  spawn.position = {0.0F, 1.0F, 0.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundRideStarted ride;
  ride.passenger_id = arv::EntityNetID{1};
  ride.rideable_id = arv::EntityNetID{10};
  ride.seat_id = "driver";
  ride.controls_vehicle = true;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundRideStarted,
      arv::protocol::serialize_clientbound_ride_started(ride));
  client.fixed_tick(2);
  ASSERT_TRUE(client.find_replicated_entity(arv::EntityNetID{10}) != nullptr);
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  (void)world.ensure_chunk({{0, 0, -1}});
  ASSERT_TRUE(world.set_block({{0, 1, -3}}, stone));
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 34; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_forward = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_GE(last.position.z, -0.401F);
  EXPECT_NEAR(last.velocity.z, 0.0F, 0.0001F);
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().y, last.position.y + 0.8F, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, LivingMountPredictionFallsWhenUnsupported) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_living_mount_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 4.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_horse",
      "saddle",
      arv::EntityNetID{10},
      {0.0F, 4.0F, 0.0F});

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 8; ++tick) {
    client.submit_input(arv::client::InputFrame{});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_LT(last.position.y, 4.0F);
  EXPECT_LT(last.velocity.y, 0.0F);
  EXPECT_FALSE(arv::protocol::has_vehicle_move_flag(
      last.flags,
      arv::protocol::VehicleMoveFlags::OnGround));
  EXPECT_NEAR(client.player_position().y, last.position.y + 1.1F, 0.0001F);
}

TEST(ClientRuntimeTests, LivingMountPredictionStepsOntoOneBlockObstacle) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_living_mount_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_horse",
      "saddle",
      arv::EntityNetID{10},
      {0.0F, 1.0F, 0.0F});

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -3; z <= 8; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  for (arv::i32 z = 2; z <= 4; ++z) {
    for (arv::i32 x = -2; x <= 2; ++x) {
      ASSERT_TRUE(world.set_block({{x, 1, z}}, stone));
    }
  }
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  bool stepped_up = false;
  bool advanced_on_step = false;
  for (arv::TickIndex tick = 3; tick < 100; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_forward = true});
    client.fixed_tick(tick, &query);
    if (client.player_position().y > 2.6F) {
      stepped_up = true;
      if (client.player_position().z > 1.25F) {
        advanced_on_step = true;
        break;
      }
    }
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_TRUE(stepped_up);
  EXPECT_TRUE(advanced_on_step);
  EXPECT_GT(last.position.y, 1.5F);
  EXPECT_GT(last.position.z, 1.25F);
  EXPECT_TRUE(arv::protocol::has_vehicle_move_flag(
      last.flags,
      arv::protocol::VehicleMoveFlags::OnGround));
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().y, last.position.y + 1.1F, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, AircraftPredictionRoutesInputToFlightMove) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 4.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_plane",
      "pilot",
      arv::EntityNetID{10},
      {0.0F, 4.0F, 0.0F});

  for (arv::TickIndex tick = 3; tick < 12; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .move_right = true,
            .jump_held = true,
            .vehicle_roll_right_held = true});
    client.fixed_tick(tick);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_GT(arv::length(last.velocity), 0.1F);
  EXPECT_GT(last.angular_velocity.x, 0.01F);
  EXPECT_GT(last.angular_velocity.y, 0.01F);
  EXPECT_GT(last.angular_velocity.z, 0.01F);
  EXPECT_FALSE(arv::protocol::has_vehicle_move_flag(
      last.flags,
      arv::protocol::VehicleMoveFlags::InWater));
}

TEST(ClientRuntimeTests, VehicleAliasUsesTaggedDefinitionForSeatAndMovement) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 4.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_plane/basic",
      "pilot",
      arv::EntityNetID{10},
      {0.0F, 4.0F, 0.0F});

  EXPECT_NEAR(client.player_position().y, 4.75F, 0.0001F);

  for (arv::TickIndex tick = 3; tick < 12; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .jump_held = true});
    client.fixed_tick(tick);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  EXPECT_GT(arv::length(moves.back().velocity), 0.1F);
}

TEST(ClientRuntimeTests, RocketVerticalThrustClimbsWithoutPitchRotation) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 4.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_rocket/basic",
      "pilot",
      arv::EntityNetID{10},
      {0.0F, 4.0F, 0.0F});

  EXPECT_NEAR(client.player_position().y, 7.2F, 0.0001F);

  for (arv::TickIndex tick = 3; tick < 45; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .jump_held = true});
    client.fixed_tick(tick);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  arv::f32 max_y = moves.front().position.y;
  arv::f32 max_vertical_speed = moves.front().velocity.y;
  for (const arv::protocol::ServerboundVehicleMove& move : moves) {
    max_y = std::max(max_y, move.position.y);
    max_vertical_speed = std::max(max_vertical_speed, move.velocity.y);
    EXPECT_NEAR(move.angular_velocity.x, 0.0F, 0.0001F);
  }
  EXPECT_GT(max_y, 8.0F);
  EXPECT_GT(max_vertical_speed, 24.0F);
  EXPECT_GT(moves.back().velocity.y, 24.0F);
}

TEST(ClientRuntimeTests, AircraftTakeoffAssistClimbsFromGroundedRoll) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_plane",
      "pilot",
      arv::EntityNetID{10},
      {0.0F, 1.0F, 0.0F});

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (arv::i32 section_z = -5; section_z <= 0; ++section_z) {
    for (arv::i32 section_x = -1; section_x <= 0; ++section_x) {
      (void)world.ensure_chunk({{section_x, 0, section_z}});
    }
  }
  for (arv::i32 z = -80; z <= 8; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 55; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .jump_held = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  arv::f32 max_y = moves.front().position.y;
  arv::f32 max_vertical_speed = moves.front().velocity.y;
  for (const arv::protocol::ServerboundVehicleMove& move : moves) {
    max_y = std::max(max_y, move.position.y);
    max_vertical_speed = std::max(max_vertical_speed, move.velocity.y);
  }
  EXPECT_GT(max_y, 1.05F);
  EXPECT_GT(max_vertical_speed, 0.1F);
  EXPECT_GT(moves.back().position.y, 1.05F);
}

TEST(ClientRuntimeTests, AircraftAcceleratesAfterNoseUpStall) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 12.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_plane",
      "pilot",
      arv::EntityNetID{10},
      {0.0F, 12.0F, 0.0F});

  for (arv::TickIndex tick = 3; tick < 28; ++tick) {
    client.submit_input(arv::client::InputFrame{.jump_held = true});
    client.fixed_tick(tick);
  }
  (void)server_transport->poll_events();

  for (arv::TickIndex tick = 28; tick < 52; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .jump_held = true});
    client.fixed_tick(tick);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  const arv::Vec3f forward =
      arv::rotate_vector(arv::normalize_quat(last.rotation), {0.0F, 0.0F, -1.0F});
  arv::f32 max_y = moves.front().position.y;
  for (const arv::protocol::ServerboundVehicleMove& move : moves) {
    max_y = std::max(max_y, move.position.y);
  }
  EXPECT_GT(arv::dot(last.velocity, forward), 3.0F);
  EXPECT_GT(last.velocity.y, 0.25F);
  EXPECT_GT(max_y, moves.front().position.y + 0.5F);
  EXPECT_GT(last.throttle, 0.0F);
}

TEST(ClientRuntimeTests, SubmersiblePredictionUsesWaterOccupancyAndVerticalInput) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_air_vehicle_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_vehicle(
      client,
      *server_transport,
      connection_id.value(),
      "arvane:test_submarine",
      "pilot",
      arv::EntityNetID{10},
      {4.0F, 1.0F, 4.0F});

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::FluidDefinition water =
      arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  const arv::voxel::FluidCell full_water{
      water.type,
      water.max_level,
      arv::voxel::FluidFlags::Source | arv::voxel::FluidFlags::Settled};
  for (arv::i32 x = 2; x <= 6; ++x) {
    for (arv::i32 z = 2; z <= 6; ++z) {
      ASSERT_TRUE(world.set_fluid({{x, 1, z}}, full_water, water));
      ASSERT_TRUE(world.set_fluid({{x, 2, z}}, full_water, water));
    }
  }
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 10; ++tick) {
    client.submit_input(
        arv::client::InputFrame{
            .move_forward = true,
            .move_left = true,
            .jump_held = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_TRUE(arv::protocol::has_vehicle_move_flag(
      last.flags,
      arv::protocol::VehicleMoveFlags::InWater));
  EXPECT_GT(arv::horizontal_length(last.velocity), 1.5F);
  EXPECT_GT(last.velocity.y, 0.0F);

  for (arv::TickIndex tick = 10; tick < 18; ++tick) {
    client.submit_input(arv::client::InputFrame{.sneak_held = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> descend_moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(descend_moves.empty());
  EXPECT_LT(descend_moves.back().velocity.y, 0.0F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionKeepsRepeatedWallImpactClamped) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 x = -4; x <= 4; ++x) {
    ASSERT_TRUE(world.set_block({{x, 1, -3}}, stone));
  }
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 34; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_forward = true});
    client.fixed_tick(tick, &query);
  }
  for (arv::TickIndex tick = 34; tick < 58; ++tick) {
    client.submit_input(
        arv::client::InputFrame{.move_forward = true, .move_right = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_GE(last.position.z, -0.401F);
  EXPECT_NEAR(last.velocity.z, 0.0F, 0.0001F);
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionKeepsHighSpeedCornerImpactClamped) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -6; z <= 2; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  ASSERT_TRUE(world.set_block({{0, 1, -3}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, -2}}, stone));
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 95; ++tick) {
    client.submit_input(
        arv::client::InputFrame{.move_forward = true, .move_right = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_GE(last.position.z, -0.401F);
  EXPECT_LE(last.position.x, 0.101F);
  EXPECT_LE(last.velocity.x, 0.0001F);
  EXPECT_NEAR(last.velocity.z, 0.0F, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionSlidesPastDiagonalTreeImpact) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -6; z <= 2; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  ASSERT_TRUE(world.set_block({{-2, 1, -3}}, stone));
  ASSERT_TRUE(world.set_block({{-2, 3, -3}}, stone));
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 95; ++tick) {
    client.submit_input(
        arv::client::InputFrame{.move_forward = true, .move_right = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_LT(last.position.z, -1.25F);
  EXPECT_GT(arv::horizontal_length(last.velocity), 0.05F);
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionReversesOutOfSingleTreeCornerImpact) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -6; z <= 2; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  ASSERT_TRUE(world.set_block({{-2, 1, -2}}, stone));
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 52; ++tick) {
    client.submit_input(
        arv::client::InputFrame{.move_forward = true, .move_right = true});
    client.fixed_tick(tick, &query);
  }
  const std::vector<arv::protocol::ServerboundVehicleMove> impact_moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(impact_moves.empty());
  const arv::protocol::ServerboundVehicleMove impact = impact_moves.back();

  for (arv::TickIndex tick = 52; tick < 90; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_backward = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> escape_moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(escape_moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = escape_moves.back();
  EXPECT_GT(last.position.z, impact.position.z + 0.15F);
  EXPECT_GT(arv::horizontal_length(last.velocity), 0.05F);
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionIgnoresMountedRiderHeadCollision) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -6; z <= 2; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  ASSERT_TRUE(world.set_block({{0, 3, -3}}, stone));
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 65; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_forward = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_LT(last.position.z, -1.25F);
  EXPECT_GT(arv::horizontal_length(last.velocity), 0.05F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionFallsOffLedgeAndClearsGroundFlag) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (const arv::ChunkSectionCoord coord : {
           arv::ChunkSectionCoord{{-1, 0, -1}},
           arv::ChunkSectionCoord{{-1, 0, 0}},
           arv::ChunkSectionCoord{{0, 0, -1}},
           arv::ChunkSectionCoord{{0, 0, 0}},
       }) {
    (void)world.ensure_chunk(coord);
  }
  for (arv::i32 z = -2; z <= 2; ++z) {
    for (arv::i32 x = -2; x <= 2; ++x) {
      ASSERT_TRUE(world.set_block({{x, 0, z}}, stone));
    }
  }
  const arv::world::WorldCollisionQuery query(world, block_registry, false);

  for (arv::TickIndex tick = 3; tick < 95; ++tick) {
    client.submit_input(arv::client::InputFrame{.move_forward = true});
    client.fixed_tick(tick, &query);
  }

  const std::vector<arv::protocol::ServerboundVehicleMove> moves =
      serverbound_vehicle_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundVehicleMove& last = moves.back();
  EXPECT_LT(last.position.z, -3.6F);
  EXPECT_LT(last.position.y, 0.95F);
  EXPECT_LT(last.velocity.y, -0.1F);
  EXPECT_FALSE(arv::protocol::has_vehicle_move_flag(
      last.flags,
      arv::protocol::VehicleMoveFlags::OnGround));
  EXPECT_NEAR(client.player_position().x, last.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().y, last.position.y + 0.8F, 0.0001F);
  EXPECT_NEAR(client.player_position().z, last.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, GroundVehiclePredictionAdvancesBetweenNetworkSends) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(20.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(3);

  EXPECT_TRUE(serverbound_vehicle_moves(server_transport->poll_events()).empty());
  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(mirror, nullptr);
  EXPECT_LT(mirror->position.z, 0.0F);
  EXPECT_EQ(mirror->render_position, mirror->position);
  EXPECT_NEAR(client.player_position().z, mirror->position.z, 0.0001F);
  ASSERT_NE(find_mirror_animation(*mirror, "driving"), nullptr);
  EXPECT_EQ(find_mirror_animation(*mirror, "reverse"), nullptr);
  const arv::Vec3f visual_forward =
      arv::rotate_vector(mirror->rotation, {0.0F, 0.0F, -1.0F});
  EXPECT_GT(arv::dot(visual_forward, mirror->velocity), 0.0F);
}

TEST(ClientRuntimeTests, GroundVehicleReverseInputMovesRearwardAndPlaysReverseAnimation) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(20.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.move_backward = true});
  client.fixed_tick(3);

  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(mirror, nullptr);
  EXPECT_GT(mirror->position.z, 0.0F);
  ASSERT_NE(find_mirror_animation(*mirror, "reverse"), nullptr);
  EXPECT_EQ(find_mirror_animation(*mirror, "driving"), nullptr);
  const arv::Vec3f visual_forward =
      arv::rotate_vector(mirror->rotation, {0.0F, 0.0F, -1.0F});
  EXPECT_LT(arv::dot(visual_forward, mirror->velocity), 0.0F);
}

TEST(ClientRuntimeTests, LocalControlledVehicleStateEchoDoesNotRewindPrediction) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(10.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(3);
  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(4);
  const arv::client::ReplicatedEntityMirror* before =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(before, nullptr);
  const arv::Vec3f predicted_before = before->position;
  ASSERT_LT(predicted_before.z, 0.0F);

  arv::protocol::ClientboundVehicleState stale_echo;
  stale_echo.world_id = arv::WorldID{1};
  stale_echo.vehicle_id = arv::EntityNetID{10};
  stale_echo.server_tick = 20;
  stale_echo.accepted_sequence = 1;
  stale_echo.position = {0.0F, 1.0F, 0.0F};
  stale_echo.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundVehicleState,
      arv::protocol::serialize_clientbound_vehicle_state(stale_echo),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);

  client.fixed_tick(5);

  const arv::client::ReplicatedEntityMirror* after =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(after, nullptr);
  EXPECT_LT(after->position.z, predicted_before.z);
  EXPECT_EQ(after->render_position, after->position);
  EXPECT_NEAR(client.player_position().z, after->position.z, 0.0001F);
}

TEST(ClientRuntimeTests, LocalControlledEntitySnapshotDoesNotRewindVehiclePrediction) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(10.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(3);
  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(4);
  const arv::client::ReplicatedEntityMirror* before =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(before, nullptr);
  const arv::Vec3f predicted_before = before->position;
  ASSERT_LT(predicted_before.z, 0.0F);
  ASSERT_NE(find_mirror_animation(*before, "driving"), nullptr);

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 20;
  snapshot.entities.push_back({arv::EntityNetID{10},
                               {0.0F, 1.0F, 0.0F},
                               {},
                               {0.0F, 0.0F, 0.0F, 1.0F},
                               0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);

  client.fixed_tick(5);

  const arv::client::ReplicatedEntityMirror* after =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(after, nullptr);
  EXPECT_LT(after->position.z, predicted_before.z);
  EXPECT_EQ(after->render_position, after->position);
  EXPECT_NEAR(client.player_position().z, after->position.z, 0.0001F);
  EXPECT_NE(find_mirror_animation(*after, "driving"), nullptr);
}

TEST(ClientRuntimeTests, VehicleCorrectionResetsLocalPredictionAndSeatAnchor) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::entity::VehicleDefinitionRegistry vehicles = make_client_buggy_registry();
  client.configure_vehicles(&vehicles);
  client.set_movement_send_rate(10.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value(), {0.0F, 1.0F, 0.0F});
  spawn_and_mount_test_buggy(client, *server_transport, connection_id.value());

  client.submit_input(arv::client::InputFrame{.move_forward = true});
  client.fixed_tick(3);

  arv::protocol::ClientboundVehicleCorrection correction;
  correction.vehicle_id = arv::EntityNetID{10};
  correction.rejected_sequence = 3;
  correction.position = {4.0F, 1.0F, -2.0F};
  correction.velocity = {};
  correction.rotation = {};
  correction.reason = arv::protocol::VehicleCorrectionReason::Collision;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundVehicleCorrection,
      arv::protocol::serialize_clientbound_vehicle_correction(correction));

  client.submit_input(arv::client::InputFrame{});
  client.fixed_tick(4);

  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{10});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, correction.position);
  EXPECT_EQ(mirror->render_position, correction.position);
  EXPECT_NEAR(client.player_position().x, correction.position.x, 0.0001F);
  EXPECT_NEAR(client.player_position().y, correction.position.y + 0.8F, 0.0001F);
  EXPECT_NEAR(client.player_position().z, correction.position.z, 0.0001F);
}

TEST(ClientRuntimeTests, MovementHzPacketOverridesDefaultClientSendRate) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  client.fixed_tick(2);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
  client.fixed_tick(3);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 0U);

  const arv::protocol::ClientboundSetMovementHz settings{60.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundSetMovementHz,
      arv::protocol::serialize_clientbound_set_movement_hz(settings));

  client.fixed_tick(4);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
  client.fixed_tick(5);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 1U);
}

TEST(ClientRuntimeTests, RemoteFixedTickSeedsClientTickWhenWorldJoinCompletes) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();

  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(hello));

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{1};
  join.local_entity_id = arv::EntityNetID{1};
  join.spawn_position = {8.0F, 20.0F, -20.0F};
  join.initial_tick = 5;
  join.tick_rate = arv::DefaultBaseTps;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));

  arv::TickIndex client_tick = 0U;
  const arv::client::RemoteClientFixedTickResult result =
      arv::client::advance_remote_client_fixed_tick(client, client_tick);

  EXPECT_FALSE(result.was_in_game);
  EXPECT_TRUE(result.entered_game);
  EXPECT_EQ(result.submitted_tick, 0U);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(client.world_initial_tick(), 5U);
  EXPECT_EQ(client_tick, 5U);
  EXPECT_EQ(player_move_count(server_transport->poll_events()), 0U);
}

TEST(ClientRuntimeTests, RemoteFixedTickIncrementsClientTickWhileInGame) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());
  (void)server_transport->poll_events();

  arv::TickIndex client_tick = 0U;
  const arv::client::RemoteClientFixedTickResult result =
      arv::client::advance_remote_client_fixed_tick(client, client_tick);

  EXPECT_TRUE(result.was_in_game);
  EXPECT_FALSE(result.entered_game);
  EXPECT_EQ(client_tick, client.world_initial_tick() + 1U);
  EXPECT_EQ(result.submitted_tick, client_tick);
  const std::vector<arv::protocol::ServerboundPlayerMove> moves =
      serverbound_player_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  EXPECT_EQ(moves.back().client_tick, client_tick);
}

TEST(ClientRuntimeTests, PlayerPermissionsPacketUpdatesLocalGameModeAndCorePermissions) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "creative";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_fly = true;
  permissions.core.has_creative_inventory = true;
  permissions.core.has_collision = false;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerPermissions,
      arv::protocol::serialize_clientbound_player_permissions(permissions));

  client.fixed_tick(2);
  EXPECT_EQ(client.game_mode_id(), "creative");
  EXPECT_TRUE(client.resolved_permissions().core.can_fly);
  EXPECT_TRUE(client.resolved_permissions().core.has_creative_inventory);
}

TEST(ClientRuntimeTests, PlayerMovementTuningPacketUpdatesLocalFlySpeed) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  client.set_movement_send_rate(60.0F);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "creative";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_fly = true;
  permissions.core.has_creative_inventory = true;
  permissions.core.has_collision = false;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerPermissions,
      arv::protocol::serialize_clientbound_player_permissions(permissions));
  const arv::protocol::ClientboundPlayerMovementTuning tuning{10.0F, 6.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerMovementTuning,
      arv::protocol::serialize_clientbound_player_movement_tuning(tuning));
  client.fixed_tick(2);
  (void)server_transport->poll_events();

  client.submit_input({.jump_pressed = true, .jump_held = true});
  client.fixed_tick(3);
  (void)server_transport->poll_events();
  client.submit_input({});
  client.fixed_tick(4);
  (void)server_transport->poll_events();
  client.submit_input({.jump_pressed = true, .jump_held = true});
  client.fixed_tick(5);
  (void)server_transport->poll_events();
  client.submit_input({.move_forward = true});
  client.fixed_tick(6);

  const std::vector<arv::protocol::ServerboundPlayerMove> moves =
      serverbound_player_moves(server_transport->poll_events());
  ASSERT_FALSE(moves.empty());
  const arv::protocol::ServerboundPlayerMove& move = moves.back();
  EXPECT_TRUE(arv::protocol::has_move_flag(
      move.flags,
      arv::protocol::PlayerMoveFlags::Flying));
  EXPECT_FLOAT_EQ(move.velocity.z, 10.0F);
}

TEST(ClientRuntimeTests, ReplicatedEntityRenderPositionSmoothsBetweenTargets) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {0.0F, 0.0F, 0.0F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  client.fixed_tick(2);

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.entities.push_back({arv::EntityNetID{5},
                               {0.0F, 0.0F, 0.0F},
                               {},
                               {0.0F, 0.0F, 0.0F, 1.0F},
                               0});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);

  snapshot.server_tick = 13;
  snapshot.entities[0].position = {3.0F, 0.0F, 0.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(4);

  const arv::client::ReplicatedEntityMirror* mirror =
      client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{3.0F, 0.0F, 0.0F}));
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 0.0F}));

  client.advance_entity_interpolation(1.0F / 60.0F);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_NEAR(mirror->render_position.x, 1.0F, 0.001F);

  client.advance_entity_interpolation(2.0F / 60.0F);
  mirror = client.find_replicated_entity(arv::EntityNetID{5});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{3.0F, 0.0F, 0.0F}));
}

TEST(ClientRuntimeTests, DroppedItemSnapshotsCreateUpdateAndRemoveMirrors) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundDroppedItemSnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.drops.push_back({arv::EntityNetID{7},
                            arv::player::ItemStack{arv::player::ItemID{8}, 2},
                            {1.0F, 2.0F, 3.0F},
                            {}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);

  ASSERT_EQ(client.dropped_item_count(), 1U);
  const arv::client::DroppedItemMirror* mirror =
      client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->stack, (arv::player::ItemStack{arv::player::ItemID{8}, 2}));
  EXPECT_EQ(mirror->position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));

  snapshot.server_tick = 11;
  snapshot.drops.clear();
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);
  EXPECT_EQ(client.dropped_item_count(), 0U);
}

TEST(ClientRuntimeTests, DroppedItemRenderPositionSmoothsBetweenTargets) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundDroppedItemSnapshot snapshot;
  snapshot.world_id = arv::WorldID{1};
  snapshot.server_tick = 10;
  snapshot.drops.push_back({arv::EntityNetID{7},
                            arv::player::ItemStack{arv::player::ItemID{8}, 2},
                            {0.0F, 0.0F, 0.0F},
                            {}});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(2);

  snapshot.server_tick = 13;
  snapshot.drops[0].position = {0.0F, 0.0F, 3.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(snapshot),
      arv::protocol::ReliabilityClass::UnreliableLatestWins);
  client.fixed_tick(3);

  const arv::client::DroppedItemMirror* mirror =
      client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->position, (arv::Vec3f{0.0F, 0.0F, 3.0F}));
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 0.0F}));

  client.advance_entity_interpolation(1.0F / 60.0F);
  mirror = client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_NEAR(mirror->render_position.z, 1.0F, 0.001F);

  client.advance_entity_interpolation(2.0F / 60.0F);
  mirror = client.find_dropped_item(arv::EntityNetID{7});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{0.0F, 0.0F, 3.0F}));
}

TEST(ClientRuntimeTests, InventoryAndBlockUpdatesRejectInactiveOrStaleState) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundBlockUpdateBatch wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.server_tick = 2;
  wrong_world.updates.push_back(
      {arv::BlockPos{{0, 0, 0}}, arv::voxel::BlockVariantID{1}, 0, 2});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(wrong_world));
  client.fixed_tick(2);
  EXPECT_TRUE(client.drain_received_block_updates().empty());

  wrong_world.world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(wrong_world));
  client.fixed_tick(3);
  EXPECT_EQ(client.drain_received_block_updates().size(), 1U);

  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 2;
  update.selected_hotbar_slot = 0;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 4}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
  client.fixed_tick(4);
  ASSERT_TRUE(client.has_authoritative_inventory());
  ASSERT_TRUE(client.inventory().slot(0).has_value());
  EXPECT_EQ(client.inventory().slot(0)->count, 4U);
  EXPECT_EQ(client.inventory_revision(), 2U);

  update.revision = 1;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 9}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
  client.fixed_tick(5);
  ASSERT_TRUE(client.inventory().slot(0).has_value());
  EXPECT_EQ(client.inventory().slot(0)->count, 4U);
  EXPECT_EQ(client.inventory_revision(), 2U);
}

TEST(ClientRuntimeTests, DrainChunkStreamPacketsReturnsAllPendingChunkQueues) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  const arv::protocol::ClientboundChunkSectionUnload unload{
      arv::WorldID{1}, arv::ChunkSectionCoord{{1, 2, 3}}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChunkSectionUnload,
      arv::protocol::serialize_clientbound_chunk_section_unload(unload));

  arv::protocol::ClientboundChunkSectionData section;
  section.world_id = arv::WorldID{1};
  section.coord = arv::ChunkSectionCoord{{4, 5, 6}};
  section.chunk_version = 7U;
  section.chunk_content_version = 8U;
  section.chunk_light_version = 9U;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChunkSectionData,
      arv::protocol::serialize_clientbound_chunk_section_data(section));

  arv::protocol::ClientboundBlockUpdateBatch block_updates;
  block_updates.world_id = arv::WorldID{1};
  block_updates.server_tick = 2U;
  block_updates.updates.push_back(
      {arv::BlockPos{{0, 1, 2}}, arv::voxel::BlockVariantID{3U}, 0U, 4U});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(block_updates));

  client.fixed_tick(2);

  arv::client::ClientChunkStreamPackets packets =
      client.drain_chunk_stream_packets();
  const arv::client::ClientChunkStreamPacketCounts counts =
      arv::client::count_client_chunk_stream_packets(packets);
  EXPECT_EQ(counts.unloads, 1U);
  EXPECT_EQ(counts.sections, 1U);
  EXPECT_EQ(counts.block_update_batches, 1U);
  EXPECT_EQ(counts.block_updates, 1U);
  ASSERT_EQ(packets.unloads.size(), 1U);
  EXPECT_EQ(packets.unloads.front().coord, unload.coord);
  ASSERT_EQ(packets.sections.size(), 1U);
  EXPECT_EQ(packets.sections.front().coord, section.coord);
  EXPECT_EQ(packets.sections.front().chunk_version, 7U);
  ASSERT_EQ(packets.block_updates.size(), 1U);
  ASSERT_EQ(packets.block_updates.front().updates.size(), 1U);
  EXPECT_EQ(
      packets.block_updates.front().updates.front().pos,
      (arv::BlockPos{{0, 1, 2}}));

  packets = client.drain_chunk_stream_packets();
  EXPECT_TRUE(packets.unloads.empty());
  EXPECT_TRUE(packets.sections.empty());
  EXPECT_TRUE(packets.block_updates.empty());
}

TEST(ClientRuntimeTests, ClientboundSpawnParticlesCreatesLocalParticlesForCurrentWorld) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  arv::client::ParticleRegistry particles = make_runtime_particle_registry();
  client.configure_particles(&particles);
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundSpawnParticles wrong_world;
  wrong_world.world_id = arv::WorldID{2};
  wrong_world.particle_type = "arvane:block_dust";
  wrong_world.texture_asset = "arvane:texture/block/dirt";
  wrong_world.position = {0.0F, 0.0F, 0.0F};
  wrong_world.count = 2U;
  wrong_world.seed = 7U;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundSpawnParticles,
      arv::protocol::serialize_clientbound_spawn_particles(wrong_world),
      arv::protocol::ReliabilityClass::UnreliableEvent);
  client.fixed_tick(2);
  EXPECT_EQ(client.particle_stats().spawned_total, 0U);

  wrong_world.world_id = arv::WorldID{1};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundSpawnParticles,
      arv::protocol::serialize_clientbound_spawn_particles(wrong_world),
      arv::protocol::ReliabilityClass::UnreliableEvent);
  client.fixed_tick(3);
  EXPECT_EQ(client.particle_stats().spawned_total, 2U);
  EXPECT_EQ(client.particle_stats().live_particles, 2U);

  const arv::render::FramePacket frame = client.build_frame_packet(0.0F, 1280U, 720U);
  EXPECT_EQ(frame.particles.size(), 2U);
  ASSERT_FALSE(frame.particles.empty());
  EXPECT_EQ(frame.particles.front().uv_min, (arv::Vec2f{32.0F, 0.0F}));
}

TEST(ClientRuntimeTests, BuildFramePacketProjectsCameraMatrices) {
  auto transports = arv::net::make_in_process_transport_pair();
  arv::client::ClientRuntime client(std::move(transports.client));
  client.camera().set_world_id(arv::WorldID{7});
  client.camera().set_position({3.0F, 4.0F, 5.0F});
  client.camera().set_rotation(0.5F, -0.25F);
  client.camera().set_fov_y_degrees(80.0F);

  const arv::render::FramePacket frame =
      client.build_frame_packet(0.25F, 1280U, 720U);

  EXPECT_EQ(frame.camera.world_id, arv::WorldID{7});
  EXPECT_EQ(frame.camera.viewport_width, 1280U);
  EXPECT_EQ(frame.camera.viewport_height, 720U);
  EXPECT_FLOAT_EQ(frame.camera.partial_tick_alpha, 0.25F);
  EXPECT_FLOAT_EQ(frame.camera.near_plane, arv::client::DefaultCameraNearPlane);
  EXPECT_FLOAT_EQ(frame.camera.far_plane, arv::client::DefaultCameraFarPlane);
  EXPECT_NE(frame.camera.view, arv::identity_mat4f());
  EXPECT_NE(frame.camera.projection, arv::identity_mat4f());
  EXPECT_NE(frame.camera.view_projection, arv::identity_mat4f());
  EXPECT_EQ(frame.world_view.world_id, frame.camera.world_id);
  EXPECT_EQ(frame.world_view.view, frame.camera.view);
  EXPECT_EQ(frame.world_view.projection, frame.camera.projection);
  EXPECT_EQ(frame.world_view.view_projection, frame.camera.view_projection);
  EXPECT_FLOAT_EQ(frame.world_view.far_plane, arv::client::DefaultCameraFarPlane);
}

TEST(ClientRuntimeTests, WorldLeaveClearsActiveWorldMirrors) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{1};
  spawn.entity_id = arv::EntityNetID{5};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));

  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 1;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 1}};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(update));
  client.fixed_tick(2);
  ASSERT_EQ(client.replicated_entity_count(), 1U);
  ASSERT_TRUE(client.has_authoritative_inventory());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldLeave,
      arv::protocol::serialize_clientbound_world_leave({arv::WorldID{1}, 0}));
  client.fixed_tick(3);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::WorldJoin);
  EXPECT_EQ(client.replicated_entity_count(), 0U);
  EXPECT_FALSE(client.has_authoritative_inventory());
  EXPECT_FALSE(client.local_player_id().has_value());
  EXPECT_FALSE(client.local_entity_id().has_value());
  EXPECT_TRUE(client.drain_received_block_updates().empty());
}

TEST(ClientRuntimeTests, WorldLeaveExposesTransitionStatusUntilJoinFadeCompletes) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  enter_in_game(client, *server_transport, connection_id.value());

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldLeave,
      arv::protocol::serialize_clientbound_world_leave({arv::WorldID{1}, 0}));
  client.fixed_tick(2);

  const arv::client::ClientWorldTransitionStatus& leaving =
      client.world_transition_status();
  ASSERT_TRUE(leaving.active);
  EXPECT_EQ(
      leaving.phase,
      arv::client::ClientWorldTransitionPhase::LeavingWorld);
  EXPECT_EQ(leaving.world_id, arv::WorldID{1});
  EXPECT_FLOAT_EQ(leaving.fade_alpha, 1.0F);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::WorldJoin);

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{2};
  join.player_id = arv::PlayerID{1};
  join.local_entity_id = arv::EntityNetID{7};
  join.spawn_position = {4.0F, 60.0F, 8.0F};
  join.initial_tick = 20;
  join.tick_rate = arv::DefaultBaseTps;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  client.fixed_tick(3);

  const arv::client::ClientWorldTransitionStatus& entering =
      client.world_transition_status();
  ASSERT_TRUE(entering.active);
  EXPECT_EQ(
      entering.phase,
      arv::client::ClientWorldTransitionPhase::EnteringWorld);
  EXPECT_EQ(entering.world_id, arv::WorldID{2});
  EXPECT_GT(entering.fade_alpha, 0.0F);
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  EXPECT_EQ(client.camera().world_id(), arv::WorldID{2});

  for (arv::TickIndex tick = 4; tick < 40; ++tick) {
    client.fixed_tick(tick);
  }
  EXPECT_FALSE(client.world_transition_status().active);
  EXPECT_FLOAT_EQ(client.world_transition_status().fade_alpha, 0.0F);
}

TEST(ClientRuntimeTests, RegistrySnapshotMismatchReportsFailureAndBlocksJoin) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientRuntime client(std::move(transports.client));
  const auto connection_id = client.connect_integrated();
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();

  arv::protocol::ClientboundHello hello;
  hello.accepted = true;
  hello.assigned_session_id = arv::SessionID{1};
  hello.assigned_player_id = arv::PlayerID{1};
  hello.initial_world_id = arv::WorldID{1};
  hello.required_followup_sync = true;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(hello));
  client.fixed_tick(1);
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::RegistrySync);

  arv::protocol::ClientboundPackManifest manifest;
  manifest.registry_hash = std::string(64U, 'a');
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPackManifest,
      arv::protocol::serialize_clientbound_pack_manifest(manifest));
  client.fixed_tick(2);

  std::vector<arv::protocol::ServerboundPackSyncStatus> statuses =
      serverbound_pack_sync_statuses(server_transport->poll_events());
  ASSERT_EQ(statuses.size(), 1U);
  EXPECT_EQ(statuses.front().status, arv::protocol::PackSyncStatus::AlreadyCached);
  EXPECT_TRUE(statuses.front().needed_bundle_hashes.empty());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::RegistrySync);

  arv::protocol::ClientboundRegistrySnapshot snapshot;
  snapshot.registry_hash = std::string(64U, 'b');
  snapshot.registry_snapshot_payload = "{\"format_version\":1,\"blocks\":[]}";
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundRegistrySnapshot,
      arv::protocol::serialize_clientbound_registry_snapshot(snapshot));
  client.fixed_tick(3);

  const std::vector<arv::net::NetworkEvent> events = server_transport->poll_events();
  statuses = serverbound_pack_sync_statuses(events);
  ASSERT_EQ(statuses.size(), 1U);
  EXPECT_EQ(statuses.front().status, arv::protocol::PackSyncStatus::VerificationFailed);
  EXPECT_TRUE(statuses.front().failure_reason.find("registry snapshot") != std::string::npos);
  EXPECT_TRUE(serverbound_pack_ready_messages(events).empty());
  EXPECT_EQ(client.connection_state(), arv::net::ConnectionState::Disconnected);
}
