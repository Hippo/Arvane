#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_registry.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/message_writer.hpp"

#include <gtest/gtest.h>

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

  ASSERT_TRUE(ping.has_value());
  ASSERT_TRUE(input.has_value());
  ASSERT_TRUE(correction.has_value());
  EXPECT_EQ(ping->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(input->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(correction->reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(registry.size(), 7U);
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
  correction.selected_hotbar_slot = 6;

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
  EXPECT_EQ(decoded.value().selected_hotbar_slot, 6U);
}
