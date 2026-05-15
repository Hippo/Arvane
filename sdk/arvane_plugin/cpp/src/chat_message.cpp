#include "arv/chat_message.hpp"

#include "arv/plugin.hpp"

#include <utility>

namespace arv {

ChatMessage::ChatMessage() noexcept = default;

ChatMessage::ChatMessage(const ArvAPI* api, std::string_view source) {
  ChatMessageResult parsed = parse(api, source);
  if (parsed) {
    *this = std::move(parsed.message);
  }
}

ChatMessage::ChatMessage(const Plugin& plugin, std::string_view source)
    : ChatMessage(plugin.api(), source) {
}

ChatMessage::ChatMessage(
    const ArvAPI* api,
    ArvChatMessageHandle handle,
    std::string channel,
    ChatMessageKind kind) noexcept
    : api_(api),
      handle_(handle),
      channel_(std::move(channel)),
      kind_(kind) {
}

ChatMessage::ChatMessage(ChatMessage&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      handle_(std::exchange(other.handle_, {})),
      channel_(std::move(other.channel_)),
      kind_(other.kind_) {
}

ChatMessage& ChatMessage::operator=(ChatMessage&& other) noexcept {
  if (this != &other) {
    reset();
    api_ = std::exchange(other.api_, nullptr);
    handle_ = std::exchange(other.handle_, {});
    channel_ = std::move(other.channel_);
    kind_ = other.kind_;
  }
  return *this;
}

ChatMessage::~ChatMessage() {
  reset();
}

ChatMessageResult ChatMessage::parse(
    const ArvAPI* api,
    std::string_view source) {
  if (api == nullptr || api->chat_message == nullptr ||
      api->chat_message->parse == nullptr) {
    return {Result::unsupported("chat message API is unavailable"), {}};
  }
  ArvChatMessageHandle handle{};
  Result result{api->chat_message->parse(api->chat_message_ctx, view(source), &handle)};
  return {result, ChatMessage{api, handle, "arvane:system", ChatMessageKind::System}};
}

ChatMessageResult ChatMessage::parse(
    const Plugin& plugin,
    std::string_view source) {
  return parse(plugin.api(), source);
}

ChatMessageResult ChatMessage::from_config(
    const ArvAPI* api,
    const DataDocument& document,
    std::string_view path) {
  if (api == nullptr || api->chat_message == nullptr ||
      api->chat_message->from_data == nullptr) {
    return {Result::unsupported("chat message API is unavailable"), {}};
  }
  ArvChatMessageHandle handle{};
  Result result{api->chat_message->from_data(
      api->chat_message_ctx,
      document.handle(),
      view(path),
      &handle)};
  return {result, ChatMessage{api, handle, "arvane:system", ChatMessageKind::System}};
}

ChatMessageResult ChatMessage::from_config(
    const Plugin& plugin,
    const DataDocument& document,
    std::string_view path) {
  return from_config(plugin.api(), document, path);
}

void ChatMessage::reset() noexcept {
  if (handle_.value != 0U && api_ != nullptr && api_->chat_message != nullptr &&
      api_->chat_message->release != nullptr) {
    api_->chat_message->release(api_->chat_message_ctx, handle_);
  }
  api_ = nullptr;
  handle_ = {};
}

bool ChatMessage::valid() const noexcept {
  return handle_.value != 0U;
}

ChatMessage::operator bool() const noexcept {
  return valid();
}

ArvChatMessageHandle ChatMessage::abi() const noexcept {
  return handle_;
}

Result ChatMessage::unsupported() const {
  return Result::unsupported("chat message API is unavailable");
}

BufferResult ChatMessage::to_buffer(std::optional<ArvPlayerID> player) const {
  if (api_ == nullptr || api_->chat_message == nullptr) {
    return {unsupported(), {}};
  }
  ArvByteBuffer buffer{};
  if (player.has_value()) {
    if (api_->chat_message->to_string_for == nullptr) {
      return {unsupported(), {}};
    }
    Result result{api_->chat_message->to_string_for(
        api_->chat_message_ctx,
        handle_,
        *player,
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }
  if (api_->chat_message->to_string == nullptr) {
    return {unsupported(), {}};
  }
  Result result{
      api_->chat_message->to_string(api_->chat_message_ctx, handle_, &buffer)};
  return {result, Buffer{api_, buffer}};
}

ValueResult<std::string> ChatMessage::to_string() const {
  BufferResult result = to_buffer(std::nullopt);
  if (!result) {
    return {result.result, {}};
  }
  return {Result::ok(), std::string{result.buffer.text()}};
}

ValueResult<std::string> ChatMessage::to_string_for(ArvPlayerID player) const {
  BufferResult result = to_buffer(player);
  if (!result) {
    return {result.result, {}};
  }
  return {Result::ok(), std::string{result.buffer.text()}};
}

TextResult ChatMessage::as_text() const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->as_text == nullptr) {
    return {unsupported(), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->chat_message->as_text(api_->chat_message_ctx, handle_, &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult ChatMessage::as_text_for(ArvPlayerID player) const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->as_text_for == nullptr) {
    return {unsupported(), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->chat_message->as_text_for(
      api_->chat_message_ctx,
      handle_,
      player,
      &handle)};
  return {result, TextComponent{api_, handle}};
}

Result ChatMessage::send_to(ArvPlayerID player) const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->send_to == nullptr) {
    return unsupported();
  }
  return Result{api_->chat_message->send_to(
      api_->chat_message_ctx,
      handle_,
      player,
      view(channel_),
      static_cast<ArvChatMessageKind>(kind_))};
}

Result ChatMessage::send_all(std::span<const ArvPlayerID> players) const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->send_to_players == nullptr) {
    return unsupported();
  }
  return Result{api_->chat_message->send_to_players(
      api_->chat_message_ctx,
      handle_,
      players.data(),
      players.size(),
      view(channel_),
      static_cast<ArvChatMessageKind>(kind_))};
}

Result ChatMessage::broadcast() const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->broadcast == nullptr) {
    return unsupported();
  }
  return Result{api_->chat_message->broadcast(
      api_->chat_message_ctx,
      handle_,
      view(channel_),
      static_cast<ArvChatMessageKind>(kind_))};
}

Result ChatMessage::broadcast_world(ArvWorldHandle world) const {
  if (api_ == nullptr || api_->chat_message == nullptr ||
      api_->chat_message->broadcast_world == nullptr) {
    return unsupported();
  }
  return Result{api_->chat_message->broadcast_world(
      api_->chat_message_ctx,
      handle_,
      world,
      view(channel_),
      static_cast<ArvChatMessageKind>(kind_))};
}

ChatMessage& ChatMessage::with_channel(std::string_view channel) {
  channel_ = std::string{channel};
  return *this;
}

ChatMessage& ChatMessage::with_kind(ChatMessageKind kind) noexcept {
  kind_ = kind;
  return *this;
}

ChatMessageResult::operator bool() const noexcept {
  return static_cast<bool>(result);
}

} // namespace arv
