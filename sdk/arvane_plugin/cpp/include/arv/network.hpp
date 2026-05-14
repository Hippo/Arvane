#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/text.hpp"
#include "arv/types.hpp"

#include <span>
#include <string_view>
#include <utility>

namespace arv {

enum class ChatMessageKind {
  Chat = ARV_CHAT_MESSAGE_CHAT,
  System = ARV_CHAT_MESSAGE_SYSTEM,
  Announcement = ARV_CHAT_MESSAGE_ANNOUNCEMENT,
  Private = ARV_CHAT_MESSAGE_PRIVATE,
  Error = ARV_CHAT_MESSAGE_ERROR,
};

class Packet {
public:
  Packet() noexcept;
  Packet(const ArvAPI* api, ArvPacketHandle handle) noexcept;

  Packet(const Packet&) = delete;
  Packet& operator=(const Packet&) = delete;

  Packet(Packet&& other) noexcept;

  Packet& operator=(Packet&& other) noexcept;

  ~Packet();

  void reset() noexcept;

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;

  [[nodiscard]] ArvPacketHandle abi() const noexcept;

private:
  const ArvAPI* api_{};
  ArvPacketHandle handle_{};
};

struct PacketResult {
  Result result{};
  Packet packet{};

  [[nodiscard]] explicit operator bool() const noexcept;
};

class Network {
public:
  explicit Network(const ArvAPI* api) noexcept;

  [[nodiscard]] Result unsupported() const;

  [[nodiscard]] PacketResult create_chat_message(
      const TextComponent& message,
      std::string_view channel = "arvane:system",
      ChatMessageKind kind = ChatMessageKind::System) const;

  [[nodiscard]] PacketResult create_kick_player(
      const TextComponent& reason) const;

  [[nodiscard]] Result send_to_player(
      ArvPlayerID player,
      const Packet& packet) const;

  [[nodiscard]] Result send_to_players(
      std::span<const ArvPlayerID> players,
      const Packet& packet) const;

  [[nodiscard]] Result broadcast(const Packet& packet) const;

  [[nodiscard]] Result broadcast_world(
      ArvWorldHandle world,
      const Packet& packet) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
