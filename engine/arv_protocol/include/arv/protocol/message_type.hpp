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
};

} // namespace arv::protocol
