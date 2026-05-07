#pragma once

#include "arv/core/types.hpp"

namespace arv::protocol {

enum class ReliabilityClass : u8 {
  ReliableOrdered,
  UnreliableLatestWins,
  UnreliableEvent,
};

enum class NetworkLane : u16 {
  Session = 0,
  Gameplay = 1,
  Movement = 2,
  ChunkStreaming = 3,
  Debug = 4,
};

enum class MessageDirection : u8 {
  ClientToServer,
  ServerToClient,
  Bidirectional,
};

enum class MessageType : u16 {
  ClientHello = 0x0001,
  ServerHello = 0x0002,
  DisconnectReason = 0x0003,
  Ping = 0x0004,
  Pong = 0x0005,
  AuthChallenge = 0x0006,
  AuthResponse = 0x0007,
  AuthResult = 0x0008,
  ClientInputBatch = 0x0100,
  PlayerCorrection = 0x0101,
  SkinRules = 0x0120,
  PlayerProfileInfo = 0x0121,
  PlayerProfileInfoBatch = 0x0122,
  SkinRequest = 0x0123,
  SkinRequestBatch = 0x0124,
  SkinData = 0x0125,
  SkinDataBeginChunked = 0x0126,
  SkinDataChunk = 0x0127,
  SkinDataEndChunked = 0x0128,
  SkinRequestDeny = 0x0129,
  SkinUploadBegin = 0x012A,
  SkinUploadAccept = 0x012B,
  SkinUploadDeny = 0x012C,
  SkinUploadData = 0x012D,
  SkinUploadChunk = 0x012E,
  SkinUploadEnd = 0x012F,
  SkinUploadComplete = 0x0130,
  SkinUploadFailed = 0x0131,
  WorldJoinResult = 0x0301,
  WorldLeave = 0x0302,
  ChunkInterestUpdate = 0x0400,
  ChunkSectionData = 0x0401,
  ChunkSectionUnload = 0x0402,
  ChunkData = ChunkSectionData,
  ChunkUnload = ChunkSectionUnload,
  BlockUpdateBatch = 0x0500,
  EntitySpawn = 0x0600,
  EntityDespawn = 0x0601,
  EntitySnapshot = 0x0602,
  DroppedItemSnapshot = 0x0603,
  InventoryActionRequest = 0x0800,
  InventoryUpdate = 0x0801,
};

} // namespace arv::protocol
