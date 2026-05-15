#pragma once

#include "arv/api.h"
#include "arv/buffer.hpp"
#include "arv/data.hpp"
#include "arv/network.hpp"
#include "arv/result.hpp"
#include "arv/text.hpp"
#include "arv/types.hpp"

#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace arv {

class Plugin;
struct ChatMessageResult;

class ChatMessage {
public:
  ChatMessage() noexcept;
  ChatMessage(const ArvAPI* api, std::string_view source);
  ChatMessage(const Plugin& plugin, std::string_view source);

  ChatMessage(const ChatMessage&) = delete;
  ChatMessage& operator=(const ChatMessage&) = delete;

  ChatMessage(ChatMessage&& other) noexcept;
  ChatMessage& operator=(ChatMessage&& other) noexcept;

  ~ChatMessage();

  [[nodiscard]] static ChatMessageResult parse(
      const ArvAPI* api,
      std::string_view source);
  [[nodiscard]] static ChatMessageResult parse(
      const Plugin& plugin,
      std::string_view source);
  [[nodiscard]] static ChatMessageResult from_config(
      const ArvAPI* api,
      const DataDocument& document,
      std::string_view path);
  [[nodiscard]] static ChatMessageResult from_config(
      const Plugin& plugin,
      const DataDocument& document,
      std::string_view path);

  void reset() noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;

  [[nodiscard]] ArvChatMessageHandle abi() const noexcept;

  [[nodiscard]] ValueResult<std::string> to_string() const;
  [[nodiscard]] ValueResult<std::string> to_string_for(ArvPlayerID player) const;
  [[nodiscard]] TextResult as_text() const;
  [[nodiscard]] TextResult as_text_for(ArvPlayerID player) const;

  [[nodiscard]] Result send_to(ArvPlayerID player) const;
  [[nodiscard]] Result send_all(std::span<const ArvPlayerID> players) const;
  [[nodiscard]] Result broadcast() const;
  [[nodiscard]] Result broadcast_world(ArvWorldHandle world) const;

  ChatMessage& with_channel(std::string_view channel);
  ChatMessage& with_kind(ChatMessageKind kind) noexcept;

private:
  ChatMessage(
      const ArvAPI* api,
      ArvChatMessageHandle handle,
      std::string channel,
      ChatMessageKind kind) noexcept;

  [[nodiscard]] Result unsupported() const;
  [[nodiscard]] BufferResult to_buffer(std::optional<ArvPlayerID> player) const;

  const ArvAPI* api_{};
  ArvChatMessageHandle handle_{};
  std::string channel_{"arvane:system"};
  ChatMessageKind kind_{ChatMessageKind::System};
};

struct ChatMessageResult {
  Result result{};
  ChatMessage message{};

  [[nodiscard]] explicit operator bool() const noexcept;
};

} // namespace arv
