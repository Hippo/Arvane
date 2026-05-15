#pragma once

#include "arv/api.h"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/text/chat_message.hpp"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace arv::plugin {

enum class PluginNetworkTargetKind : u8 {
  Player,
  Players,
  Broadcast,
  World,
};

struct PluginNetworkPacket {
  protocol::MessageType message_type{protocol::MessageType::Ping};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  protocol::NetworkLane lane{protocol::NetworkLane::Session};
  std::vector<u8> payload{};
};

struct PluginNetworkSendError {
  ArvResultCode code{ARV_RESULT_INTERNAL_ERROR};
  std::string message{};
};

struct PluginNetworkSendRequest {
  PluginNetworkTargetKind target_kind{PluginNetworkTargetKind::Player};
  PlayerID player{};
  std::span<const PlayerID> players{};
  WorldID world{};
  const PluginNetworkPacket* packet{};
};

using PluginNetworkSendResult = Expected<void, PluginNetworkSendError>;
using PluginNetworkSendFn =
    std::function<PluginNetworkSendResult(const PluginNetworkSendRequest&)>;

struct PluginChatMessageSendRequest {
  PluginNetworkTargetKind target_kind{PluginNetworkTargetKind::Player};
  PlayerID player{};
  std::span<const PlayerID> players{};
  WorldID world{};
  const arv::text::ChatMessageTemplate* message{};
  std::string channel{std::string{protocol::ChatChannelSystem}};
  protocol::ChatMessageKind kind{protocol::ChatMessageKind::System};
};

using PluginChatMessageSendFn =
    std::function<PluginNetworkSendResult(const PluginChatMessageSendRequest&)>;

} // namespace arv::plugin
