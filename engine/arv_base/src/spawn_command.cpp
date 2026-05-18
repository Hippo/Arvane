#include "spawn_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <cassert>
#include <sstream>
#include <string>

namespace arv::base::commands {
namespace {

[[nodiscard]] std::string format_position(Vec3f position) {
  std::ostringstream out;
  out << position.x << ", " << position.y << ", " << position.z;
  return out.str();
}

[[nodiscard]] command::CommandResult services_unavailable(
    const command::CommandCtx& ctx) {
  return command::CommandResult::internal_error(arv::text::text_or_fallback(
      ctx.services.language,
      "command.spawn.services_unavailable",
      "Server command services are unavailable."));
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
sender_target(command::CommandCtx& ctx, const ServerCommandServices& services) {
  if (!ctx.sender.player_id.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(arv::text::text_or_fallback(
            ctx.services.language,
            "command.spawn.player_required",
            "This command requires an online player sender.")));
  }

  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_id(ctx.sender.player_id.value());
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(arv::text::text_or_fallback(
            ctx.services.language,
            "command.spawn.sender_not_online",
            "Command sender is not an online player.")));
  }

  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] command::CommandResult execute_spawn(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->online_player_by_id ||
      !services->read_world_spawn ||
      !services->teleport_player_location) {
    return services_unavailable(ctx);
  }
  if (!ctx.args.empty()) {
    return command::CommandResult::usage_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.spawn.usage",
        "Usage: /spawn."));
  }

  auto target = sender_target(ctx, *services);
  if (!target) {
    return target.error();
  }

  const std::optional<Location> spawn =
      services->read_world_spawn(target.value().world_id);
  if (!spawn.has_value()) {
    return command::CommandResult::failure(arv::text::text_or_fallback(
        ctx.services.language,
        "command.spawn.unavailable",
        "World spawn is unavailable."));
  }

  if (!services->teleport_player_location(target.value().player_id, spawn.value())) {
    return command::CommandResult::failure(arv::text::text_or_fallback(
        ctx.services.language,
        "command.spawn.unable_to_teleport",
        "Unable to teleport player to world spawn."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
      "command.spawn.success",
      {{"position", format_position(location_position(*spawn))}},
      "Teleported to world spawn at {position}."));
}

[[nodiscard]] command::CommandResult execute_setspawn(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->online_player_by_id ||
      !services->write_world_spawn) {
    return services_unavailable(ctx);
  }
  if (!ctx.args.empty()) {
    return command::CommandResult::usage_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.setspawn.usage",
        "Usage: /setspawn."));
  }

  auto target = sender_target(ctx, *services);
  if (!target) {
    return target.error();
  }

  const Location spawn{
      target.value().position.x,
      target.value().position.y,
      target.value().position.z,
      target.value().yaw_radians,
      target.value().pitch_radians,
      target.value().world_id};
  if (!services->write_world_spawn(target.value().world_id, spawn)) {
    return command::CommandResult::failure(arv::text::text_or_fallback(
        ctx.services.language,
        "command.setspawn.unable_to_set",
        "Unable to set world spawn."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
      "command.setspawn.success",
      {{"position", format_position(location_position(spawn))}},
      "Set world spawn to {position}."));
}

} // namespace

void register_spawn_commands(command::CommandRegistry& registry) {
  command::Command spawn;
  spawn.name = "world.spawn";
  spawn.domain = command::CommandDomain::ServerBound;
  spawn.permission = std::string{ids::SpawnPermissionNode};
  spawn.description = "Teleports the sender to the world spawn.";
  spawn.usage = "/spawn";
  spawn.execute = execute_spawn;

  const auto spawn_result = registry.register_command(std::move(spawn));
  assert(spawn_result && "spawn command registration failed");
  const auto spawn_alias_result =
      registry.register_alias("spawn", "world.spawn", true);
  assert(spawn_alias_result && "spawn command alias registration failed");

  command::Command setspawn;
  setspawn.name = "world.setspawn";
  setspawn.domain = command::CommandDomain::ServerBound;
  setspawn.permission = std::string{ids::SetSpawnPermissionNode};
  setspawn.description = "Sets the world spawn to the sender's current pose.";
  setspawn.usage = "/setspawn";
  setspawn.execute = execute_setspawn;

  const auto setspawn_result = registry.register_command(std::move(setspawn));
  assert(setspawn_result && "setspawn command registration failed");
  const auto setspawn_alias_result =
      registry.register_alias("setspawn", "world.setspawn", true);
  assert(setspawn_alias_result && "setspawn command alias registration failed");

  (void)spawn_result;
  (void)spawn_alias_result;
  (void)setspawn_result;
  (void)setspawn_alias_result;
}

} // namespace arv::base::commands
