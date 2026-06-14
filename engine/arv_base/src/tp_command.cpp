#include "tp_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace arv::base::commands {
namespace {

constexpr std::string_view TeleportPermission = ids::TeleportPermissionNode;

[[nodiscard]] command::CommandResult usage_error(const command::CommandCtx& ctx) {
  return command::CommandResult::usage_error(
      arv::text::text_or_fallback(ctx.services.language, "command.tp.usage", "Usage: /tp [target] <x> <y> <z> or /tp <target_player> <destination_player>."));
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
            arv::text::text_or_fallback(ctx.services.language,
                "command.tp.console_requires_target",
                "Teleport from server console requires an explicit target.")));
  }

  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_id(ctx.sender.player_id.value());
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(arv::text::text_or_fallback(ctx.services.language,
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
        command::CommandResult::usage_error(arv::text::format_or_fallback(
            ctx.services.language,
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
      !services->teleport_player ||
      !services->teleport_player_location) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(ctx.services.language,
        "command.tp.services_unavailable",
        "Server command services are unavailable."));
  }

  if (ctx.args.size() != 2U && ctx.args.size() != 3U && ctx.args.size() != 4U) {
    return usage_error(ctx);
  }

  command::CommandArgReader args = ctx.reader();
  if (ctx.args.size() == 2U) {
    auto target_name = args.read_token("target player");
    if (!target_name) {
      return target_name.error();
    }
    Expected<OnlinePlayerCommandTarget, command::CommandResult> target =
        named_target(ctx, *services, target_name.value());
    if (!target) {
      return target.error();
    }

    auto destination_name = args.read_token("destination player");
    if (!destination_name) {
      return destination_name.error();
    }
    Expected<OnlinePlayerCommandTarget, command::CommandResult> destination =
        named_target(ctx, *services, destination_name.value());
    if (!destination) {
      return destination.error();
    }

    auto end = args.expect_end();
    if (!end) {
      return end.error();
    }

    const Location destination_location = location_from_position(
        destination.value().position,
        destination.value().yaw_radians,
        destination.value().pitch_radians,
        destination.value().world_id);
    if (!services->teleport_player_location(
            target.value().player_id,
            destination_location)) {
      return command::CommandResult::failure(
          arv::text::text_or_fallback(ctx.services.language, "command.tp.unable_to_teleport", "Unable to teleport player."));
    }

    return command::CommandResult::success(arv::text::format_or_fallback(
        ctx.services.language,
        "command.tp.success_to_player",
        {{"name", target.value().display_name},
         {"destination", destination.value().display_name},
         {"position", format_position(destination.value().position)}},
        "Teleported {name} to {destination}."));
  }

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
        arv::text::text_or_fallback(ctx.services.language, "command.tp.unable_to_teleport", "Unable to teleport player."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
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
  tp.description = "Teleports an online player to a position or another player.";
  tp.usage = "/tp [target] <x> <y> <z>\n/tp <target_player> <destination_player>";
  tp.arguments = {
      {"target",
       command::CommandArgumentType::String,
       false,
       false,
       {},
       "Online player name or @s."},
      {"destination",
       command::CommandArgumentType::String,
       true,
       false,
       {},
       "Coordinates or online destination player."},
  };
  tp.technical_notes =
      "Uses ServerCommandServices to resolve online players and performs server-authoritative teleports.";
  tp.examples = {"/tp ~ ~5 ~", "/tp local_player 10 20 -30", "/tp @s @s"};
  tp.execute = execute_tp;

  const auto command_result = registry.register_command(std::move(tp));
  assert(command_result && "tp command registration failed");
  const auto alias_result = registry.register_alias("tp", "player.teleport", true);
  assert(alias_result && "tp command alias registration failed");
  (void)command_result;
  (void)alias_result;
}

} // namespace arv::base::commands
