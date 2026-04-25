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

inline std::vector<u8> serialize_envelope(MessageEnvelope envelope, std::span<const u8> payload) {
  envelope.payload_size = static_cast<u32>(payload.size());

  MessageWriter writer;
  writer.write<u16>(envelope.protocol_version);
  writer.write<MessageType>(envelope.message_type);
  writer.write<u32>(envelope.message_flags);
  writer.write<u32>(envelope.sequence);
  writer.write<u32>(envelope.ack_reference);
  writer.write<u32>(envelope.payload_size);
  writer.write_bytes(payload);
  return std::move(writer).take_bytes();
}

inline Expected<DecodedMessage, ProtocolError> deserialize_envelope(std::span<const u8> bytes) {
  if (bytes.size() < MessageEnvelopeHeaderSize) {
    return Expected<DecodedMessage, ProtocolError>::failure(ProtocolError{"packet shorter than envelope"});
  }

  MessageReader reader(bytes);
  auto version = reader.read<u16>();
  auto type = reader.read<MessageType>();
  auto flags = reader.read<u32>();
  auto sequence = reader.read<u32>();
  auto ack = reader.read<u32>();
  auto payload_size = reader.read<u32>();

  if (!version || !type || !flags || !sequence || !ack || !payload_size) {
    return Expected<DecodedMessage, ProtocolError>::failure(ProtocolError{"failed to decode envelope"});
  }

  if (version.value() != CurrentProtocolVersion) {
    return Expected<DecodedMessage, ProtocolError>::failure(ProtocolError{"unsupported protocol version"});
  }

  if (reader.remaining() != payload_size.value()) {
    return Expected<DecodedMessage, ProtocolError>::failure(ProtocolError{"payload size mismatch"});
  }

  DecodedMessage decoded;
  decoded.envelope.protocol_version = version.value();
  decoded.envelope.message_type = type.value();
  decoded.envelope.message_flags = flags.value();
  decoded.envelope.sequence = sequence.value();
  decoded.envelope.ack_reference = ack.value();
  decoded.envelope.payload_size = payload_size.value();
  decoded.payload.assign(bytes.end() - static_cast<isize>(payload_size.value()), bytes.end());
  return Expected<DecodedMessage, ProtocolError>::success(std::move(decoded));
}

} // namespace arv::protocol
