#include "arv/protocol/message_registry.hpp"

#include <utility>

namespace arv::protocol {

bool MessageRegistry::register_message(MessageDescriptor descriptor) {
  const MessageType type = descriptor.type;
  return descriptors_.emplace(type, std::move(descriptor)).second;
}

std::optional<MessageDescriptor> MessageRegistry::lookup(MessageType type) const {
  const auto found = descriptors_.find(type);
  if (found == descriptors_.end()) {
    return std::nullopt;
  }
  return found->second;
}

MessageRegistry MessageRegistry::make_core_registry() {
  MessageRegistry registry;
  registry.register_message({MessageType::ClientHello, "ClientHello",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered});
  registry.register_message({MessageType::ServerHello, "ServerHello",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered});
  registry.register_message({MessageType::DisconnectReason, "DisconnectReason",
                             MessageDirection::Bidirectional, ReliabilityClass::ReliableOrdered});
  registry.register_message({MessageType::Ping, "Ping",
                             MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent});
  registry.register_message({MessageType::Pong, "Pong",
                             MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent});
  registry.register_message({MessageType::ClientInputBatch, "ClientInputBatch",
                             MessageDirection::ClientToServer, ReliabilityClass::UnreliableLatestWins});
  registry.register_message({MessageType::PlayerCorrection, "PlayerCorrection",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins});
  registry.register_message({MessageType::ChunkInterestUpdate, "ChunkInterestUpdate",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered});
  registry.register_message({MessageType::ChunkData, "ChunkData",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered});
  registry.register_message({MessageType::ChunkUnload, "ChunkUnload",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered});
  return registry;
}

} // namespace arv::protocol
