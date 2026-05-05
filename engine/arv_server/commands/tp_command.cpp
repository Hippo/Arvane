#include "arv/commands/tp_command.hpp"

#include "arv/command/command.hpp"
#include "arv/server/command_services.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace arv::server::commands {
namespace {

constexpr std::string_view TeleportPermission = "arvane.command.teleport";

[[nodiscard]] command::CommandResult usage_error() {
  return command::CommandResult::usage_error("Usage: /tp [target] <x> <y> <z>.");
}

[[nodiscard]] std::string format_position(Vec3f position) {
  std::ostringstream out;
  out << position.x << ", " << position.y << ", " << position.z;
  return out.str();
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
sender_target(command::CommandCtx& ctx, const ServerCommandServices& services) {
  if (!ctx.sender.player_id.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(
            "Teleport from server console requires an explicit target."));
  }

  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_id(ctx.sender.player_id.value());
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source("Command sender is not an online player."));
  }

  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
named_target(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    std::string_view target_name) {
  if (target_name == "@s") {
    return sender_target(ctx, services);
  }

  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_name(target_name);
  if (!target.has_value()) {
    std::string text{"Unknown online player: "};
    text.append(target_name);
    text.push_back('.');
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::usage_error(std::move(text)));
  }

  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] command::CommandResult execute_tp(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr) {
    return command::CommandResult::internal_error("Server command services are unavailable.");
  }

  if (ctx.args.size() != 3U && ctx.args.size() != 4U) {
    return usage_error();
  }

  command::CommandArgReader args = ctx.reader();
  Expected<OnlinePlayerCommandTarget, command::CommandResult> target =
      Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
          usage_error());
  if (ctx.args.size() == 4U) {
    auto target_name = args.read_token("target player");
    if (!target_name) {
      return target_name.error();
    }
    target = named_target(ctx, *services, target_name.value());
  } else {
    target = sender_target(ctx, *services);
  }
  if (!target) {
    return target.error();
  }

  auto position_arg = args.read_position();
  if (!position_arg) {
    return position_arg.error();
  }

  auto destination = command::resolve_position(position_arg.value(), target.value().position);
  if (!destination) {
    return destination.error();
  }

  auto end = args.expect_end();
  if (!end) {
    return end.error();
  }

  if (!services->teleport_player(target.value().player_id, destination.value())) {
    return command::CommandResult::failure("Unable to teleport player.");
  }

  std::string text{"Teleported "};
  text.append(target.value().display_name);
  text.append(" to ");
  text.append(format_position(destination.value()));
  text.push_back('.');
  return command::CommandResult::success(std::move(text));
}

} // namespace

void register_tp_command(command::CommandRegistry& registry) {
  command::Command tp;
  tp.name = "player.teleport";
  tp.domain = command::CommandDomain::ServerBound;
  tp.permission = std::string{TeleportPermission};
  tp.description = "Teleports an online player to a position.";
  tp.usage = "/tp [target] <x> <y> <z>";
  tp.execute = execute_tp;

  const auto command_result = registry.register_command(std::move(tp));
  assert(command_result && "tp command registration failed");
  const auto alias_result = registry.register_alias("tp", "player.teleport", true);
  assert(alias_result && "tp command alias registration failed");
  (void)command_result;
  (void)alias_result;
}

} // namespace arv::server::commands
