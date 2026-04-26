#pragma once

#include "arv/protocol/message_type.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/protocol_version.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

struct MessageEnvelope {
  u16 protocol_version{CurrentProtocolVersion};
  MessageType message_type{MessageType::Ping};
  u32 message_flags{};
  u32 sequence{};
  u32 ack_reference{};
  u32 payload_size{};
};

struct DecodedMessage {
  MessageEnvelope envelope{};
  std::vector<u8> payload{};
};

inline constexpr usize MessageEnvelopeHeaderSize = sizeof(u16) + sizeof(u16) + sizeof(u32) +
                                                   sizeof(u32) + sizeof(u32) + sizeof(u32);

std::vector<u8> serialize_envelope(MessageEnvelope envelope, std::span<const u8> payload);

Expected<DecodedMessage, ProtocolError> deserialize_envelope(std::span<const u8> bytes);

} // namespace arv::protocol
