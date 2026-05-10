#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::protocol {

inline constexpr usize MaxKickReasonBytes = 512U;

struct ClientboundKickPlayer {
  std::string reason{};
};

struct ClientboundKeepAlive {
  u64 id{};
};

struct ServerboundKeepAlive {
  u64 id{};
};

[[nodiscard]] std::string clamp_kick_reason(std::string_view reason);

[[nodiscard]] std::vector<u8> serialize_clientbound_kick_player(
    const ClientboundKickPlayer& kick);
[[nodiscard]] Expected<ClientboundKickPlayer, ProtocolError> deserialize_clientbound_kick_player(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_clientbound_keep_alive(
    const ClientboundKeepAlive& keep_alive);
[[nodiscard]] Expected<ClientboundKeepAlive, ProtocolError> deserialize_clientbound_keep_alive(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_serverbound_keep_alive(
    const ServerboundKeepAlive& keep_alive);
[[nodiscard]] Expected<ServerboundKeepAlive, ProtocolError> deserialize_serverbound_keep_alive(
    std::span<const u8> bytes);

} // namespace arv::protocol
