#include "arv/network.hpp"

#include <utility>

namespace arv {

Packet::Packet() noexcept = default;

Packet::Packet(const ArvAPI* api, ArvPacketHandle handle) noexcept
    : api_(api), handle_(handle) {
}

Packet::Packet(Packet&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      handle_(std::exchange(other.handle_, {})) {
}

Packet& Packet::operator=(Packet&& other) noexcept {
  if (this != &other) {
    reset();
    api_ = std::exchange(other.api_, nullptr);
    handle_ = std::exchange(other.handle_, {});
  }
  return *this;
}

Packet::~Packet() {
  reset();
}

void Packet::reset() noexcept {
  if (handle_.value != 0U && api_ != nullptr && api_->network != nullptr &&
      api_->network->release_packet != nullptr) {
    api_->network->release_packet(api_->network_ctx, handle_);
  }
  api_ = nullptr;
  handle_ = {};
}

bool Packet::valid() const noexcept {
  return handle_.value != 0U;
}

Packet::operator bool() const noexcept {
  return valid();
}

ArvPacketHandle Packet::abi() const noexcept {
  return handle_;
}

PacketResult::operator bool() const noexcept {
  return static_cast<bool>(result);
}

Network::Network(const ArvAPI* api) noexcept : api_(api) {
}

Result Network::unsupported() const {
  if (api_ == nullptr || api_->network == nullptr ||
      api_->network->unsupported == nullptr) {
    return Result::unsupported("network API is unavailable");
  }
  return Result{api_->network->unsupported(api_->network_ctx)};
}

PacketResult Network::create_chat_message(
    const TextComponent& message,
    std::string_view channel,
    ChatMessageKind kind) const {
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

PacketResult Network::create_kick_player(
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

Result Network::send_to_player(
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

Result Network::send_to_players(
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

Result Network::broadcast(const Packet& packet) const {
  if (api_ == nullptr || api_->network == nullptr ||
      api_->network->broadcast == nullptr) {
    return Result::unsupported("network API is unavailable");
  }
  return Result{api_->network->broadcast(api_->network_ctx, packet.abi())};
}

Result Network::broadcast_world(
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

} // namespace arv
