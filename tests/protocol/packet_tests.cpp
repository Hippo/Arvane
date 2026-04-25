#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/message_registry.hpp"
#include "arv/protocol/message_writer.hpp"

#include <gtest/gtest.h>

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

  ASSERT_TRUE(ping.has_value());
  EXPECT_EQ(ping->reliability, arv::protocol::ReliabilityClass::UnreliableEvent);
  EXPECT_EQ(registry.size(), 5U);
}
