#include "tp_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/lang/language.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace arv::base::commands {
namespace {

constexpr std::string_view TeleportPermission = ids::TeleportPermissionNode;

[[nodiscard]] std::string tr(
    const command::CommandCtx& ctx,
    std::string_view key,
    std::string_view fallback) {
  return ctx.services.language == nullptr ? std::string{fallback}
                                          : ctx.services.language->text(key, fallback);
}

[[nodiscard]] std::string tr(
    const command::CommandCtx& ctx,
    std::string_view key,
    std::initializer_list<lang::FormatArg> args,
    std::string_view fallback) {
  return ctx.services.language == nullptr
             ? lang::LanguageCatalog{}.format(key, args, fallback)
             : ctx.services.language->format(key, args, fallback);
}

[[nodiscard]] command::CommandResult usage_error(const command::CommandCtx& ctx) {
  return command::CommandResult::usage_error(
      tr(ctx, "command.tp.usage", "Usage: /tp [target] <x> <y> <z>."));
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
            tr(
                ctx,
                "command.tp.console_requires_target",
                "Teleport from server console requires an explicit target.")));
  }

  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_id(ctx.sender.player_id.value());
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(tr(
            ctx,
            "command.tp.sender_not_online",
            "Command sender is not an online player.")));
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
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::usage_error(tr(
            ctx,
            "command.tp.unknown_player",
            {{"name", std::string{target_name}}},
            "Unknown online player: {name}.")));
  }

  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] command::CommandResult execute_tp(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->online_player_by_id ||
      !services->online_player_by_name ||
      !services->teleport_player) {
    return command::CommandResult::internal_error(tr(
        ctx,
        "command.tp.services_unavailable",
        "Server command services are unavailable."));
  }

  if (ctx.args.size() != 3U && ctx.args.size() != 4U) {
    return usage_error(ctx);
  }

  command::CommandArgReader args = ctx.reader();
  Expected<OnlinePlayerCommandTarget, command::CommandResult> target =
      Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
          usage_error(ctx));
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
    return command::CommandResult::failure(
        tr(ctx, "command.tp.unable_to_teleport", "Unable to teleport player."));
  }

  return command::CommandResult::success(tr(
      ctx,
      "command.tp.success",
      {{"name", target.value().display_name},
       {"position", format_position(destination.value())}},
      "Teleported {name} to {position}."));
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

} // namespace arv::base::commands
