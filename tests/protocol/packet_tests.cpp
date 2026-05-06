#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_registry.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/world_join.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

TEST(ProtocolTests, PacketEnvelopeRoundTripsPayload) {
  arv::protocol::MessageWriter payload_writer;
  payload_writer.write_string("hello");
  const std::vector<arv::u8> payload = std::move(payload_writer).take_bytes();

  const std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, arv::protocol::MessageType::Ping, 0, 17, 3, 0},
      payload);

  const auto decoded = arv::protocol::deserialize_envelope(packet);
  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().envelope.message_type, arv::protocol::MessageType::Ping);
  EXPECT_EQ(decoded.value().envelope.sequence, 17U);
  EXPECT_EQ(decoded.value().envelope.ack_reference, 3U);

  arv::protocol::MessageReader payload_reader(decoded.value().payload);
  const auto text = payload_reader.read_string();
  ASSERT_TRUE(text);
  EXPECT_EQ(text.value(), "hello");
}

TEST(ProtocolTests, RejectsPayloadSizeMismatch) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, arv::protocol::MessageType::Ping, 0, 1, 0, 0},
      {});
  packet.push_back(0xFF);

  const auto decoded = arv::protocol::deserialize_envelope(packet);
  EXPECT_FALSE(decoded);
}

TEST(ProtocolTests, CoreMessageRegistryHasDeclaredReliability) {
  const auto registry = arv::protocol::MessageRegistry::make_core_registry();
  const auto ping = registry.lookup(arv::protocol::MessageType::Ping);
  const auto input = registry.lookup(arv::protocol::MessageType::ClientInputBatch);
  const auto correction = registry.lookup(arv::protocol::MessageType::PlayerCorrection);
  const auto auth_challenge = registry.lookup(arv::protocol::MessageType::AuthChallenge);
  const auto auth_response = registry.lookup(arv::protocol::MessageType::AuthResponse);
  const auto auth_result = registry.lookup(arv::protocol::MessageType::AuthResult);
  const auto chunk_interest = registry.lookup(arv::protocol::MessageType::ChunkInterestUpdate);
  const auto chunk_data = registry.lookup(arv::protocol::MessageType::ChunkSectionData);
  const auto chunk_unload = registry.lookup(arv::protocol::MessageType::ChunkSectionUnload);
  const auto world_join = registry.lookup(arv::protocol::MessageType::WorldJoinResult);
  const auto block_updates = registry.lookup(arv::protocol::MessageType::BlockUpdateBatch);
  const auto entity_spawn = registry.lookup(arv::protocol::MessageType::EntitySpawn);
  const auto entity_snapshot = registry.lookup(arv::protocol::MessageType::EntitySnapshot);
  const auto dropped_item_snapshot =
      registry.lookup(arv::protocol::MessageType::DroppedItemSnapshot);
  const auto inventory_action =
      registry.lookup(arv::protocol::MessageType::InventoryActionRequest);
  const auto inventory_update = registry.lookup(arv::protocol::MessageType::InventoryUpdate);

  ASSERT_TRUE(ping.has_value());
  ASSERT_TRUE(input.has_value());
  ASSERT_TRUE(correction.has_value());
  ASSERT_TRUE(auth_challenge.has_value());
  ASSERT_TRUE(auth_response.has_value());
  ASSERT_TRUE(auth_result.has_value());
  ASSERT_TRUE(chunk_interest.has_value());
  ASSERT_TRUE(chunk_data.has_value());
  ASSERT_TRUE(chunk_unload.has_value());
  ASSERT_TRUE(world_join.has_value());
  ASSERT_TRUE(block_updates.has_value());
  ASSERT_TRUE(entity_spawn.has_value());
  ASSERT_TRUE(entity_snapshot.has_value());
  ASSERT_TRUE(dropped_item_snapshot.has_value());
  ASSERT_TRUE(inventory_action.has_value());
  ASSERT_TRUE(inventory_update.has_value());
  EXPECT_EQ(ping->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(input->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(correction->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(auth_challenge->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(auth_response->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(auth_result->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(chunk_interest->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(chunk_data->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(chunk_unload->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(world_join->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(block_updates->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(entity_spawn->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(entity_snapshot->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(dropped_item_snapshot->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(inventory_action->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(inventory_update->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(ping->lane, arv::protocol::NetworkLane::Debug);
  EXPECT_EQ(input->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(correction->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(chunk_interest->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(chunk_data->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(chunk_unload->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(world_join->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(block_updates->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(entity_spawn->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(entity_snapshot->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(dropped_item_snapshot->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(inventory_action->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(inventory_update->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(registry.size(), 22U);
}

TEST(ProtocolTests, ClientInputBatchRoundTripsThroughSerialization) {
  arv::protocol::ClientInputBatch batch;
  batch.first_input_tick = 44;
  batch.inputs.push_back({
      arv::protocol::ClientInputButtons::JumpPressed |
          arv::protocol::ClientInputButtons::PrimaryPressed,
      -1,
      1,
      12,
      -8,
      3});

  const std::vector<arv::u8> bytes = arv::protocol::serialize_client_input_batch(batch);
  const auto decoded = arv::protocol::deserialize_client_input_batch(bytes);

  ASSERT_TRUE(decoded);
  ASSERT_EQ(decoded.value().inputs.size(), 1U);
  EXPECT_EQ(decoded.value().first_input_tick, 44U);
  EXPECT_EQ(decoded.value().inputs[0].move_x, -1);
  EXPECT_EQ(decoded.value().inputs[0].move_z, 1);
  EXPECT_EQ(decoded.value().inputs[0].look_delta_x, 12);
  EXPECT_EQ(decoded.value().inputs[0].look_delta_y, -8);
  EXPECT_EQ(decoded.value().inputs[0].selected_hotbar_slot, 3U);
  EXPECT_TRUE(arv::protocol::has_button(
      decoded.value().inputs[0].buttons,
      arv::protocol::ClientInputButtons::JumpPressed));
  EXPECT_TRUE(arv::protocol::has_button(
      decoded.value().inputs[0].buttons,
      arv::protocol::ClientInputButtons::PrimaryPressed));
}

TEST(ProtocolTests, PlayerCorrectionRoundTripsThroughSerialization) {
  arv::protocol::PlayerCorrection correction;
  correction.server_tick = 55;
  correction.player_id = arv::PlayerID{7};
  correction.world_id = arv::WorldID{9};
  correction.position = {1.0F, 2.0F, 3.0F};
  correction.velocity = {0.5F, -1.0F, 2.5F};
  correction.yaw_radians = 0.75F;
  correction.pitch_radians = -0.25F;
  correction.movement_mode = arv::player::MovementMode::Flying;
  correction.game_mode_id = std::string{arv::player::CreativeGameModeID};
  correction.resolved_core_permissions.can_fly = true;
  correction.resolved_core_permissions.has_creative_inventory = true;
  correction.grounded = false;
  correction.sneaking = true;
  correction.swimming = true;
  correction.breath_seconds = 12.5F;
  correction.selected_hotbar_slot = 6;
  correction.block_break.active = true;
  correction.block_break.block = {{4, 5, 6}};
  correction.block_break.variant = arv::voxel::BlockVariantID{3};
  correction.block_break.progress = 0.42F;
  correction.block_break.stage = 4;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_player_correction(correction);
  const auto decoded = arv::protocol::deserialize_player_correction(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().server_tick, 55U);
  EXPECT_EQ(decoded.value().player_id, (arv::PlayerID{7}));
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{9}));
  EXPECT_EQ(decoded.value().position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(decoded.value().velocity, (arv::Vec3f{0.5F, -1.0F, 2.5F}));
  EXPECT_FLOAT_EQ(decoded.value().yaw_radians, 0.75F);
  EXPECT_FLOAT_EQ(decoded.value().pitch_radians, -0.25F);
  EXPECT_EQ(decoded.value().movement_mode, arv::player::MovementMode::Flying);
  EXPECT_EQ(decoded.value().game_mode_id, arv::player::CreativeGameModeID);
  EXPECT_TRUE(decoded.value().resolved_core_permissions.can_fly);
  EXPECT_TRUE(decoded.value().resolved_core_permissions.has_creative_inventory);
  EXPECT_FALSE(decoded.value().grounded);
  EXPECT_TRUE(decoded.value().sneaking);
  EXPECT_TRUE(decoded.value().swimming);
  EXPECT_FLOAT_EQ(decoded.value().breath_seconds, 12.5F);
  EXPECT_EQ(decoded.value().selected_hotbar_slot, 6U);
  EXPECT_TRUE(decoded.value().block_break.active);
  EXPECT_EQ(decoded.value().block_break.block, (arv::BlockPos{{4, 5, 6}}));
  EXPECT_EQ(decoded.value().block_break.variant, (arv::voxel::BlockVariantID{3}));
  EXPECT_FLOAT_EQ(decoded.value().block_break.progress, 0.42F);
  EXPECT_EQ(decoded.value().block_break.stage, 4U);
}

TEST(ProtocolTests, ChunkInterestUpdateRoundTripsThroughSerialization) {
  arv::protocol::ChunkInterestUpdate update;
  update.world_id = arv::WorldID{3};
  update.center_chunk = {{4, 5, -6}};
  update.client_view_distance = 7;
  update.previous_center_chunk = arv::ChunkSectionCoord{{3, 5, -6}};
  update.sequence_id = 42;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_interest_update(update);
  const auto decoded = arv::protocol::deserialize_chunk_interest_update(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{3}));
  EXPECT_EQ(decoded.value().center_chunk, (arv::ChunkSectionCoord{{4, 5, -6}}));
  EXPECT_EQ(decoded.value().client_view_distance, 7);
  ASSERT_TRUE(decoded.value().previous_center_chunk.has_value());
  EXPECT_EQ(decoded.value().previous_center_chunk.value(), (arv::ChunkSectionCoord{{3, 5, -6}}));
  EXPECT_EQ(decoded.value().sequence_id, 42U);
}

TEST(ProtocolTests, ChunkSectionDataRoundTripsThroughSerialization) {
  arv::protocol::ChunkSectionData data;
  data.world_id = arv::WorldID{5};
  data.coord = {{1, -2, 3}};
  data.chunk_version = 12;
  data.chunk_content_version = 9;
  data.chunk_light_version = 14;
  data.has_lighting = true;
  data.lighting_valid = true;
  data.has_fluids = true;
  data.blocks.fill(arv::voxel::AirBlockVariantID);
  data.lights.fill(arv::voxel::NoLight);
  data.fluids.fill(arv::voxel::empty_fluid_cell());
  data.blocks[arv::voxel::local_index({{1, 2, 3}})] = arv::voxel::BlockVariantID{8};
  data.lights[arv::voxel::local_index({{1, 2, 3}})] = arv::voxel::pack_light(1, 2, 3, 4);
  data.fluids[arv::voxel::local_index({{1, 2, 3}})] =
      arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition());

  const std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_section_data(data);
  const auto decoded = arv::protocol::deserialize_chunk_section_data(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{5}));
  EXPECT_EQ(decoded.value().coord, (arv::ChunkSectionCoord{{1, -2, 3}}));
  EXPECT_EQ(decoded.value().chunk_version, 12U);
  EXPECT_EQ(decoded.value().chunk_content_version, 9U);
  EXPECT_EQ(decoded.value().chunk_light_version, 14U);
  EXPECT_TRUE(decoded.value().has_lighting);
  EXPECT_TRUE(decoded.value().lighting_valid);
  EXPECT_TRUE(decoded.value().has_fluids);
  EXPECT_EQ(
      decoded.value().blocks[arv::voxel::local_index({{1, 2, 3}})],
      (arv::voxel::BlockVariantID{8}));
  EXPECT_EQ(
      decoded.value().lights[arv::voxel::local_index({{1, 2, 3}})],
      arv::voxel::pack_light(1, 2, 3, 4));
  EXPECT_TRUE(decoded.value().fluids[arv::voxel::local_index({{1, 2, 3}})].source());
}

TEST(ProtocolTests, ChunkSectionUnloadRoundTripsThroughSerialization) {
  const arv::protocol::ChunkSectionUnload unload{arv::WorldID{2}, {{-1, 0, 1}}};

  const std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_section_unload(unload);
  const auto decoded = arv::protocol::deserialize_chunk_section_unload(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{2}));
  EXPECT_EQ(decoded.value().coord, (arv::ChunkSectionCoord{{-1, 0, 1}}));
}

TEST(ProtocolTests, WorldJoinAndLeaveRoundTripThroughSerialization) {
  arv::protocol::WorldJoinResult result;
  result.accepted = true;
  result.world_id = arv::WorldID{4};
  result.player_id = arv::PlayerID{8};
  result.local_entity_id = arv::EntityNetID{12};
  result.spawn_position = {1.0F, 2.0F, 3.0F};
  result.initial_tick = 99;
  result.tick_rate = 20;
  result.effective_view_distance = 6;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_world_join_result(result);
  const auto decoded = arv::protocol::deserialize_world_join_result(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_TRUE(decoded.value().accepted);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{4}));
  EXPECT_EQ(decoded.value().player_id, (arv::PlayerID{8}));
  EXPECT_EQ(decoded.value().local_entity_id, (arv::EntityNetID{12}));
  EXPECT_EQ(decoded.value().spawn_position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(decoded.value().initial_tick, 99U);
  EXPECT_EQ(decoded.value().tick_rate, 20U);
  EXPECT_EQ(decoded.value().effective_view_distance, 6);

  const arv::protocol::WorldLeave leave{arv::WorldID{4}, 3};
  const auto decoded_leave =
      arv::protocol::deserialize_world_leave(arv::protocol::serialize_world_leave(leave));
  ASSERT_TRUE(decoded_leave);
  EXPECT_EQ(decoded_leave.value().world_id, (arv::WorldID{4}));
  EXPECT_EQ(decoded_leave.value().reason, 3U);
}

TEST(ProtocolTests, BlockUpdateBatchRoundTripsAndRejectsMalformedCounts) {
  arv::protocol::BlockUpdateBatch batch;
  batch.world_id = arv::WorldID{5};
  batch.server_tick = 123;
  batch.updates.push_back({
      arv::BlockPos{{1, -2, 3}},
      arv::voxel::BlockVariantID{9},
      7,
      120});

  const std::vector<arv::u8> bytes = arv::protocol::serialize_block_update_batch(batch);
  const auto decoded = arv::protocol::deserialize_block_update_batch(bytes);

  ASSERT_TRUE(decoded);
  ASSERT_EQ(decoded.value().updates.size(), 1U);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{5}));
  EXPECT_EQ(decoded.value().server_tick, 123U);
  EXPECT_EQ(decoded.value().updates.front().pos, (arv::BlockPos{{1, -2, 3}}));
  EXPECT_EQ(decoded.value().updates.front().new_state, (arv::voxel::BlockVariantID{9}));
  EXPECT_EQ(decoded.value().updates.front().update_flags, 7U);
  EXPECT_EQ(decoded.value().updates.front().server_tick, 120U);

  arv::protocol::MessageWriter malformed;
  malformed.write<arv::u32>(5);
  malformed.write<arv::u64>(123);
  malformed.write<arv::u32>(arv::protocol::MaxBlockUpdatesPerBatch + 1U);
  EXPECT_FALSE(arv::protocol::deserialize_block_update_batch(
      std::move(malformed).take_bytes()));
}

TEST(ProtocolTests, EntityReplicationMessagesRoundTripAndRejectMalformedCounts) {
  arv::protocol::EntitySpawn spawn;
  spawn.world_id = arv::WorldID{6};
  spawn.entity_id = arv::EntityNetID{22};
  spawn.entity_type = arv::EntityTypeID{2};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {3.0F, 4.0F, 5.0F};
  spawn.velocity = {0.25F, 0.5F, 0.75F};
  spawn.rotation = {0.0F, 0.1F, 0.2F, 0.9F};
  spawn.spawn_flags = 11;
  spawn.owning_player = arv::PlayerID{9};

  const auto decoded_spawn =
      arv::protocol::deserialize_entity_spawn(arv::protocol::serialize_entity_spawn(spawn));
  ASSERT_TRUE(decoded_spawn);
  EXPECT_EQ(decoded_spawn.value().world_id, (arv::WorldID{6}));
  EXPECT_EQ(decoded_spawn.value().entity_id, (arv::EntityNetID{22}));
  EXPECT_EQ(decoded_spawn.value().entity_type, (arv::EntityTypeID{2}));
  EXPECT_EQ(decoded_spawn.value().entity_type_name, "arvane:test_entity");
  EXPECT_EQ(decoded_spawn.value().position, (arv::Vec3f{3.0F, 4.0F, 5.0F}));
  EXPECT_EQ(decoded_spawn.value().velocity, (arv::Vec3f{0.25F, 0.5F, 0.75F}));
  EXPECT_EQ(decoded_spawn.value().rotation, (arv::Quatf{0.0F, 0.1F, 0.2F, 0.9F}));
  ASSERT_TRUE(decoded_spawn.value().owning_player.has_value());
  EXPECT_EQ(decoded_spawn.value().owning_player.value(), (arv::PlayerID{9}));

  const arv::protocol::EntityDespawn despawn{arv::WorldID{6}, arv::EntityNetID{22}, 4};
  const auto decoded_despawn = arv::protocol::deserialize_entity_despawn(
      arv::protocol::serialize_entity_despawn(despawn));
  ASSERT_TRUE(decoded_despawn);
  EXPECT_EQ(decoded_despawn.value().entity_id, (arv::EntityNetID{22}));
  EXPECT_EQ(decoded_despawn.value().reason, 4U);

  arv::protocol::EntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{6};
  snapshot.server_tick = 77;
  snapshot.entities.push_back({
      arv::EntityNetID{22},
      {6.0F, 7.0F, 8.0F},
      {1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, 0.0F, 1.0F},
      5,
      "walking",
      1.25F});
  const auto decoded_snapshot = arv::protocol::deserialize_entity_snapshot(
      arv::protocol::serialize_entity_snapshot(snapshot));
  ASSERT_TRUE(decoded_snapshot);
  ASSERT_EQ(decoded_snapshot.value().entities.size(), 1U);
  EXPECT_EQ(decoded_snapshot.value().server_tick, 77U);
  EXPECT_EQ(decoded_snapshot.value().entities.front().entity_id, (arv::EntityNetID{22}));
  EXPECT_EQ(decoded_snapshot.value().entities.front().position, (arv::Vec3f{6.0F, 7.0F, 8.0F}));
  EXPECT_EQ(decoded_snapshot.value().entities.front().state_flags, 5U);
  EXPECT_EQ(decoded_snapshot.value().entities.front().animation_name, "walking");
  EXPECT_FLOAT_EQ(decoded_snapshot.value().entities.front().animation_time_seconds, 1.25F);

  arv::protocol::MessageWriter malformed;
  malformed.write<arv::u32>(6);
  malformed.write<arv::u64>(77);
  malformed.write<arv::u32>(arv::protocol::MaxEntitiesPerSnapshot + 1U);
  EXPECT_FALSE(arv::protocol::deserialize_entity_snapshot(
      std::move(malformed).take_bytes()));

  arv::protocol::DroppedItemSnapshot dropped;
  dropped.world_id = arv::WorldID{6};
  dropped.server_tick = 88;
  dropped.drops.push_back({
      arv::EntityNetID{33},
      arv::player::ItemStack{arv::player::ItemID{44}, 3},
      {1.0F, 2.0F, 3.0F},
      {0.1F, 0.2F, 0.3F}});
  const auto decoded_dropped = arv::protocol::deserialize_dropped_item_snapshot(
      arv::protocol::serialize_dropped_item_snapshot(dropped));
  ASSERT_TRUE(decoded_dropped);
  ASSERT_EQ(decoded_dropped.value().drops.size(), 1U);
  EXPECT_EQ(decoded_dropped.value().server_tick, 88U);
  EXPECT_EQ(decoded_dropped.value().drops.front().drop_id, (arv::EntityNetID{33}));
  EXPECT_EQ(decoded_dropped.value().drops.front().stack, (arv::player::ItemStack{arv::player::ItemID{44}, 3}));
  EXPECT_EQ(decoded_dropped.value().drops.front().position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
}

TEST(ProtocolTests, InventorySyncMessagesRoundTripAndRejectMalformedCounts) {
  arv::protocol::InventoryActionRequest request;
  request.inventory_id = arv::protocol::PlayerInventoryID;
  request.client_action_id = 42;
  request.kind = arv::protocol::InventoryActionKind::SlotClick;
  request.slot_index = 3;
  request.button = arv::player::InventoryClickButton::Secondary;

  const auto decoded_request = arv::protocol::deserialize_inventory_action_request(
      arv::protocol::serialize_inventory_action_request(request));
  ASSERT_TRUE(decoded_request);
  EXPECT_EQ(decoded_request.value().inventory_id, arv::protocol::PlayerInventoryID);
  EXPECT_EQ(decoded_request.value().client_action_id, 42U);
  EXPECT_EQ(decoded_request.value().kind, arv::protocol::InventoryActionKind::SlotClick);
  EXPECT_EQ(decoded_request.value().slot_index, 3U);
  EXPECT_EQ(decoded_request.value().button, arv::player::InventoryClickButton::Secondary);

  arv::protocol::MessageWriter invalid_action;
  invalid_action.write<arv::u32>(arv::protocol::PlayerInventoryID.value);
  invalid_action.write<arv::u32>(43);
  invalid_action.write<arv::u8>(255);
  invalid_action.write<arv::u32>(3);
  invalid_action.write<arv::u8>(0);
  EXPECT_FALSE(arv::protocol::deserialize_inventory_action_request(
      std::move(invalid_action).take_bytes()));

  arv::protocol::InventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 7;
  update.selected_hotbar_slot = 2;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 5},
      std::nullopt};
  update.carried_stack = arv::player::ItemStack{arv::player::ItemID{11}, 1};

  const auto decoded_update = arv::protocol::deserialize_inventory_update(
      arv::protocol::serialize_inventory_update(update));
  ASSERT_TRUE(decoded_update);
  EXPECT_EQ(decoded_update.value().revision, 7U);
  EXPECT_EQ(decoded_update.value().selected_hotbar_slot, 2U);
  ASSERT_EQ(decoded_update.value().slots.size(), 2U);
  ASSERT_TRUE(decoded_update.value().slots[0].has_value());
  EXPECT_EQ(decoded_update.value().slots[0]->item, (arv::player::ItemID{10}));
  EXPECT_EQ(decoded_update.value().slots[0]->count, 5U);
  EXPECT_FALSE(decoded_update.value().slots[1].has_value());
  ASSERT_TRUE(decoded_update.value().carried_stack.has_value());
  EXPECT_EQ(decoded_update.value().carried_stack->item, (arv::player::ItemID{11}));

  arv::protocol::MessageWriter malformed;
  malformed.write<arv::u32>(arv::protocol::PlayerInventoryID.value);
  malformed.write<arv::u32>(7);
  malformed.write<arv::u8>(2);
  malformed.write<arv::u32>(arv::protocol::MaxInventorySlots + 1U);
  EXPECT_FALSE(arv::protocol::deserialize_inventory_update(
      std::move(malformed).take_bytes()));
}
