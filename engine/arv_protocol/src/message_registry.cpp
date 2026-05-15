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
  registry.register_message({MessageType::ServerboundHello, "ServerboundHello",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundHello, "ClientboundHello",
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
  registry.register_message({MessageType::ClientboundAuthChallenge, "ClientboundAuthChallenge",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundAuthResponse, "ServerboundAuthResponse",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundAuthResult, "ClientboundAuthResult",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundKickPlayer, "ClientboundKickPlayer",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableImmediate,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundKeepAlive, "ClientboundKeepAlive",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableEvent,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundKeepAlive, "ServerboundKeepAlive",
                             MessageDirection::ClientToServer, ReliabilityClass::UnreliableEvent,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundPlayerMove, "ServerboundPlayerMove",
                             MessageDirection::ClientToServer, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Movement});
  registry.register_message({MessageType::ClientboundPlayerMoveCorrection, "ClientboundPlayerMoveCorrection",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Movement});
  registry.register_message({MessageType::ClientboundPlayerVelocity, "ClientboundPlayerVelocity",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Movement});
  registry.register_message({MessageType::ClientboundSetMovementHz, "ClientboundSetMovementHz",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundPlayerBlockBreakProgress, "ClientboundPlayerBlockBreakProgress",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundSkinRules, "ClientboundSkinRules",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundPlayerProfileInfo, "ClientboundPlayerProfileInfo",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundPlayerProfileInfoBatch, "ClientboundPlayerProfileInfoBatch",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundSkinRequest, "ServerboundSkinRequest",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundSkinRequestBatch, "ServerboundSkinRequestBatch",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundSkinData, "ClientboundSkinData",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundSkinDataBeginChunked, "ClientboundSkinDataBeginChunked",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundSkinDataChunk, "ClientboundSkinDataChunk",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundSkinDataEndChunked, "ClientboundSkinDataEndChunked",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundSkinRequestDeny, "ClientboundSkinRequestDeny",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundSkinUploadBegin, "ServerboundSkinUploadBegin",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundSkinUploadAccept, "ClientboundSkinUploadAccept",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundSkinUploadDeny, "ClientboundSkinUploadDeny",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundSkinUploadData, "ServerboundSkinUploadData",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ServerboundSkinUploadChunk, "ServerboundSkinUploadChunk",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ServerboundSkinUploadEnd, "ServerboundSkinUploadEnd",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundSkinUploadComplete, "ClientboundSkinUploadComplete",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundSkinUploadFailed, "ClientboundSkinUploadFailed",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundWorldJoinResult, "ClientboundWorldJoinResult",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ClientboundWorldLeave, "ClientboundWorldLeave",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Session});
  registry.register_message({MessageType::ServerboundChunkInterestUpdate, "ServerboundChunkInterestUpdate",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundChunkSectionData, "ClientboundChunkSectionData",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundChunkSectionUnload, "ClientboundChunkSectionUnload",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::ChunkStreaming});
  registry.register_message({MessageType::ClientboundBlockUpdateBatch, "ClientboundBlockUpdateBatch",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundEntitySpawn, "ClientboundEntitySpawn",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundEntityDespawn, "ClientboundEntityDespawn",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundEntitySnapshot, "ClientboundEntitySnapshot",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Movement});
  registry.register_message({MessageType::ClientboundDroppedItemSnapshot, "ClientboundDroppedItemSnapshot",
                             MessageDirection::ServerToClient, ReliabilityClass::UnreliableLatestWins,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ServerboundAttackEntity, "ServerboundAttackEntity",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ServerboundInventoryActionRequest, "ServerboundInventoryActionRequest",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundInventoryUpdate, "ClientboundInventoryUpdate",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ServerboundChatMessage, "ServerboundChatMessage",
                             MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundChatMessage, "ClientboundChatMessage",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  registry.register_message({MessageType::ClientboundTextVariableUpdate, "ClientboundTextVariableUpdate",
                             MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered,
                             NetworkLane::Gameplay});
  return registry;
}

} // namespace arv::protocol
