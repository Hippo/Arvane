#include "arv/client/client_runtime.hpp"
#include "arv/crafting/smelting.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/container.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/world/block_container.hpp"
#include "support/server_harness.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::voxel::BlockRegistry make_container_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:dirt"));

  arv::voxel::BlockDefinition furnace;
  furnace.name = "arvane:furnace";
  furnace.display_name = "Furnace";
  furnace.properties.solid = true;
  furnace.properties.collidable = true;
  furnace.properties.full_cube = true;
  furnace.properties.hardness = 1.0F;
  furnace.render_category = arv::voxel::RenderCategory::Opaque;
  furnace.container_type = "arvane:furnace";
  furnace.texture_assets.fill("arvane:texture/block/furnace");
  definitions.push_back(std::move(furnace));

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
  chest.states.push_back({"facing", {"north", "south", "east", "west"}});
  definitions.push_back(std::move(chest));

  auto registry = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

[[nodiscard]] arv::player::ItemRegistry make_container_item_registry(
    const arv::voxel::BlockRegistry& block_registry) {
  std::vector<arv::player::ItemDefinition> items;
  const auto add_item = [&items](std::string name) {
    arv::player::ItemDefinition item;
    item.name = std::move(name);
    item.max_stack_size = 64;
    item.explicit_render_type = arv::player::ItemRenderType::Texture;
    item.texture_asset = "arvane:texture/item/test";
    items.push_back(std::move(item));
  };
  add_item("arvane:coal");
  add_item("arvane:copper");
  add_item("arvane:copper_bar");
  add_item("arvane:iron");
  add_item("arvane:iron_bar");

  auto registry = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(items));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

