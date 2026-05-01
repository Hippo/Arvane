#pragma once

#include "arv/core/types.hpp"

namespace arv::protocol {

enum class ReliabilityClass : u8 {
  ReliableOrdered,
  UnreliableLatestWins,
  UnreliableEvent,
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
  ClientInputBatch = 0x0100,
  PlayerCorrection = 0x0101,
  WorldJoinResult = 0x0301,
  WorldLeave = 0x0302,
  ChunkInterestUpdate = 0x0400,
  ChunkData = 0x0401,
  ChunkUnload = 0x0402,
  BlockUpdateBatch = 0x0500,
  EntitySpawn = 0x0600,
  EntityDespawn = 0x0601,
  EntitySnapshot = 0x0602,
  InventoryActionRequest = 0x0800,
  InventoryUpdate = 0x0801,
};

} // namespace arv::protocol
