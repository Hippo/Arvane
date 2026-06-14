#include "msg_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <cassert>
#include <optional>
#include <string>

namespace arv::base::commands {
namespace {

[[nodiscard]] command::CommandResult usage_error(const command::CommandCtx& ctx) {
  return command::CommandResult::usage_error(arv::text::text_or_fallback(
      ctx.services.language,
      "command.msg.usage",
      "Usage: /msg <player> <message>."));
}

[[nodiscard]] std::string join_message_tokens(
    const command::CommandArgs& args,
    usize begin) {
  std::string message;
  for (usize i = begin; i < args.size(); ++i) {
    if (!message.empty()) {
      message.push_back(' ');
    }
    message.append(args[i]);
  }
  return message;
}

[[nodiscard]] command::CommandResult execute_msg(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->online_player_by_id ||
      !services->online_player_by_name ||
      !services->send_private_message) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.msg.services_unavailable",
        "Server command services are unavailable."));
  }
  if (!ctx.sender.player_id.has_value()) {
    return command::CommandResult::invalid_source(arv::text::text_or_fallback(
        ctx.services.language,
        "command.msg.player_required",
        "Private messages require an online player sender."));
  }
  if (ctx.args.size() < 2U) {
    return usage_error(ctx);
  }

  command::CommandArgReader args = ctx.reader();
  auto recipient_name = args.read_token("recipient player");
  if (!recipient_name) {
    return recipient_name.error();
  }

  std::optional<OnlinePlayerCommandTarget> recipient;
  if (recipient_name.value() == "@s") {
    recipient = services->online_player_by_id(ctx.sender.player_id.value());
  } else {
    recipient = services->online_player_by_name(recipient_name.value());
  }
  if (!recipient.has_value()) {
    return command::CommandResult::usage_error(arv::text::format_or_fallback(
        ctx.services.language,
        "command.msg.unknown_player",
        {{"name", std::string{recipient_name.value()}}},
        "Unknown online player: {name}."));
  }

  const std::string message = join_message_tokens(ctx.args, args.cursor());
  if (message.empty()) {
    return usage_error(ctx);
  }

  const std::string sender_name = ctx.sender.display_name.empty()
                                      ? std::string{"server"}
                                      : ctx.sender.display_name;
  std::string recipient_text;
  recipient_text.reserve(sender_name.size() + message.size() + 10U);
  recipient_text.append("[");
  recipient_text.append(sender_name);
  recipient_text.append(" -> you] ");
  recipient_text.append(message);

  if (!services->send_private_message(recipient->session_id, recipient_text)) {
    return command::CommandResult::failure(arv::text::text_or_fallback(
        ctx.services.language,
        "command.msg.unable_to_send",
        "Unable to send private message."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
      "command.msg.sent",
      {{"name", recipient->display_name}},
      "Message sent to {name}."));
}

} // namespace

void register_msg_command(command::CommandRegistry& registry) {
  command::Command msg;
  msg.name = "player.message";
  msg.domain = command::CommandDomain::ServerBound;
  msg.permission = std::string{ids::MsgPermissionNode};
  msg.description = "Sends a private message to an online player.";
  msg.usage = "/msg <player> <message>";
  msg.arguments = {
      {"player",
       command::CommandArgumentType::String,
       true,
       false,
       {},
       "Online player name."},
      {"message",
       command::CommandArgumentType::Message,
       true,
       true,
       {},
       "Message text."},
  };
  msg.technical_notes =
      "Resolves online players through ServerCommandServices and sends a targeted private chat packet.";
  msg.examples = {"/msg local_player hello", "/msg @s \"checking private chat\""};
  msg.execute = execute_msg;

  const auto command_result = registry.register_command(std::move(msg));
  assert(command_result && "msg command registration failed");
  const auto alias_result = registry.register_alias("msg", "player.message", true);
  assert(alias_result && "msg command alias registration failed");
  (void)command_result;
  (void)alias_result;
}

} // namespace arv::base::commands
