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
  Packet() = default;
  Packet(const ArvAPI* api, ArvPacketHandle handle) noexcept
      : api_(api), handle_(handle) {
  }

  Packet(const Packet&) = delete;
  Packet& operator=(const Packet&) = delete;

  Packet(Packet&& other) noexcept
      : api_(std::exchange(other.api_, nullptr)),
        handle_(std::exchange(other.handle_, {})) {
  }

  Packet& operator=(Packet&& other) noexcept {
    if (this != &other) {
      reset();
      api_ = std::exchange(other.api_, nullptr);
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~Packet() {
    reset();
  }

  void reset() noexcept {
    if (handle_.value != 0U && api_ != nullptr && api_->network != nullptr &&
        api_->network->release_packet != nullptr) {
      api_->network->release_packet(api_->network_ctx, handle_);
    }
    api_ = nullptr;
    handle_ = {};
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return handle_.value != 0U;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return valid();
  }

  [[nodiscard]] constexpr ArvPacketHandle abi() const noexcept {
    return handle_;
  }

private:
  const ArvAPI* api_{};
  ArvPacketHandle handle_{};
};

struct PacketResult {
  Result result{};
  Packet packet{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result);
  }
};

class Network {
public:
  explicit constexpr Network(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->unsupported == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->unsupported(api_->network_ctx)};
  }

  [[nodiscard]] PacketResult create_chat_message(
      const TextComponent& message,
      std::string_view channel = "arvane:system",
      ChatMessageKind kind = ChatMessageKind::System) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->create_clientbound_chat_message == nullptr) {
      return {Result::unsupported("network API is unavailable"), {}};
    }
    ArvPacketHandle handle{};
    Result result{api_->network->create_clientbound_chat_message(
        api_->network_ctx,
        message.abi(),
        view(channel),
        static_cast<ArvChatMessageKind>(kind),
        &handle)};
    return {result, Packet{api_, handle}};
  }

  [[nodiscard]] PacketResult create_kick_player(
      const TextComponent& reason) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->create_clientbound_kick_player == nullptr) {
      return {Result::unsupported("network API is unavailable"), {}};
    }
    ArvPacketHandle handle{};
    Result result{api_->network->create_clientbound_kick_player(
        api_->network_ctx,
        reason.abi(),
        &handle)};
    return {result, Packet{api_, handle}};
  }

  [[nodiscard]] Result send_to_player(
      ArvPlayerID player,
      const Packet& packet) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->send_to_player == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->send_to_player(
        api_->network_ctx,
        player,
        packet.abi())};
  }

  [[nodiscard]] Result send_to_players(
      std::span<const ArvPlayerID> players,
      const Packet& packet) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->send_to_players == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->send_to_players(
        api_->network_ctx,
        players.data(),
        players.size(),
        packet.abi())};
  }

  [[nodiscard]] Result broadcast(const Packet& packet) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->broadcast == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->broadcast(api_->network_ctx, packet.abi())};
  }

  [[nodiscard]] Result broadcast_world(
      ArvWorldHandle world,
      const Packet& packet) const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->broadcast_world == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->broadcast_world(
        api_->network_ctx,
        world,
        packet.abi())};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