[[nodiscard]] arv::player::ContainerTypeRegistry make_container_type_registry() {
  std::vector<arv::player::ContainerTypeDefinition> definitions;
  definitions.push_back({"arvane:furnace", 3U, "Furnace", "arvane:furnace"});
  definitions.push_back({"arvane:chest", 27U, "Chest", "arvane:chest"});
  auto registry =
      arv::player::ContainerTypeRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

[[nodiscard]] arv::player::ContainerUiLayoutRegistry make_container_layout_registry() {
  arv::player::ContainerUiLayoutDefinition furnace;
  furnace.id = "arvane:furnace";
  furnace.size = {426.0F, 306.0F};
  furnace.slot_groups.push_back(
      {"furnace",
       arv::player::ContainerSlotSource::OpenedContainer,
       0U,
       3U,
       3U,
       {106.0F, 38.0F},
       {88.0F, 44.0F}});
  furnace.slot_groups.push_back(
      {"inventory",
       arv::player::ContainerSlotSource::PlayerInventory,
       9U,
       27U,
       9U,
       {18.0F, 104.0F},
       {44.0F, 44.0F}});
  furnace.slot_groups.push_back(
      {"hotbar",
       arv::player::ContainerSlotSource::PlayerInventory,
       0U,
       9U,
       9U,
       {18.0F, 248.0F},
       {44.0F, 44.0F}});

  arv::player::ContainerUiLayoutDefinition chest;
  chest.id = "arvane:chest";
  chest.size = {426.0F, 386.0F};
  chest.slot_groups.push_back(
      {"container",
       arv::player::ContainerSlotSource::OpenedContainer,
       0U,
       27U,
       9U,
       {18.0F, 38.0F},
       {44.0F, 44.0F}});
  chest.slot_groups.push_back(
      {"inventory",
       arv::player::ContainerSlotSource::PlayerInventory,
       9U,
       27U,
       9U,
       {18.0F, 184.0F},
       {44.0F, 44.0F}});
  chest.slot_groups.push_back(
      {"hotbar",
       arv::player::ContainerSlotSource::PlayerInventory,
       0U,
       9U,
       9U,
       {18.0F, 328.0F},
       {44.0F, 44.0F}});

  std::vector<arv::player::ContainerUiLayoutDefinition> definitions;
  definitions.push_back(std::move(furnace));
  definitions.push_back(std::move(chest));
  auto registry =
      arv::player::ContainerUiLayoutRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

[[nodiscard]] arv::crafting::SmeltingRuleRegistry make_smelting_registry(
    const arv::player::ItemRegistry& item_registry) {
  std::vector<arv::crafting::SmeltingRule> rules;
  rules.push_back(
      {
          "arvane:iron",
          item_registry.find_item("arvane:iron").value(),
          3U,
          "arvane:iron_bar",
          item_registry.find_item("arvane:iron_bar").value(),
          1U,
          20.0F});
  rules.push_back(
      {
          "arvane:copper",
          item_registry.find_item("arvane:copper").value(),
          3U,
          "arvane:copper_bar",
          item_registry.find_item("arvane:copper_bar").value(),
          1U,
          10.0F});
  auto registry = arv::crafting::SmeltingRuleRegistry::build(std::move(rules));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

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

  explicit MultiClientInProcessTransport(
      std::shared_ptr<MultiClientSharedState> shared,
      Side side,
      std::optional<arv::usize> client_index = std::nullopt)
      : shared_(std::move(shared)), side_(side), client_index_(client_index) {
  }

  [[nodiscard]] static std::unique_ptr<arv::net::ITransport> make_server(
      std::shared_ptr<MultiClientSharedState> shared) {
    return std::make_unique<MultiClientInProcessTransport>(
        std::move(shared),
        Side::Server);
  }

  [[nodiscard]] static std::unique_ptr<arv::net::ITransport> make_client(
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
      for (MultiClientEndpoint& endpoint : shared_->clients) {
        endpoint.state = arv::net::ConnectionState::Disconnected;
        endpoint.inbound.clear();
      }
      shared_->server_inbound.clear();
      return;
    }

    if (client_index_.has_value()) {
      MultiClientEndpoint& endpoint = shared_->clients[*client_index_];
      endpoint.state = arv::net::ConnectionState::Disconnected;
      endpoint.inbound.clear();
    }
  }

private:
  [[nodiscard]] MultiClientEndpoint* find_client(
      arv::net::ConnectionID connection_id) noexcept {
    auto it = std::find_if(
        shared_->clients.begin(),
        shared_->clients.end(),
        [connection_id](const MultiClientEndpoint& endpoint) {
          return endpoint.id == connection_id;
        });
    return it != shared_->clients.end() ? &*it : nullptr;
  }

  [[nodiscard]] const MultiClientEndpoint* find_client(
      arv::net::ConnectionID connection_id) const noexcept {
    auto it = std::find_if(
        shared_->clients.begin(),
        shared_->clients.end(),
        [connection_id](const MultiClientEndpoint& endpoint) {
          return endpoint.id == connection_id;
        });
    return it != shared_->clients.end() ? &*it : nullptr;
  }

  [[nodiscard]] static arv::net::NetworkEvent make_received_event(
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

  [[nodiscard]] static arv::net::NetworkEvent make_closed_event(
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

  [[nodiscard]] static bool connection_is_active(
      arv::net::ConnectionState state) noexcept {
    return state != arv::net::ConnectionState::Disconnected &&
           state != arv::net::ConnectionState::Disconnecting;
  }

  std::shared_ptr<MultiClientSharedState> shared_;
  Side side_{Side::Server};
  std::optional<arv::usize> client_index_{};
};

struct ContainerRuntimeFixture {
  arv::voxel::BlockRegistry block_registry{make_container_block_registry()};
  arv::player::ItemRegistry item_registry{
      make_container_item_registry(block_registry)};
  arv::crafting::SmeltingRuleRegistry smelting_registry{
      make_smelting_registry(item_registry)};
  arv::player::ContainerTypeRegistry container_types{make_container_type_registry()};
  arv::player::ContainerUiLayoutRegistry container_layouts{
      make_container_layout_registry()};
  arv::net::InProcessTransportPair transports{
      arv::net::make_in_process_transport_pair()};
  arv::server::ServerRuntime server{std::move(transports.server)};
  arv::client::ClientRuntime client{std::move(transports.client)};
  arv::BlockPos chest_pos{{3, 1, 0}};
  arv::BlockPos furnace_pos{{4, 1, 0}};

  ContainerRuntimeFixture() {
    arv::server::ServerStartInfo start_info;
    start_info.mode = arv::server::ServerMode::Integrated;
    start_info.base_tps = 1U;
    start_info.listen_address = {"in-process", 0};
    start_info.block_registry = &block_registry;
    start_info.item_registry = &item_registry;
    start_info.smelting_registry = &smelting_registry;
    start_info.container_types = &container_types;
    start_info.container_layouts = &container_layouts;
    start_info.block_breaking.base_seconds_per_hardness = 0.0F;
    EXPECT_TRUE(server.start(start_info));
    EXPECT_TRUE(client.connect_integrated());
    arv::testsupport::complete_integrated_handshake(server, client);

    (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
    const arv::voxel::BlockVariantID chest =
        block_registry.find_variant("arvane:chest[facing=north]").value();
    EXPECT_TRUE(server.voxel_world().set_block(chest_pos, chest));
    const arv::voxel::BlockVariantID furnace =
        block_registry.find_variant("arvane:furnace").value();
    EXPECT_TRUE(server.voxel_world().set_block(furnace_pos, furnace));
    position_player_for_chest();
  }

  void tick_round_trip() {
    server.tick();
    client.fixed_tick(server.tick_index());
  }

  void position_player_for_chest() {
    arv::player::PlayerState& player = arv::testsupport::connected_player(server);
    player.position = {0.5F, 0.0F, 0.5F};
    player.velocity = {};
    player.yaw_radians = std::numbers::pi_v<float> * 0.5F;
    player.pitch_radians = 0.0F;
    player.grounded = false;
    player.selected_hotbar_slot = 0U;
  }

  void give_player_dirt(arv::u16 count) {
    arv::player::PlayerState& player = arv::testsupport::connected_player(server);
    ASSERT_TRUE(player.inventory.set_slot(
        0U,
        arv::player::ItemStack{
            item_registry.find_item("arvane:dirt").value(),
            count}));
  }

  void give_player_item(arv::usize slot, std::string_view item_name, arv::u16 count) {
    arv::player::PlayerState& player = arv::testsupport::connected_player(server);
    ASSERT_TRUE(player.inventory.set_slot(
        slot,
        arv::player::ItemStack{
            item_registry.find_item(item_name).value(),
            count}));
  }

  void seed_furnace(
      std::string_view input_name,
      arv::u16 input_count,
      std::string_view fuel_name,
      arv::u16 fuel_count,
      std::optional<arv::player::ItemStack> output = std::nullopt) {
    arv::world::BlockAttachedContainer* container =
        server.voxel_world().ensure_block_container(
            furnace_pos,
            "arvane:furnace",
            3U);
    ASSERT_NE(container, nullptr);
    if (input_count > 0U) {
      ASSERT_TRUE(container->inventory.set_slot(
          0U,
          arv::player::ItemStack{
              item_registry.find_item(input_name).value(),
              input_count}));
    } else {
      ASSERT_TRUE(container->inventory.clear_slot(0U));
    }
    if (fuel_count > 0U) {
      ASSERT_TRUE(container->inventory.set_slot(
          1U,
          arv::player::ItemStack{
              item_registry.find_item(fuel_name).value(),
              fuel_count}));
    } else {
      ASSERT_TRUE(container->inventory.clear_slot(1U));
    }
    ASSERT_TRUE(container->inventory.set_slot(2U, std::move(output)));
    container->furnace.active_cycle.reset();
  }

  void seed_active_furnace_cycle(
      std::string_view input_name,
      arv::u16 input_count,
      std::string_view output_name,
      arv::u16 output_count,
      arv::Seconds duration_secs,
      arv::Seconds start_at) {
    arv::world::BlockAttachedContainer* container =
        server.voxel_world().ensure_block_container(
            furnace_pos,
            "arvane:furnace",
            3U);
    ASSERT_NE(container, nullptr);
    container->furnace.active_cycle = arv::world::FurnaceCycleState{
        item_registry.find_item(input_name).value(),
        input_count,
        item_registry.find_item(output_name).value(),
        output_count,
        duration_secs,
        start_at};
  }

  void open_chest() {
    ASSERT_TRUE(client.request_open_container_block(server.voxel_world().id(), chest_pos));
    tick_round_trip();
    ASSERT_TRUE(client.container_open());
  }

  void open_furnace() {
    ASSERT_TRUE(client.request_open_container_block(server.voxel_world().id(), furnace_pos));
    tick_round_trip();
    ASSERT_TRUE(client.container_open());
  }

  void advance_seconds(arv::u32 seconds) {
    for (arv::u32 tick = 0; tick < seconds; ++tick) {
      tick_round_trip();
    }
  }
};

struct MultiClientContainerRuntimeFixture {
  arv::voxel::BlockRegistry block_registry{make_container_block_registry()};
  arv::player::ItemRegistry item_registry{
      make_container_item_registry(block_registry)};
  arv::crafting::SmeltingRuleRegistry smelting_registry{
      make_smelting_registry(item_registry)};
  arv::player::ContainerTypeRegistry container_types{make_container_type_registry()};
  arv::player::ContainerUiLayoutRegistry container_layouts{
      make_container_layout_registry()};
  std::shared_ptr<MultiClientSharedState> shared{
      std::make_shared<MultiClientSharedState>()};
  arv::server::ServerRuntime server{
      MultiClientInProcessTransport::make_server(shared)};
  arv::client::ClientRuntime client_a{
      MultiClientInProcessTransport::make_client(shared)};
  arv::client::ClientRuntime client_b{
      MultiClientInProcessTransport::make_client(shared)};
  arv::client::ClientRuntime client_breaker{
      MultiClientInProcessTransport::make_client(shared)};
  arv::BlockPos chest_pos{{3, 1, 0}};

  MultiClientContainerRuntimeFixture() {
    arv::server::ServerStartInfo start_info;
    start_info.mode = arv::server::ServerMode::Integrated;
    start_info.base_tps = arv::DefaultBaseTps;
    start_info.listen_address = {"in-process", 0};
    start_info.block_registry = &block_registry;
    start_info.item_registry = &item_registry;
    start_info.smelting_registry = &smelting_registry;
    start_info.container_types = &container_types;
    start_info.container_layouts = &container_layouts;
    start_info.block_breaking.base_seconds_per_hardness = 0.0F;
    EXPECT_TRUE(server.start(start_info));

    client_a.set_local_identity(
        arv::client::make_ephemeral_local_identity("ViewerA"));
    client_b.set_local_identity(
        arv::client::make_ephemeral_local_identity("ViewerB"));
    client_breaker.set_local_identity(
        arv::client::make_ephemeral_local_identity("Breaker"));
    EXPECT_TRUE(client_a.connect_integrated());
    EXPECT_TRUE(client_b.connect_integrated());
    EXPECT_TRUE(client_breaker.connect_integrated());
    complete_handshakes();

    (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
    const arv::voxel::BlockVariantID chest =
        block_registry.find_variant("arvane:chest[facing=north]").value();
    EXPECT_TRUE(server.voxel_world().set_block(chest_pos, chest));
    position_players_for_chest();
  }

  void complete_handshakes() {
    for (int i = 0; i < 8; ++i) {
      tick_round_trip();
      if (client_a.connection_state() == arv::net::ConnectionState::InGame &&
          client_b.connection_state() == arv::net::ConnectionState::InGame &&
          client_breaker.connection_state() == arv::net::ConnectionState::InGame) {
        break;
      }
    }
    ASSERT_EQ(client_a.connection_state(), arv::net::ConnectionState::InGame);
    ASSERT_EQ(client_b.connection_state(), arv::net::ConnectionState::InGame);
    ASSERT_EQ(client_breaker.connection_state(), arv::net::ConnectionState::InGame);
  }

  void tick_round_trip() {
    server.tick();
    client_a.fixed_tick(server.tick_index());
    client_b.fixed_tick(server.tick_index());
    client_breaker.fixed_tick(server.tick_index());
  }

  [[nodiscard]] arv::player::PlayerState& player_for(
      const arv::client::ClientRuntime& client) {
    arv::player::PlayerState* player_state = nullptr;
    server.sessions().for_each_session(
        [this, &client, &player_state](const arv::server::ServerSession& session) {
          if (session.profile_id == client.local_identity().profile_id) {
            player_state = server.voxel_world().find_player(session.player_id);
          }
        });
    EXPECT_NE(player_state, nullptr);
    return *player_state;
  }

  void position_players_for_chest() {
    server.sessions().for_each_session(
        [this](const arv::server::ServerSession& session) {
          arv::player::PlayerState* player =
              server.voxel_world().find_player(session.player_id);
          ASSERT_NE(player, nullptr);
          player->position = {0.5F, 0.0F, 0.5F};
          player->velocity = {};
          player->yaw_radians = std::numbers::pi_v<float> * 0.5F;
          player->pitch_radians = 0.0F;
          player->grounded = false;
          player->selected_hotbar_slot = 0U;
        });
  }

  void give_player_dirt(arv::client::ClientRuntime& client, arv::u16 count) {
    arv::player::PlayerState& player = player_for(client);
    ASSERT_TRUE(player.inventory.set_slot(
        0U,
        arv::player::ItemStack{
            item_registry.find_item("arvane:dirt").value(),
            count}));
  }

  void open_chest(arv::client::ClientRuntime& client) {
    ASSERT_TRUE(client.request_open_container_block(server.voxel_world().id(), chest_pos));
    tick_round_trip();
    ASSERT_TRUE(client.container_open());
  }
};

} // namespace

TEST(ContainerRuntimeTests, OpenAndShiftTransferUseServerAuthoritativeBlockContainer) {
  ContainerRuntimeFixture fixture;
  fixture.give_player_dirt(12U);
  fixture.open_chest();

  EXPECT_EQ(fixture.client.active_container_type(), "arvane:chest");
  EXPECT_EQ(fixture.client.active_container_title(), "Chest");
  EXPECT_EQ(fixture.client.active_container_layout(), "arvane:chest");
  EXPECT_EQ(fixture.client.active_container_inventory().slot_count(), 27U);

  ASSERT_TRUE(fixture.client.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U));
  fixture.tick_round_trip();

  const std::optional<arv::player::ItemStack>& client_chest_stack =
      fixture.client.active_container_inventory().slot(0U);
  ASSERT_TRUE(client_chest_stack.has_value());
  EXPECT_EQ(
      client_chest_stack->item,
      fixture.item_registry.find_item("arvane:dirt").value());
  EXPECT_EQ(client_chest_stack->count, 12U);
  EXPECT_FALSE(fixture.client.inventory().slot(0U).has_value());

  const arv::world::BlockAttachedContainer* server_container =
      fixture.server.voxel_world().find_block_container(fixture.chest_pos);
  ASSERT_NE(server_container, nullptr);
  ASSERT_TRUE(server_container->inventory.slot(0U).has_value());
  EXPECT_EQ(server_container->inventory.slot(0U)->count, 12U);

  ASSERT_TRUE(fixture.client.request_inventory_close());
  fixture.tick_round_trip();
  EXPECT_FALSE(fixture.client.container_open());
  fixture.open_chest();
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(0U)->count, 12U);
}

TEST(ContainerRuntimeTests, OpensFurnaceAsThreeSlotContainer) {
  ContainerRuntimeFixture fixture;
  fixture.open_furnace();

  EXPECT_EQ(fixture.client.active_container_type(), "arvane:furnace");
  EXPECT_EQ(fixture.client.active_container_title(), "Furnace");
  EXPECT_EQ(fixture.client.active_container_layout(), "arvane:furnace");
  EXPECT_EQ(fixture.client.active_container_inventory().slot_count(), 3U);
}

TEST(ContainerRuntimeTests, FurnaceStartsCycleAndDoesNotOutputBeforeDuration) {
  ContainerRuntimeFixture fixture;
  fixture.seed_furnace("arvane:iron", 4U, "arvane:coal", 1U);
  fixture.open_furnace();

  EXPECT_FALSE(fixture.client.active_container_inventory().slot(2U).has_value());
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(0U)->count, 1U);

  ASSERT_TRUE(fixture.client.request_inventory_close());
  fixture.tick_round_trip();
  fixture.advance_seconds(5U);
  fixture.open_furnace();

  EXPECT_FALSE(fixture.client.active_container_inventory().slot(2U).has_value());
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(1U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(1U)->count, 1U);
}

TEST(ContainerRuntimeTests, FurnaceCompletesIronAndCopperCyclesLazily) {
  ContainerRuntimeFixture iron_fixture;
  iron_fixture.seed_furnace("arvane:iron", 3U, "arvane:coal", 1U);
  iron_fixture.open_furnace();
  ASSERT_TRUE(iron_fixture.client.request_inventory_close());
  iron_fixture.tick_round_trip();
  iron_fixture.advance_seconds(20U);
  iron_fixture.open_furnace();

  const arv::player::ItemID iron_bar =
      iron_fixture.item_registry.find_item("arvane:iron_bar").value();
  ASSERT_TRUE(iron_fixture.client.active_container_inventory().slot(2U).has_value());
  EXPECT_EQ(iron_fixture.client.active_container_inventory().slot(2U)->item, iron_bar);
  EXPECT_EQ(iron_fixture.client.active_container_inventory().slot(2U)->count, 1U);
  EXPECT_FALSE(iron_fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_FALSE(iron_fixture.client.active_container_inventory().slot(1U).has_value());

  ContainerRuntimeFixture copper_fixture;
  copper_fixture.seed_furnace("arvane:copper", 3U, "arvane:coal", 1U);
  copper_fixture.open_furnace();
  ASSERT_TRUE(copper_fixture.client.request_inventory_close());
  copper_fixture.tick_round_trip();
  copper_fixture.advance_seconds(10U);
  copper_fixture.open_furnace();

  const arv::player::ItemID copper_bar =
      copper_fixture.item_registry.find_item("arvane:copper_bar").value();
  ASSERT_TRUE(copper_fixture.client.active_container_inventory().slot(2U).has_value());
  EXPECT_EQ(copper_fixture.client.active_container_inventory().slot(2U)->item, copper_bar);
  EXPECT_EQ(copper_fixture.client.active_container_inventory().slot(2U)->count, 1U);
  EXPECT_FALSE(copper_fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_FALSE(copper_fixture.client.active_container_inventory().slot(1U).has_value());
}

TEST(ContainerRuntimeTests, FurnaceProcessesMultipleElapsedCycles) {
  ContainerRuntimeFixture fixture;
  fixture.seed_furnace("arvane:iron", 9U, "arvane:coal", 3U);
  fixture.open_furnace();
  ASSERT_TRUE(fixture.client.request_inventory_close());
  fixture.tick_round_trip();
  fixture.advance_seconds(60U);
  fixture.open_furnace();

  const arv::player::ItemID iron_bar =
      fixture.item_registry.find_item("arvane:iron_bar").value();
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(2U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(2U)->item, iron_bar);
  EXPECT_EQ(fixture.client.active_container_inventory().slot(2U)->count, 3U);
  EXPECT_FALSE(fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_FALSE(fixture.client.active_container_inventory().slot(1U).has_value());
}

TEST(ContainerRuntimeTests, FurnaceOutputCapacityBlocksCompletionWithoutItemLoss) {
  ContainerRuntimeFixture fixture;
  const arv::player::ItemID iron_bar =
      fixture.item_registry.find_item("arvane:iron_bar").value();
  fixture.seed_furnace(
      "arvane:iron",
      0U,
      "arvane:coal",
      1U,
      arv::player::ItemStack{iron_bar, 64U});
  fixture.seed_active_furnace_cycle(
      "arvane:iron",
      3U,
      "arvane:iron_bar",
      1U,
      20.0F,
      0.0F);
  fixture.advance_seconds(25U);
  fixture.open_furnace();

  ASSERT_TRUE(fixture.client.active_container_inventory().slot(2U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(2U)->count, 64U);
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(1U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(1U)->count, 1U);
  const arv::world::BlockAttachedContainer* server_container =
      fixture.server.voxel_world().find_block_container(fixture.furnace_pos);
  ASSERT_NE(server_container, nullptr);
  EXPECT_TRUE(server_container->furnace.active_cycle.has_value());
}

TEST(ContainerRuntimeTests, FurnaceDoesNotStartWithoutEnoughInputOrFuel) {
  ContainerRuntimeFixture missing_input;
  missing_input.seed_furnace("arvane:iron", 2U, "arvane:coal", 1U);
  missing_input.open_furnace();
  ASSERT_TRUE(missing_input.client.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(missing_input.client.active_container_inventory().slot(0U)->count, 2U);
  EXPECT_FALSE(missing_input.client.active_container_inventory().slot(2U).has_value());

  ContainerRuntimeFixture missing_fuel;
  missing_fuel.seed_furnace("arvane:iron", 3U, "arvane:coal", 0U);
  missing_fuel.open_furnace();
  ASSERT_TRUE(missing_fuel.client.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(missing_fuel.client.active_container_inventory().slot(0U)->count, 3U);
  EXPECT_FALSE(missing_fuel.client.active_container_inventory().slot(2U).has_value());
}

TEST(ContainerRuntimeTests, FurnaceOutputSlotRejectsPlayerInput) {
  ContainerRuntimeFixture fixture;
  fixture.give_player_dirt(12U);
  fixture.open_furnace();

  ASSERT_TRUE(fixture.client.request_container_slot_click(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U,
      arv::player::InventoryClickButton::Primary));
  fixture.tick_round_trip();
  ASSERT_TRUE(fixture.client.inventory_cursor_stack().has_value());
  EXPECT_FALSE(fixture.client.inventory().slot(0U).has_value());

  ASSERT_TRUE(fixture.client.request_container_slot_click(
      arv::player::ContainerSlotSource::OpenedContainer,
      2U,
      arv::player::InventoryClickButton::Primary));
  fixture.tick_round_trip();

  EXPECT_FALSE(fixture.client.active_container_inventory().slot(2U).has_value());
  ASSERT_TRUE(fixture.client.inventory_cursor_stack().has_value());
  EXPECT_EQ(fixture.client.inventory_cursor_stack()->count, 12U);
}

TEST(ContainerRuntimeTests, FurnaceSmeltsAfterPlayerInsertsInputAndFuel) {
  ContainerRuntimeFixture fixture;
  fixture.give_player_item(0U, "arvane:iron", 3U);
  fixture.give_player_item(1U, "arvane:coal", 1U);
  fixture.open_furnace();

  ASSERT_TRUE(fixture.client.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U));
  fixture.tick_round_trip();
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(0U)->count, 3U);

  ASSERT_TRUE(fixture.client.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      1U));
  fixture.tick_round_trip();
  EXPECT_FALSE(fixture.client.active_container_inventory().slot(0U).has_value());
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(1U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(1U)->count, 1U);

  ASSERT_TRUE(fixture.client.request_inventory_close());
  fixture.tick_round_trip();
  fixture.advance_seconds(20U);
  fixture.open_furnace();

  const arv::player::ItemID iron_bar =
      fixture.item_registry.find_item("arvane:iron_bar").value();
  ASSERT_TRUE(fixture.client.active_container_inventory().slot(2U).has_value());
  EXPECT_EQ(fixture.client.active_container_inventory().slot(2U)->item, iron_bar);
  EXPECT_EQ(fixture.client.active_container_inventory().slot(2U)->count, 1U);
}

TEST(ContainerRuntimeTests, ChestPlacementResolvesFacingFromPlayerLook) {
  ContainerRuntimeFixture fixture;
  const arv::player::ItemID chest_item =
      fixture.item_registry.find_item("arvane:chest").value();
  arv::player::PlayerState& player =
      arv::testsupport::connected_player(fixture.server);
  ASSERT_TRUE(player.inventory.set_slot(
      0U,
      arv::player::ItemStack{chest_item, 1U}));

  fixture.client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  fixture.client.fixed_tick(fixture.server.tick_index());
  fixture.server.tick();

  EXPECT_EQ(
      fixture.server.voxel_world().get_block({{2, 1, 0}}),
      fixture.block_registry.find_variant("arvane:chest[facing=east]").value());
}

TEST(ContainerRuntimeTests, BreakDropsContentsAndRemovesBlockAttachedData) {
  ContainerRuntimeFixture fixture;
  fixture.give_player_dirt(12U);
  fixture.open_chest();
  ASSERT_TRUE(fixture.client.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U));
  fixture.tick_round_trip();

  ASSERT_TRUE(fixture.client.request_inventory_close());
  fixture.tick_round_trip();
  EXPECT_FALSE(fixture.client.container_open());

  fixture.position_player_for_chest();
  fixture.client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  fixture.client.fixed_tick(fixture.server.tick_index());
  fixture.server.tick();
  fixture.client.fixed_tick(fixture.server.tick_index());

  EXPECT_EQ(
      fixture.server.voxel_world().get_block(fixture.chest_pos),
      arv::voxel::AirBlockVariantID);
  EXPECT_EQ(
      fixture.server.voxel_world().find_block_container(fixture.chest_pos),
      nullptr);

  const arv::player::ItemID dirt_item =
      fixture.item_registry.find_item("arvane:dirt").value();
  const std::vector<arv::server::DroppedItemStack> drops =
      fixture.server.dropped_item_stacks();
  const auto dirt_drop = std::find_if(
      drops.begin(),
      drops.end(),
      [dirt_item](const arv::server::DroppedItemStack& drop) {
        return drop.stack.item == dirt_item;
      });
  ASSERT_NE(dirt_drop, drops.end());
  EXPECT_EQ(dirt_drop->stack.count, 12U);
}

TEST(ContainerRuntimeTests, StaleActionClosesWhenTargetBlockIsRemoved) {
  ContainerRuntimeFixture fixture;
  fixture.give_player_dirt(12U);
  fixture.open_chest();

  ASSERT_TRUE(fixture.server.voxel_world().set_block(
      fixture.chest_pos,
      arv::voxel::AirBlockVariantID));
  ASSERT_TRUE(fixture.client.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U));
  fixture.tick_round_trip();

  EXPECT_FALSE(fixture.client.container_open());
  const arv::player::PlayerState& player =
      arv::testsupport::connected_player(fixture.server);
  ASSERT_TRUE(player.inventory.slot(0U).has_value());
  EXPECT_EQ(player.inventory.slot(0U)->count, 12U);
  const arv::world::BlockAttachedContainer* container =
      fixture.server.voxel_world().find_block_container(fixture.chest_pos);
  ASSERT_NE(container, nullptr);
  EXPECT_FALSE(container->inventory.slot(0U).has_value());
}

TEST(ContainerRuntimeTests, MultiViewerActionsSyncAndBreakClosesAllViewers) {
  MultiClientContainerRuntimeFixture fixture;
  fixture.give_player_dirt(fixture.client_a, 12U);
  fixture.open_chest(fixture.client_a);
  fixture.open_chest(fixture.client_b);

  ASSERT_TRUE(fixture.client_a.request_container_shift_transfer(
      arv::player::ContainerSlotSource::PlayerInventory,
      0U));
  fixture.tick_round_trip();

  ASSERT_TRUE(fixture.client_a.active_container_inventory().slot(0U).has_value());
  ASSERT_TRUE(fixture.client_b.active_container_inventory().slot(0U).has_value());
  EXPECT_EQ(fixture.client_a.active_container_inventory().slot(0U)->count, 12U);
  EXPECT_EQ(fixture.client_b.active_container_inventory().slot(0U)->count, 12U);
  EXPECT_FALSE(fixture.client_a.inventory().slot(0U).has_value());

  fixture.position_players_for_chest();
  fixture.client_breaker.submit_input(arv::client::InputFrame{.primary_pressed = true});
  fixture.client_breaker.fixed_tick(fixture.server.tick_index());
  fixture.server.tick();
  fixture.client_a.fixed_tick(fixture.server.tick_index());
  fixture.client_b.fixed_tick(fixture.server.tick_index());
  fixture.client_breaker.fixed_tick(fixture.server.tick_index());

  EXPECT_FALSE(fixture.client_a.container_open());
  EXPECT_FALSE(fixture.client_b.container_open());
  EXPECT_EQ(
      fixture.server.voxel_world().get_block(fixture.chest_pos),
      arv::voxel::AirBlockVariantID);
  EXPECT_EQ(
      fixture.server.voxel_world().find_block_container(fixture.chest_pos),
      nullptr);

  const arv::player::ItemID dirt_item =
      fixture.item_registry.find_item("arvane:dirt").value();
  const std::vector<arv::server::DroppedItemStack> drops =
      fixture.server.dropped_item_stacks();
  const auto dirt_drop = std::find_if(
      drops.begin(),
      drops.end(),
      [dirt_item](const arv::server::DroppedItemStack& drop) {
        return drop.stack.item == dirt_item;
      });
  ASSERT_NE(dirt_drop, drops.end());
  EXPECT_EQ(dirt_drop->stack.count, 12U);
}
