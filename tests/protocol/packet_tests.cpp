#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_registry.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/skins.hpp"
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
  const auto move = registry.lookup(arv::protocol::MessageType::ServerboundPlayerMove);
  const auto correction = registry.lookup(arv::protocol::MessageType::ClientboundPlayerMoveCorrection);
  const auto velocity = registry.lookup(arv::protocol::MessageType::ClientboundPlayerVelocity);
  const auto movement_hz = registry.lookup(arv::protocol::MessageType::ClientboundSetMovementHz);
  const auto break_progress =
      registry.lookup(arv::protocol::MessageType::ClientboundPlayerBlockBreakProgress);
  const auto auth_challenge = registry.lookup(arv::protocol::MessageType::ClientboundAuthChallenge);
  const auto auth_response = registry.lookup(arv::protocol::MessageType::ServerboundAuthResponse);
  const auto auth_result = registry.lookup(arv::protocol::MessageType::ClientboundAuthResult);
  const auto kick = registry.lookup(arv::protocol::MessageType::ClientboundKickPlayer);
  const auto client_keep_alive = registry.lookup(arv::protocol::MessageType::ClientboundKeepAlive);
  const auto server_keep_alive = registry.lookup(arv::protocol::MessageType::ServerboundKeepAlive);
  const auto chunk_interest = registry.lookup(arv::protocol::MessageType::ServerboundChunkInterestUpdate);
  const auto chunk_data = registry.lookup(arv::protocol::MessageType::ClientboundChunkSectionData);
  const auto chunk_unload = registry.lookup(arv::protocol::MessageType::ClientboundChunkSectionUnload);
  const auto world_join = registry.lookup(arv::protocol::MessageType::ClientboundWorldJoinResult);
  const auto block_updates = registry.lookup(arv::protocol::MessageType::ClientboundBlockUpdateBatch);
  const auto entity_spawn = registry.lookup(arv::protocol::MessageType::ClientboundEntitySpawn);
  const auto entity_snapshot = registry.lookup(arv::protocol::MessageType::ClientboundEntitySnapshot);
  const auto dropped_item_snapshot =
      registry.lookup(arv::protocol::MessageType::ClientboundDroppedItemSnapshot);
  const auto attack_entity =
      registry.lookup(arv::protocol::MessageType::ServerboundAttackEntity);
  const auto inventory_action =
      registry.lookup(arv::protocol::MessageType::ServerboundInventoryActionRequest);
  const auto inventory_update = registry.lookup(arv::protocol::MessageType::ClientboundInventoryUpdate);
  const auto chat_send = registry.lookup(arv::protocol::MessageType::ServerboundChatMessage);
  const auto chat_receive = registry.lookup(arv::protocol::MessageType::ClientboundChatMessage);
  const auto skin_rules = registry.lookup(arv::protocol::MessageType::ClientboundSkinRules);
  const auto skin_data = registry.lookup(arv::protocol::MessageType::ClientboundSkinData);
  const auto skin_upload_data = registry.lookup(arv::protocol::MessageType::ServerboundSkinUploadData);

  ASSERT_TRUE(ping.has_value());
  ASSERT_TRUE(move.has_value());
  ASSERT_TRUE(correction.has_value());
  ASSERT_TRUE(velocity.has_value());
  ASSERT_TRUE(movement_hz.has_value());
  ASSERT_TRUE(break_progress.has_value());
  ASSERT_TRUE(auth_challenge.has_value());
  ASSERT_TRUE(auth_response.has_value());
  ASSERT_TRUE(auth_result.has_value());
  ASSERT_TRUE(kick.has_value());
  ASSERT_TRUE(client_keep_alive.has_value());
  ASSERT_TRUE(server_keep_alive.has_value());
  ASSERT_TRUE(chunk_interest.has_value());
  ASSERT_TRUE(chunk_data.has_value());
  ASSERT_TRUE(chunk_unload.has_value());
  ASSERT_TRUE(world_join.has_value());
  ASSERT_TRUE(block_updates.has_value());
  ASSERT_TRUE(entity_spawn.has_value());
  ASSERT_TRUE(entity_snapshot.has_value());
  ASSERT_TRUE(dropped_item_snapshot.has_value());
  ASSERT_TRUE(attack_entity.has_value());
  ASSERT_TRUE(inventory_action.has_value());
  ASSERT_TRUE(inventory_update.has_value());
  ASSERT_TRUE(chat_send.has_value());
  ASSERT_TRUE(chat_receive.has_value());
  ASSERT_TRUE(skin_rules.has_value());
  ASSERT_TRUE(skin_data.has_value());
  ASSERT_TRUE(skin_upload_data.has_value());
  EXPECT_EQ(ping->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(move->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(correction->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(velocity->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(movement_hz->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(break_progress->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(auth_challenge->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(auth_response->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(auth_result->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(kick->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(kick->reliability, arv::protocol::ReliabilityClass::ReliableImmediate);
  EXPECT_EQ(kick->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(client_keep_alive->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(client_keep_alive->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(client_keep_alive->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(server_keep_alive->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(server_keep_alive->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(server_keep_alive->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(chunk_interest->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(chunk_data->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(chunk_unload->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(world_join->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(block_updates->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(entity_spawn->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(entity_snapshot->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(dropped_item_snapshot->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(attack_entity->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(inventory_action->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(inventory_update->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(chat_send->direction, arv::protocol::MessageDirection::ClientToServer);
  EXPECT_EQ(chat_receive->direction, arv::protocol::MessageDirection::ServerToClient);
  EXPECT_EQ(chat_send->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(chat_receive->reliability, arv::protocol::ReliabilityClass::ReliableOrdered);
  EXPECT_EQ(ping->lane, arv::protocol::NetworkLane::Debug);
  EXPECT_EQ(move->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(correction->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(velocity->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(movement_hz->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(break_progress->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(chunk_interest->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(chunk_data->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(chunk_unload->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(world_join->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(block_updates->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(entity_spawn->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(entity_snapshot->lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(dropped_item_snapshot->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(attack_entity->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(inventory_action->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(inventory_update->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(chat_send->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(chat_receive->lane, arv::protocol::NetworkLane::Gameplay);
  EXPECT_EQ(skin_rules->lane, arv::protocol::NetworkLane::Session);
  EXPECT_EQ(skin_data->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(skin_upload_data->lane, arv::protocol::NetworkLane::ChunkStreaming);
  EXPECT_EQ(registry.size(), 49U);
}

TEST(ProtocolTests, SessionControlPacketsRoundTripThroughSerialization) {
  const arv::protocol::ClientboundKickPlayer kick{"Server is shutting down."};
  const auto decoded_kick = arv::protocol::deserialize_clientbound_kick_player(
      arv::protocol::serialize_clientbound_kick_player(kick));
  ASSERT_TRUE(decoded_kick);
  EXPECT_EQ(decoded_kick.value().reason, kick.reason);

  const std::string long_reason(arv::protocol::MaxKickReasonBytes + 8U, 'x');
  const auto clamped_kick = arv::protocol::deserialize_clientbound_kick_player(
      arv::protocol::serialize_clientbound_kick_player({long_reason}));
  ASSERT_TRUE(clamped_kick);
  EXPECT_EQ(clamped_kick.value().reason.size(), arv::protocol::MaxKickReasonBytes);

  const arv::protocol::ClientboundKeepAlive client_keep_alive{123456789ULL};
  const auto decoded_client_keep_alive =
      arv::protocol::deserialize_clientbound_keep_alive(
          arv::protocol::serialize_clientbound_keep_alive(client_keep_alive));
  ASSERT_TRUE(decoded_client_keep_alive);
  EXPECT_EQ(decoded_client_keep_alive.value().id, client_keep_alive.id);

  const arv::protocol::ServerboundKeepAlive server_keep_alive{987654321ULL};
  const auto decoded_server_keep_alive =
      arv::protocol::deserialize_serverbound_keep_alive(
          arv::protocol::serialize_serverbound_keep_alive(server_keep_alive));
  ASSERT_TRUE(decoded_server_keep_alive);
  EXPECT_EQ(decoded_server_keep_alive.value().id, server_keep_alive.id);
}

TEST(ProtocolTests, SkinPacketsRoundTripThroughSerialization) {
  const std::string hash =
      "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d";
  arv::protocol::ClientboundSkinRules rules;
  rules.enabled = true;
  rules.allow_uploads = true;
  rules.max_res = 128;
  rules.selected_skin = {hash, arv::protocol::SkinImageFormat::Png, 64, 64, true};
  auto decoded_rules = arv::protocol::deserialize_skin_rules(
      arv::protocol::serialize_skin_rules(rules));
  ASSERT_TRUE(decoded_rules);
  EXPECT_EQ(decoded_rules.value().max_res, 128U);
  EXPECT_EQ(decoded_rules.value().selected_skin.skin_hash, hash);
  EXPECT_TRUE(decoded_rules.value().selected_skin.slim_model);

  arv::protocol::ClientboundPlayerProfileInfo info;
  info.player_id = arv::PlayerID{7};
  info.profile_id = hash;
  info.display_name = "Hippo";
  info.skin = {hash, arv::protocol::SkinImageFormat::Png, 64, 64, true};
  auto decoded_info = arv::protocol::deserialize_player_profile_info(
      arv::protocol::serialize_player_profile_info(info));
  ASSERT_TRUE(decoded_info);
  EXPECT_EQ(decoded_info.value().player_id, (arv::PlayerID{7}));
  EXPECT_EQ(decoded_info.value().skin.width, 64U);
  EXPECT_TRUE(decoded_info.value().skin.slim_model);

  arv::protocol::ClientboundSkinData data;
  data.request_id = 42;
  data.skin_hash = hash;
  data.format = arv::protocol::SkinImageFormat::Png;
  data.width = 64;
  data.height = 64;
  data.compressed_image_bytes = {1, 2, 3, 4};
  auto decoded_data = arv::protocol::deserialize_skin_data(
      arv::protocol::serialize_skin_data(data));
  ASSERT_TRUE(decoded_data);
  EXPECT_EQ(decoded_data.value().request_id, 42U);
  EXPECT_EQ(decoded_data.value().compressed_image_bytes, data.compressed_image_bytes);

  arv::protocol::ServerboundSkinUploadBegin begin;
  begin.format = arv::protocol::SkinImageFormat::Png;
  begin.client_skin_hash_hint = hash;
  begin.width = 64;
  begin.height = 64;
  begin.slim_model = true;
  begin.total_upload_bytes = 4096;
  auto decoded_begin = arv::protocol::deserialize_skin_upload_begin(
      arv::protocol::serialize_skin_upload_begin(begin));
  ASSERT_TRUE(decoded_begin);
  EXPECT_EQ(decoded_begin.value().client_skin_hash_hint, hash);
  EXPECT_TRUE(decoded_begin.value().slim_model);

  arv::protocol::ClientboundSkinUploadAccept accept{
      9,
      arv::protocol::SkinUploadMode::Chunked,
      32768};
  auto decoded_accept = arv::protocol::deserialize_skin_upload_accept(
      arv::protocol::serialize_skin_upload_accept(accept));
  ASSERT_TRUE(decoded_accept);
  EXPECT_EQ(decoded_accept.value().upload_mode, arv::protocol::SkinUploadMode::Chunked);
}

TEST(ProtocolTests, ServerboundPlayerMoveRoundTripsThroughSerialization) {
  arv::protocol::ServerboundPlayerMove move;
  move.sequence = 44;
  move.client_tick = 55;
  move.position = {1.0F, 2.0F, 3.0F};
  move.velocity = {0.25F, -0.5F, 1.5F};
  move.yaw_radians = 0.75F;
  move.pitch_radians = -0.25F;
  move.flags = arv::protocol::PlayerMoveFlags::OnGround |
               arv::protocol::PlayerMoveFlags::Sprinting;
  move.action_flags = arv::protocol::PlayerActionFlags::PrimaryPressed;
  move.selected_hotbar_slot = 3;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_serverbound_player_move(move);
  const auto decoded = arv::protocol::deserialize_serverbound_player_move(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().sequence, 44U);
  EXPECT_EQ(decoded.value().client_tick, 55U);
  EXPECT_EQ(decoded.value().position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(decoded.value().velocity, (arv::Vec3f{0.25F, -0.5F, 1.5F}));
  EXPECT_FLOAT_EQ(decoded.value().yaw_radians, 0.75F);
  EXPECT_FLOAT_EQ(decoded.value().pitch_radians, -0.25F);
  EXPECT_EQ(decoded.value().selected_hotbar_slot, 3U);
  EXPECT_TRUE(arv::protocol::has_move_flag(
      decoded.value().flags,
      arv::protocol::PlayerMoveFlags::Sprinting));
  EXPECT_TRUE(arv::protocol::has_action_flag(
      decoded.value().action_flags,
      arv::protocol::PlayerActionFlags::PrimaryPressed));
}

TEST(ProtocolTests, AttackEntityRoundTripsThroughSerialization) {
  const arv::protocol::ServerboundAttackEntity attack{
      arv::EntityNetID{42},
      77U};
  const auto decoded = arv::protocol::deserialize_serverbound_attack_entity(
      arv::protocol::serialize_serverbound_attack_entity(attack));

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().target_entity, (arv::EntityNetID{42}));
  EXPECT_EQ(decoded.value().sequence, 77U);
}

TEST(ProtocolTests, PlayerMoveCorrectionRoundTripsThroughSerialization) {
  arv::protocol::ClientboundPlayerMoveCorrection correction;
  correction.rejected_sequence = 55;
  correction.position = {1.0F, 2.0F, 3.0F};
  correction.velocity = {0.5F, -1.0F, 2.5F};
  correction.yaw_radians = 0.75F;
  correction.pitch_radians = -0.25F;
  correction.flags = arv::protocol::PlayerMoveFlags::Flying;
  correction.reason = arv::protocol::PlayerMoveCorrectionReason::TooFar;

  const std::vector<arv::u8> bytes =
      arv::protocol::serialize_clientbound_player_move_correction(correction);
  const auto decoded = arv::protocol::deserialize_clientbound_player_move_correction(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().rejected_sequence, 55U);
  EXPECT_EQ(decoded.value().position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(decoded.value().velocity, (arv::Vec3f{0.5F, -1.0F, 2.5F}));
  EXPECT_FLOAT_EQ(decoded.value().yaw_radians, 0.75F);
  EXPECT_FLOAT_EQ(decoded.value().pitch_radians, -0.25F);
  EXPECT_TRUE(arv::protocol::has_move_flag(
      decoded.value().flags,
      arv::protocol::PlayerMoveFlags::Flying));
  EXPECT_EQ(decoded.value().reason, arv::protocol::PlayerMoveCorrectionReason::TooFar);
}

TEST(ProtocolTests, PlayerVelocityRoundTripsThroughSerialization) {
  arv::protocol::ClientboundPlayerVelocity velocity;
  velocity.server_tick = 88;
  velocity.player_id = arv::PlayerID{9};
  velocity.velocity = {3.0F, 4.0F, -5.0F};
  velocity.knockback_control_suppression_ticks = 2;

  const std::vector<arv::u8> bytes =
      arv::protocol::serialize_clientbound_player_velocity(velocity);
  const auto decoded = arv::protocol::deserialize_clientbound_player_velocity(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().server_tick, 88U);
  EXPECT_EQ(decoded.value().player_id, (arv::PlayerID{9}));
  EXPECT_EQ(decoded.value().velocity, (arv::Vec3f{3.0F, 4.0F, -5.0F}));
  EXPECT_EQ(decoded.value().knockback_control_suppression_ticks, 2U);
}

TEST(ProtocolTests, SetMovementHzRoundTripsThroughSerialization) {
  const arv::protocol::ClientboundSetMovementHz settings{30.0F};

  const std::vector<arv::u8> bytes =
      arv::protocol::serialize_clientbound_set_movement_hz(settings);
  const auto decoded = arv::protocol::deserialize_clientbound_set_movement_hz(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_FLOAT_EQ(decoded.value().movement_hz, 30.0F);
}

TEST(ProtocolTests, PlayerBlockBreakProgressRoundTripsThroughSerialization) {
  arv::protocol::ClientboundPlayerBlockBreakProgress progress;
  progress.server_tick = 99;
  progress.player_id = arv::PlayerID{7};
  progress.block_break.active = true;
  progress.block_break.block = {{3, 4, 5}};
  progress.block_break.variant = arv::voxel::BlockVariantID{11};
  progress.block_break.progress = 0.5F;
  progress.block_break.stage = 4;

  const std::vector<arv::u8> bytes =
      arv::protocol::serialize_clientbound_player_block_break_progress(progress);
  const auto decoded = arv::protocol::deserialize_clientbound_player_block_break_progress(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().server_tick, 99U);
  EXPECT_EQ(decoded.value().player_id, (arv::PlayerID{7}));
  EXPECT_TRUE(decoded.value().block_break.active);
  EXPECT_EQ(decoded.value().block_break.block, (arv::BlockPos{{3, 4, 5}}));
  EXPECT_EQ(decoded.value().block_break.variant, (arv::voxel::BlockVariantID{11}));
  EXPECT_FLOAT_EQ(decoded.value().block_break.progress, 0.5F);
  EXPECT_EQ(decoded.value().block_break.stage, 4U);
}

TEST(ProtocolTests, ChunkInterestUpdateRoundTripsThroughSerialization) {
  arv::protocol::ServerboundChunkInterestUpdate update;
  update.world_id = arv::WorldID{3};
  update.center_chunk = {{4, 5, -6}};
  update.client_view_distance = 7;
  update.previous_center_chunk = arv::ChunkSectionCoord{{3, 5, -6}};
  update.sequence_id = 42;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_serverbound_chunk_interest_update(update);
  const auto decoded = arv::protocol::deserialize_serverbound_chunk_interest_update(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{3}));
  EXPECT_EQ(decoded.value().center_chunk, (arv::ChunkSectionCoord{{4, 5, -6}}));
  EXPECT_EQ(decoded.value().client_view_distance, 7);
  ASSERT_TRUE(decoded.value().previous_center_chunk.has_value());
  EXPECT_EQ(decoded.value().previous_center_chunk.value(), (arv::ChunkSectionCoord{{3, 5, -6}}));
  EXPECT_EQ(decoded.value().sequence_id, 42U);
}

TEST(ProtocolTests, ChunkSectionDataRoundTripsThroughSerialization) {
  arv::protocol::ClientboundChunkSectionData data;
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

  const std::vector<arv::u8> bytes = arv::protocol::serialize_clientbound_chunk_section_data(data);
  const auto decoded = arv::protocol::deserialize_clientbound_chunk_section_data(bytes);

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
  const arv::protocol::ClientboundChunkSectionUnload unload{arv::WorldID{2}, {{-1, 0, 1}}};

  const std::vector<arv::u8> bytes = arv::protocol::serialize_clientbound_chunk_section_unload(unload);
  const auto decoded = arv::protocol::deserialize_clientbound_chunk_section_unload(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{2}));
  EXPECT_EQ(decoded.value().coord, (arv::ChunkSectionCoord{{-1, 0, 1}}));
}

TEST(ProtocolTests, WorldJoinAndLeaveRoundTripThroughSerialization) {
  arv::protocol::ClientboundWorldJoinResult result;
  result.accepted = true;
  result.world_id = arv::WorldID{4};
  result.player_id = arv::PlayerID{8};
  result.local_entity_id = arv::EntityNetID{12};
  result.spawn_position = {1.0F, 2.0F, 3.0F};
  result.initial_tick = 99;
  result.tick_rate = 20;
  result.effective_view_distance = 6;

  const std::vector<arv::u8> bytes = arv::protocol::serialize_clientbound_world_join_result(result);
  const auto decoded = arv::protocol::deserialize_clientbound_world_join_result(bytes);

  ASSERT_TRUE(decoded);
  EXPECT_TRUE(decoded.value().accepted);
  EXPECT_EQ(decoded.value().world_id, (arv::WorldID{4}));
  EXPECT_EQ(decoded.value().player_id, (arv::PlayerID{8}));
  EXPECT_EQ(decoded.value().local_entity_id, (arv::EntityNetID{12}));
  EXPECT_EQ(decoded.value().spawn_position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(decoded.value().initial_tick, 99U);
  EXPECT_EQ(decoded.value().tick_rate, 20U);
  EXPECT_EQ(decoded.value().effective_view_distance, 6);

  const arv::protocol::ClientboundWorldLeave leave{arv::WorldID{4}, 3};
  const auto decoded_leave =
      arv::protocol::deserialize_clientbound_world_leave(arv::protocol::serialize_clientbound_world_leave(leave));
  ASSERT_TRUE(decoded_leave);
  EXPECT_EQ(decoded_leave.value().world_id, (arv::WorldID{4}));
  EXPECT_EQ(decoded_leave.value().reason, 3U);
}

TEST(ProtocolTests, BlockUpdateBatchRoundTripsAndRejectsMalformedCounts) {
  arv::protocol::ClientboundBlockUpdateBatch batch;
  batch.world_id = arv::WorldID{5};
  batch.server_tick = 123;
  batch.updates.push_back({arv::BlockPos{{1, -2, 3}},
                           arv::voxel::BlockVariantID{9},
                           7,
                           120});

  const std::vector<arv::u8> bytes = arv::protocol::serialize_clientbound_block_update_batch(batch);
  const auto decoded = arv::protocol::deserialize_clientbound_block_update_batch(bytes);

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
  EXPECT_FALSE(arv::protocol::deserialize_clientbound_block_update_batch(
      std::move(malformed).take_bytes()));
}

TEST(ProtocolTests, EntityReplicationMessagesRoundTripAndRejectMalformedCounts) {
  arv::protocol::ClientboundEntitySpawn spawn;
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
      arv::protocol::deserialize_clientbound_entity_spawn(arv::protocol::serialize_clientbound_entity_spawn(spawn));
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

  const arv::protocol::ClientboundEntityDespawn despawn{arv::WorldID{6}, arv::EntityNetID{22}, 4};
  const auto decoded_despawn = arv::protocol::deserialize_clientbound_entity_despawn(
      arv::protocol::serialize_clientbound_entity_despawn(despawn));
  ASSERT_TRUE(decoded_despawn);
  EXPECT_EQ(decoded_despawn.value().entity_id, (arv::EntityNetID{22}));
  EXPECT_EQ(decoded_despawn.value().reason, 4U);

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{6};
  snapshot.server_tick = 77;
  snapshot.entities.push_back({arv::EntityNetID{22},
                               {6.0F, 7.0F, 8.0F},
                               {1.0F, 0.0F, 0.0F},
                               {0.0F, 0.0F, 0.0F, 1.0F},
                               5,
                               {{"walking", 1.25F}, {"hit_right", 0.125F}},
                               {10.0F, 20.0F, 0.0F}});
  const auto decoded_snapshot = arv::protocol::deserialize_clientbound_entity_snapshot(
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot));
  ASSERT_TRUE(decoded_snapshot);
  ASSERT_EQ(decoded_snapshot.value().entities.size(), 1U);
  EXPECT_EQ(decoded_snapshot.value().server_tick, 77U);
  EXPECT_EQ(decoded_snapshot.value().entities.front().entity_id, (arv::EntityNetID{22}));
  EXPECT_EQ(decoded_snapshot.value().entities.front().position, (arv::Vec3f{6.0F, 7.0F, 8.0F}));
  EXPECT_EQ(decoded_snapshot.value().entities.front().state_flags, 5U);
  ASSERT_EQ(decoded_snapshot.value().entities.front().animations.size(), 2U);
  EXPECT_EQ(decoded_snapshot.value().entities.front().animations[0].name, "walking");
  EXPECT_FLOAT_EQ(decoded_snapshot.value().entities.front().animations[0].time_seconds, 1.25F);
  EXPECT_EQ(decoded_snapshot.value().entities.front().animations[1].name, "hit_right");
  EXPECT_FLOAT_EQ(decoded_snapshot.value().entities.front().animations[1].time_seconds, 0.125F);
  EXPECT_EQ(
      decoded_snapshot.value().entities.front().head_rotation_degrees,
      (arv::Vec3f{10.0F, 20.0F, 0.0F}));

  arv::protocol::MessageWriter malformed;
  malformed.write<arv::u32>(6);
  malformed.write<arv::u64>(77);
  malformed.write<arv::u32>(arv::protocol::MaxEntitiesPerSnapshot + 1U);
  EXPECT_FALSE(arv::protocol::deserialize_clientbound_entity_snapshot(
      std::move(malformed).take_bytes()));

  arv::protocol::ClientboundDroppedItemSnapshot dropped;
  dropped.world_id = arv::WorldID{6};
  dropped.server_tick = 88;
  dropped.drops.push_back({arv::EntityNetID{33},
                           arv::player::ItemStack{arv::player::ItemID{44}, 3},
                           {1.0F, 2.0F, 3.0F},
                           {0.1F, 0.2F, 0.3F}});
  const auto decoded_dropped = arv::protocol::deserialize_clientbound_dropped_item_snapshot(
      arv::protocol::serialize_clientbound_dropped_item_snapshot(dropped));
  ASSERT_TRUE(decoded_dropped);
  ASSERT_EQ(decoded_dropped.value().drops.size(), 1U);
  EXPECT_EQ(decoded_dropped.value().server_tick, 88U);
  EXPECT_EQ(decoded_dropped.value().drops.front().drop_id, (arv::EntityNetID{33}));
  EXPECT_EQ(decoded_dropped.value().drops.front().stack, (arv::player::ItemStack{arv::player::ItemID{44}, 3}));
  EXPECT_EQ(decoded_dropped.value().drops.front().position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
}

TEST(ProtocolTests, InventorySyncMessagesRoundTripAndRejectMalformedCounts) {
  arv::protocol::ServerboundInventoryActionRequest request;
  request.inventory_id = arv::protocol::PlayerInventoryID;
  request.client_action_id = 42;
  request.kind = arv::protocol::InventoryActionKind::SlotClick;
  request.slot_index = 3;
  request.button = arv::player::InventoryClickButton::Secondary;

  const auto decoded_request = arv::protocol::deserialize_serverbound_inventory_action_request(
      arv::protocol::serialize_serverbound_inventory_action_request(request));
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
  EXPECT_FALSE(arv::protocol::deserialize_serverbound_inventory_action_request(
      std::move(invalid_action).take_bytes()));

  arv::protocol::ClientboundInventoryUpdate update;
  update.inventory_id = arv::protocol::PlayerInventoryID;
  update.revision = 7;
  update.selected_hotbar_slot = 2;
  update.slots = {
      arv::player::ItemStack{arv::player::ItemID{10}, 5},
      std::nullopt};
  update.carried_stack = arv::player::ItemStack{arv::player::ItemID{11}, 1};

  const auto decoded_update = arv::protocol::deserialize_clientbound_inventory_update(
      arv::protocol::serialize_clientbound_inventory_update(update));
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
  EXPECT_FALSE(arv::protocol::deserialize_clientbound_inventory_update(
      std::move(malformed).take_bytes()));
}
