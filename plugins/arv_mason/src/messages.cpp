#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <string>

namespace arv_mason {
namespace {

void replace_all(std::string& text, std::string_view needle, std::string_view value) {
  if (needle.empty()) {
    return;
  }
  for (std::size_t pos = text.find(needle); pos != std::string::npos;
       pos = text.find(needle, pos + value.size())) {
    text.replace(pos, needle.size(), value);
  }
}

[[nodiscard]] arv::ChatMessageKind chat_kind_for(arv::CommandMessageKind kind) noexcept {
  switch (kind) {
    case arv::CommandMessageKind::Info:
    case arv::CommandMessageKind::Success:
    case arv::CommandMessageKind::Warning:
      return arv::ChatMessageKind::System;
    case arv::CommandMessageKind::Error:
      return arv::ChatMessageKind::Error;
  }
  return arv::ChatMessageKind::System;
}

} // namespace

std::string MasonPlugin::message_source(
    std::string_view key,
    MasonMessageArgs args) const {
  std::string message{messages_.get(key)};
  if (message.empty()) {
    const MasonMessages defaults = default_messages();
    message = std::string{defaults.get(key)};
  }
  if (message.empty()) {
    message = std::string{key};
  }
  replace_all(message, "{arv_mason_prefix}", messages_.prefix);
  for (const auto& [name, value] : args) {
    std::string token;
    token.reserve(name.size() + 2U);
    token.push_back('{');
    token.append(name);
    token.push_back('}');
    replace_all(message, token, value);
  }
  return message;
}

void MasonPlugin::reply(
    arv::CommandContext& ctx,
    arv::CommandMessageKind kind,
    std::string_view key,
    MasonMessageArgs args) const {
  std::string source = message_source(key, args);
  arv::ChatMessageResult parsed = arv::ChatMessage::parse(plugin_, source);
  if (parsed) {
    parsed.message.with_channel("arv_mason:messages").with_kind(chat_kind_for(kind));
    if (std::optional<ArvPlayerID> player = ctx.player(); player.has_value()) {
      if (arv::Result sent = parsed.message.send_to(*player); sent) {
        return;
      }
      auto rendered = parsed.message.to_string_for(*player);
      if (rendered) {
        ctx.reply(kind, rendered.value);
        return;
      }
    } else {
      auto rendered = parsed.message.to_string();
      if (rendered) {
        ctx.reply(kind, rendered.value);
        return;
      }
    }
  }
  ctx.reply(kind, source);
}

void MasonPlugin::info(
    arv::CommandContext& ctx,
    std::string_view key,
    MasonMessageArgs args) const {
  reply(ctx, arv::CommandMessageKind::Info, key, args);
}

void MasonPlugin::success(
    arv::CommandContext& ctx,
    std::string_view key,
    MasonMessageArgs args) const {
  reply(ctx, arv::CommandMessageKind::Success, key, args);
}

void MasonPlugin::warn(
    arv::CommandContext& ctx,
    std::string_view key,
    MasonMessageArgs args) const {
  reply(ctx, arv::CommandMessageKind::Warning, key, args);
}

void MasonPlugin::error(
    arv::CommandContext& ctx,
    std::string_view key,
    MasonMessageArgs args) const {
  reply(ctx, arv::CommandMessageKind::Error, key, args);
}

} // namespace arv_mason
