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
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerHello, "ServerHello",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::DisconnectReason, "DisconnectReason",
                             MessageDirection::Bidirectional, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::Ping, "Ping",
                             MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent,
                             NetworkLane::Debug});
  registry.register_message({MessageType::Pong, "Pong",
                             MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent,
                             NetworkLane::Debug});
  registry.register_message({MessageType::AuthChallenge, "AuthChallenge",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::AuthResponse, "AuthResponse",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::AuthResult, "AuthResult",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientInputBatch, "ClientInputBatch",
                             MessageDirection::ClientToServer, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Movement});
  registry.register_message({MessageType::PlayerCorrection, "PlayerCorrection",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Movement});
  registry.register_message({MessageType::WorldJoinResult, "WorldJoinResult",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::WorldLeave, "WorldLeave",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ChunkInterestUpdate, "ChunkInterestUpdate",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ChunkData, "ChunkData",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ChunkUnload, "ChunkUnload",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::BlockUpdateBatch, "BlockUpdateBatch",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::EntitySpawn, "EntitySpawn",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::EntityDespawn, "EntityDespawn",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::EntitySnapshot, "EntitySnapshot",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Movement});
  registry.register_message({MessageType::DroppedItemSnapshot, "DroppedItemSnapshot",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::InventoryActionRequest, "InventoryActionRequest",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::InventoryUpdate, "InventoryUpdate",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  return registry;
}

} // namespace arv::protocol
