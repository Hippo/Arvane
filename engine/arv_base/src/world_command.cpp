#include "world_command.hpp"

#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/core/string.hpp"
#include "arv/text/language.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <sstream>
#include <string>
#include <string_view>

namespace arv::base::commands {
namespace {

constexpr std::string_view WorldPermission = "arv.command.world";
constexpr std::string_view WorldTeleportPermission = "arv.command.world.teleport";
constexpr std::string_view WorldTeleportOthersPermission = "arv.command.world.teleport.others";
constexpr std::string_view WorldCreatePermission = "arv.command.world.create";
constexpr std::string_view WorldDeletePermission = "arv.command.world.delete";
constexpr std::string_view WorldLoadPermission = "arv.command.world.load";
constexpr std::string_view WorldSpawnPermission = "arv.command.world.spawn";
constexpr std::string_view WorldSetSpawnPermission = "arv.command.world.setspawn";
constexpr std::string_view WorldModifyPermission = "arv.command.world.modify";

struct HelpEntry {
  std::string_view usage{};
  std::string_view description{};
  std::string_view permission{};
};

constexpr HelpEntry HelpEntries[] = {
    {"/world help [page]", "Shows paged help for world commands.", WorldPermission},
    {"/world list", "Lists all known worlds.", WorldPermission},
    {"/world info [world]", "Shows details for a world.", WorldPermission},
    {"/world create <name> [--seed <seed>] [--generator <id>] [--type <type>]",
     "Creates a new world.",
     WorldCreatePermission},
    {"/world tp <world> [player]", "Teleports to a world spawn.", WorldTeleportPermission},
    {"/world spawn [world] [player]", "Teleports to a world spawn.", WorldSpawnPermission},
    {"/world setspawn [world]", "Sets a world spawn to your current pose.", WorldSetSpawnPermission},
    {"/world load <world>", "Loads a known world.", WorldLoadPermission},
    {"/world unload <world> [--force]", "Unloads a loaded world.", WorldLoadPermission},
    {"/world delete <world> --confirm", "Permanently deletes an unloaded world.", WorldDeletePermission},
    {"/world rename <old_name> <new_name>", "Renames an unloaded world.", WorldModifyPermission},
    {"/world default [world]", "Shows or sets the server default world.", WorldPermission},
};

constexpr usize HelpPageSize = 6U;

[[nodiscard]] std::string msg(
    const command::CommandCtx& ctx,
    std::string_view key,
    std::string_view fallback) {
  return arv::text::text_or_fallback(ctx.services.language, key, fallback);
}

[[nodiscard]] std::string format_msg(
    const command::CommandCtx& ctx,
    std::string_view key,
    std::initializer_list<arv::text::FormatArg> args,
    std::string_view fallback) {
  return arv::text::format_or_fallback(ctx.services.language, key, args, fallback);
}

[[nodiscard]] command::CommandResult world_permission_denied() {
  return command::CommandResult::permission_denied(
      "You do not have permission to use this world command.");
}

[[nodiscard]] command::CommandResult check_world_permission(
    command::CommandCtx& ctx,
    std::string_view permission) {
  if (ctx.sender.kind != command::CommandSourceKind::Player) {
    return command::CommandResult::success();
  }
  if (!ctx.services.permission_check) {
    return command::CommandResult::internal_error(msg(
        ctx,
        "command.dispatcher.permission_service_unavailable",
        "Command permission service is unavailable."));
  }

  command::Command checked = ctx.command;
  checked.permission = std::string{permission};
  const command::CommandPermissionResult result =
      ctx.services.permission_check(ctx.sender, checked);
  return result.allowed ? command::CommandResult::success()
                        : world_permission_denied();
}

[[nodiscard]] command::CommandResult usage() {
  return command::CommandResult::usage_error("Usage: /world help [page].");
}

[[nodiscard]] bool parse_u32(std::string_view text, u32& out) noexcept {
  const char* begin = text.data();
  const char* end = begin + text.size();
  u32 value{};
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return false;
  }
  out = value;
  return true;
}

[[nodiscard]] bool parse_u64(std::string_view text, u64& out) noexcept {
  const char* begin = text.data();
  const char* end = begin + text.size();
  u64 value{};
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return false;
  }
  out = value;
  return true;
}

[[nodiscard]] bool safe_world_name_char(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '_' ||
         ch == '-';
}

[[nodiscard]] bool validate_world_name(
    std::string_view name,
    std::string& error) {
  if (name.empty()) {
    error = "World name cannot be empty.";
    return false;
  }
  if (name == "." || name == ".." || name.find("..") != std::string_view::npos ||
      name.find('/') != std::string_view::npos ||
      name.find('\\') != std::string_view::npos) {
    error = "World name cannot contain path traversal or slashes.";
    return false;
  }
  for (const char ch : name) {
    if (!safe_world_name_char(ch)) {
      error = "World names must use lowercase letters, numbers, '_' or '-'.";
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string format_position(Vec3f position) {
  std::ostringstream out;
  out << position.x << ", " << position.y << ", " << position.z;
  return out.str();
}

[[nodiscard]] std::string world_status(const command::WorldCommandInfo& world) {
  return world.loaded ? "loaded" : "unloaded";
}

[[nodiscard]] std::string display_seed(const command::WorldCommandInfo& world) {
  return world.seed_visible ? std::to_string(world.seed) : "hidden";
}

[[nodiscard]] command::CommandResult services_unavailable(
    const command::CommandCtx& ctx) {
  return command::CommandResult::internal_error(msg(
      ctx,
      "command.world.services_unavailable",
      "World command services are unavailable."));
}

[[nodiscard]] const ServerCommandServices* world_services(
    const command::CommandCtx& ctx) {
  return ctx.services.as<ServerCommandServices>();
}

[[nodiscard]] std::optional<command::WorldCommandInfo> world_by_sender_or_default(
    const ServerCommandServices& services,
    const command::CommandSender& sender) {
  if (sender.world_id.has_value() && services.list_worlds) {
    for (const command::WorldCommandInfo& world : services.list_worlds()) {
      if (world.id == sender.world_id.value()) {
        return world;
      }
    }
  }
  if (services.default_world_name && services.world_info) {
    const std::optional<std::string> default_name = services.default_world_name();
    if (default_name.has_value()) {
      return services.world_info(default_name.value());
    }
  }
  return std::nullopt;
}

[[nodiscard]] command::CommandResult unknown_world(
    std::string_view world_name) {
  return command::CommandResult::failure(
      "Unknown world '" + std::string{world_name} +
      "'. Use /world list to see available worlds.");
}

[[nodiscard]] std::optional<command::WorldCommandInfo> find_world(
    const ServerCommandServices& services,
    std::string_view name) {
  if (!services.world_info) {
    return std::nullopt;
  }
  return services.world_info(name);
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
sender_target(command::CommandCtx& ctx, const ServerCommandServices& services) {
  if (!ctx.sender.player_id.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(
            "This world command requires an online player sender."));
  }
  if (!services.online_player_by_id) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        services_unavailable(ctx));
  }
  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_id(ctx.sender.player_id.value());
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(
            "Command sender is not an online player."));
  }
  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
named_target(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    std::string_view name) {
  if (name == "@s") {
    return sender_target(ctx, services);
  }
  if (!services.online_player_by_name) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        services_unavailable(ctx));
  }
  const std::optional<OnlinePlayerCommandTarget> target =
      services.online_player_by_name(name);
  if (!target.has_value()) {
    return Expected<OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::usage_error("Unknown online player: " + std::string{name} + "."));
  }
  return Expected<OnlinePlayerCommandTarget, command::CommandResult>::success(
      target.value());
}

[[nodiscard]] Expected<OnlinePlayerCommandTarget, command::CommandResult>
target_for_optional_player(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    std::optional<std::string_view> player_name) {
  if (player_name.has_value()) {
    return named_target(ctx, services, player_name.value());
  }
  return sender_target(ctx, services);
}

[[nodiscard]] command::CommandResult execute_help(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /world help [page].");
  }
  u32 requested_page = 1U;
  if (ctx.args.size() == 1U && ctx.args[0] != "help" &&
      !parse_u32(ctx.args[0], requested_page)) {
    return command::CommandResult::usage_error("World help page must be a positive number.");
  }
  if (ctx.args.size() == 2U && !parse_u32(ctx.args[1], requested_page)) {
    return command::CommandResult::usage_error("World help page must be a positive number.");
  }
  if (requested_page == 0U) {
    requested_page = 1U;
  }
  const u32 page_count =
      static_cast<u32>((std::size(HelpEntries) + HelpPageSize - 1U) / HelpPageSize);
  const u32 page = std::min(requested_page, page_count);
  const usize start = static_cast<usize>(page - 1U) * HelpPageSize;
  const usize end = std::min<usize>(start + HelpPageSize, std::size(HelpEntries));

  command::CommandResult result = command::CommandResult::success();
  result.add_message(
      command::CommandMessageKind::Info,
      "World commands page " + std::to_string(page) + "/" +
          std::to_string(page_count) + ":");
  for (usize index = start; index < end; ++index) {
    const HelpEntry& entry = HelpEntries[index];
    result.add_message(
        command::CommandMessageKind::Info,
        std::string{entry.usage} + " - " + std::string{entry.description} +
            " Permission: " + std::string{entry.permission});
  }
  return result;
}

[[nodiscard]] command::CommandResult execute_list(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() != 1U) {
    return command::CommandResult::usage_error("Usage: /world list.");
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->list_worlds) {
    return services_unavailable(ctx);
  }
  const std::vector<command::WorldCommandInfo> worlds = services->list_worlds();
  command::CommandResult result = command::CommandResult::success();
  result.add_message(
      command::CommandMessageKind::Info,
      "Worlds (" + std::to_string(worlds.size()) + "):");
  for (const command::WorldCommandInfo& world : worlds) {
    std::string line = "  " + world.name;
    if (world.is_default) {
      line.append(" [default]");
    }
    line.append(" - ");
    line.append(world_status(world));
    line.append(", players=");
    line.append(std::to_string(world.player_count));
    line.append(", seed=");
    line.append(display_seed(world));
    line.append(", generator=");
    line.append(world.generator_id.empty() ? "unknown" : world.generator_id);
    line.append(", type=");
    line.append(world.world_type.empty() ? "default" : world.world_type);
    result.add_message(command::CommandMessageKind::Info, std::move(line));
  }
  return result;
}

[[nodiscard]] command::CommandResult add_world_info_messages(
    command::CommandResult result,
    const command::WorldCommandInfo& world) {
  result.add_message(command::CommandMessageKind::Info, "World: " + world.name);
  result.add_message(command::CommandMessageKind::Info, "Status: " + world_status(world));
  result.add_message(
      command::CommandMessageKind::Info,
      "Default: " + std::string{world.is_default ? "yes" : "no"});
  result.add_message(
      command::CommandMessageKind::Info,
      "Players: " + std::to_string(world.player_count));
  result.add_message(
      command::CommandMessageKind::Info,
      "Seed: " + display_seed(world));
  result.add_message(
      command::CommandMessageKind::Info,
      "Generator: " + (world.generator_id.empty() ? std::string{"unknown"} : world.generator_id));
  result.add_message(
      command::CommandMessageKind::Info,
      "Type: " + (world.world_type.empty() ? std::string{"default"} : world.world_type));
  if (world.spawn.has_value()) {
    result.add_message(
        command::CommandMessageKind::Info,
        "Spawn: " + format_position(location_position(world.spawn.value())));
  }
  if (!world.path.empty()) {
    result.add_message(command::CommandMessageKind::Info, "Path: " + world.path);
  }
  if (!world.created_at.empty()) {
    result.add_message(command::CommandMessageKind::Info, "Created: " + world.created_at);
  }
  return result;
}

[[nodiscard]] command::CommandResult execute_info(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /world info [world].");
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->world_info) {
    return services_unavailable(ctx);
  }

  std::optional<command::WorldCommandInfo> world;
  if (ctx.args.size() == 2U) {
    world = find_world(*services, ctx.args[1]);
    if (!world.has_value()) {
      return unknown_world(ctx.args[1]);
    }
  } else {
    world = world_by_sender_or_default(*services, ctx.sender);
    if (!world.has_value()) {
      return command::CommandResult::failure("Current world is unavailable.");
    }
  }
  return add_world_info_messages(command::CommandResult::success(), world.value());
}

[[nodiscard]] command::CommandResult execute_create(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldCreatePermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() < 2U) {
    return command::CommandResult::usage_error(
        "Usage: /world create <name> [--seed <seed>] [--generator <id>] [--type <type>].");
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->create_world) {
    return services_unavailable(ctx);
  }

  command::WorldCreateCommandRequest request;
  request.name = std::string{ctx.args[1]};
  std::string validation_error;
  if (!validate_world_name(request.name, validation_error)) {
    return command::CommandResult::usage_error(std::move(validation_error));
  }
  for (usize index = 2U; index < ctx.args.size(); ++index) {
    const std::string_view flag = ctx.args[index];
    if (flag == "--seed") {
      if (++index >= ctx.args.size()) {
        return command::CommandResult::usage_error("--seed requires a value.");
      }
      u64 seed = 0U;
      if (!parse_u64(ctx.args[index], seed)) {
        return command::CommandResult::usage_error("World seed must be an unsigned integer.");
      }
      request.seed = seed;
    } else if (flag == "--generator") {
      if (++index >= ctx.args.size()) {
        return command::CommandResult::usage_error("--generator requires a value.");
      }
      request.generator_id = std::string{ctx.args[index]};
    } else if (flag == "--type") {
      if (++index >= ctx.args.size()) {
        return command::CommandResult::usage_error("--type requires a value.");
      }
      request.world_type = arv::lower_ascii_copy(ctx.args[index]);
    } else {
      return command::CommandResult::usage_error("Unknown world create option: " + std::string{flag} + ".");
    }
  }

  const command::WorldOperationCommandResult created = services->create_world(request);
  if (!created.success) {
    return command::CommandResult::failure(
        created.message.empty() ? "Unable to create world." : created.message);
  }
  return command::CommandResult::success(
      created.message.empty()
          ? "Created world '" + request.name + "'."
          : created.message);
}

[[nodiscard]] std::optional<Location> spawn_for_world(
    const ServerCommandServices& services,
    const command::WorldCommandInfo& world) {
  if (world.spawn.has_value()) {
    Location spawn = world.spawn.value();
    spawn.world = world.id;
    return spawn;
  }
  if (services.read_world_spawn) {
    return services.read_world_spawn(world.id);
  }
  return std::nullopt;
}

[[nodiscard]] command::CommandResult teleport_to_world_spawn(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    const OnlinePlayerCommandTarget& target,
    const command::WorldCommandInfo& world) {
  if (!world.loaded) {
    return command::CommandResult::failure(
        "World '" + world.name + "' is not loaded. Use /world load " + world.name + " first.");
  }
  if (services.teleport_player_to_world_spawn) {
    const command::WorldOperationCommandResult result =
        services.teleport_player_to_world_spawn(target.player_id, world.name);
    if (!result.success) {
      return command::CommandResult::failure(
          result.message.empty() ? "Unable to teleport player to world." : result.message);
    }
    return command::CommandResult::success(
        result.message.empty()
            ? "Teleported " + target.display_name + " to world '" + world.name + "'."
            : result.message);
  }
  if (!services.teleport_player_location) {
    return services_unavailable(ctx);
  }
  const std::optional<Location> spawn = spawn_for_world(services, world);
  if (!spawn.has_value()) {
    return command::CommandResult::failure(
        "World spawn is unavailable for '" + world.name + "'.");
  }
  if (!services.teleport_player_location(target.player_id, spawn.value())) {
    return command::CommandResult::failure("Unable to teleport player to world spawn.");
  }
  return command::CommandResult::success(
      "Teleported " + target.display_name + " to world '" + world.name + "'.");
}

[[nodiscard]] command::CommandResult execute_tp(command::CommandCtx& ctx) {
  if (ctx.args.size() != 2U && ctx.args.size() != 3U) {
    return command::CommandResult::usage_error("Usage: /world tp <world> [player].");
  }
  const bool targets_other = ctx.args.size() == 3U;
  const command::CommandResult permission = check_world_permission(
      ctx,
      targets_other ? WorldTeleportOthersPermission : WorldTeleportPermission);
  if (!permission) {
    return permission;
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->world_info) {
    return services_unavailable(ctx);
  }
  std::optional<command::WorldCommandInfo> world = find_world(*services, ctx.args[1]);
  if (!world.has_value()) {
    return unknown_world(ctx.args[1]);
  }
  auto target = target_for_optional_player(
      ctx,
      *services,
      targets_other ? std::optional<std::string_view>{ctx.args[2]} : std::nullopt);
  if (!target) {
    return target.error();
  }
  return teleport_to_world_spawn(ctx, *services, target.value(), world.value());
}

[[nodiscard]] command::CommandResult execute_spawn(command::CommandCtx& ctx) {
  if (ctx.args.size() > 3U) {
    return command::CommandResult::usage_error("Usage: /world spawn [world] [player].");
  }
  const bool targets_other = ctx.args.size() == 3U;
  const command::CommandResult permission = check_world_permission(
      ctx,
      targets_other ? WorldTeleportOthersPermission : WorldSpawnPermission);
  if (!permission) {
    return permission;
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->world_info) {
    return services_unavailable(ctx);
  }

  std::optional<command::WorldCommandInfo> world;
  if (ctx.args.size() >= 2U) {
    world = find_world(*services, ctx.args[1]);
    if (!world.has_value()) {
      return unknown_world(ctx.args[1]);
    }
  } else {
    world = world_by_sender_or_default(*services, ctx.sender);
    if (!world.has_value()) {
      return command::CommandResult::failure("Current world is unavailable.");
    }
  }
  auto target = target_for_optional_player(
      ctx,
      *services,
      targets_other ? std::optional<std::string_view>{ctx.args[2]} : std::nullopt);
  if (!target) {
    return target.error();
  }
  return teleport_to_world_spawn(ctx, *services, target.value(), world.value());
}

[[nodiscard]] command::CommandResult execute_setspawn(command::CommandCtx& ctx) {
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /world setspawn [world].");
  }
  const command::CommandResult permission = check_world_permission(ctx, WorldSetSpawnPermission);
  if (!permission) {
    return permission;
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->world_info) {
    return services_unavailable(ctx);
  }
  auto target = sender_target(ctx, *services);
  if (!target) {
    return target.error();
  }
  std::optional<command::WorldCommandInfo> world;
  if (ctx.args.size() == 2U) {
    world = find_world(*services, ctx.args[1]);
    if (!world.has_value()) {
      return unknown_world(ctx.args[1]);
    }
  } else {
    world = world_by_sender_or_default(*services, ctx.sender);
    if (!world.has_value()) {
      return command::CommandResult::failure("Current world is unavailable.");
    }
  }
  Location spawn{
      target.value().position.x,
      target.value().position.y,
      target.value().position.z,
      target.value().yaw_radians,
      target.value().pitch_radians,
      world->id};
  if (services->set_world_spawn) {
    const command::WorldOperationCommandResult result =
        services->set_world_spawn(world->name, spawn);
    if (!result.success) {
      return command::CommandResult::failure(
          result.message.empty() ? "Unable to set world spawn." : result.message);
    }
    return command::CommandResult::success(
        result.message.empty()
            ? "Set spawn for world '" + world->name + "'."
            : result.message);
  }
  if (!services->write_world_spawn || !services->write_world_spawn(world->id, spawn)) {
    return command::CommandResult::failure("Unable to set world spawn.");
  }
  return command::CommandResult::success(
      "Set spawn for world '" + world->name + "' to " +
      format_position(location_position(spawn)) + ".");
}

[[nodiscard]] command::CommandResult execute_load(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldLoadPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() != 2U) {
    return command::CommandResult::usage_error("Usage: /world load <world>.");
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->load_world) {
    return services_unavailable(ctx);
  }
  const command::WorldOperationCommandResult result = services->load_world(ctx.args[1]);
  return result.success
             ? command::CommandResult::success(result.message.empty()
                                                   ? "Loaded world '" + std::string{ctx.args[1]} + "'."
                                                   : result.message)
             : command::CommandResult::failure(result.message.empty()
                                                   ? "Unable to load world."
                                                   : result.message);
}

[[nodiscard]] command::CommandResult execute_unload(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldLoadPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() != 2U && ctx.args.size() != 3U) {
    return command::CommandResult::usage_error("Usage: /world unload <world> [--force].");
  }
  bool force = false;
  if (ctx.args.size() == 3U) {
    if (ctx.args[2] != "--force") {
      return command::CommandResult::usage_error("Usage: /world unload <world> [--force].");
    }
    force = true;
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->unload_world) {
    return services_unavailable(ctx);
  }
  const command::WorldOperationCommandResult result =
      services->unload_world(ctx.args[1], force);
  return result.success
             ? command::CommandResult::success(result.message.empty()
                                                   ? "Unloaded world '" + std::string{ctx.args[1]} + "'."
                                                   : result.message)
             : command::CommandResult::failure(result.message.empty()
                                                   ? "Unable to unload world."
                                                   : result.message);
}

[[nodiscard]] command::CommandResult execute_delete(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldDeletePermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() != 2U && ctx.args.size() != 3U) {
    return command::CommandResult::usage_error("Usage: /world delete <world> --confirm.");
  }
  if (ctx.args.size() == 2U) {
    return command::CommandResult::with_message(
        command::CommandStatus::Failure,
        command::CommandMessageKind::Warning,
        "This will permanently delete world '" + std::string{ctx.args[1]} +
            "'. Run /world delete " + std::string{ctx.args[1]} +
            " --confirm to continue.");
  }
  if (ctx.args[2] != "--confirm") {
    return command::CommandResult::usage_error("Usage: /world delete <world> --confirm.");
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->delete_world) {
    return services_unavailable(ctx);
  }
  const command::WorldOperationCommandResult result = services->delete_world(ctx.args[1]);
  return result.success
             ? command::CommandResult::success(result.message.empty()
                                                   ? "Deleted world '" + std::string{ctx.args[1]} + "'."
                                                   : result.message)
             : command::CommandResult::failure(result.message.empty()
                                                   ? "Unable to delete world."
                                                   : result.message);
}

[[nodiscard]] command::CommandResult execute_rename(command::CommandCtx& ctx) {
  const command::CommandResult permission = check_world_permission(ctx, WorldModifyPermission);
  if (!permission) {
    return permission;
  }
  if (ctx.args.size() != 3U) {
    return command::CommandResult::usage_error("Usage: /world rename <old_name> <new_name>.");
  }
  std::string validation_error;
  if (!validate_world_name(ctx.args[2], validation_error)) {
    return command::CommandResult::usage_error(std::move(validation_error));
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->rename_world) {
    return services_unavailable(ctx);
  }
  const command::WorldOperationCommandResult result =
      services->rename_world(ctx.args[1], ctx.args[2]);
  return result.success
             ? command::CommandResult::success(result.message.empty()
                                                   ? "Renamed world."
                                                   : result.message)
             : command::CommandResult::failure(result.message.empty()
                                                   ? "Unable to rename world."
                                                   : result.message);
}

[[nodiscard]] command::CommandResult execute_default(command::CommandCtx& ctx) {
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /world default [world].");
  }
  const bool write = ctx.args.size() == 2U;
  const command::CommandResult permission =
      check_world_permission(ctx, write ? WorldModifyPermission : WorldPermission);
  if (!permission) {
    return permission;
  }
  const ServerCommandServices* services = world_services(ctx);
  if (services == nullptr || !services->default_world_name) {
    return services_unavailable(ctx);
  }
  if (!write) {
    const std::optional<std::string> name = services->default_world_name();
    return name.has_value()
               ? command::CommandResult::success("Default world is '" + name.value() + "'.")
               : command::CommandResult::failure("Default world is unavailable.");
  }
  if (!services->set_default_world) {
    return services_unavailable(ctx);
  }
  const command::WorldOperationCommandResult result =
      services->set_default_world(ctx.args[1]);
  return result.success
             ? command::CommandResult::success(result.message.empty()
                                                   ? "Default world is now '" + std::string{ctx.args[1]} + "'."
                                                   : result.message)
             : command::CommandResult::failure(result.message.empty()
                                                   ? "Unable to set default world."
                                                   : result.message);
}

[[nodiscard]] command::CommandResult execute_world(command::CommandCtx& ctx) {
  if (ctx.args.empty()) {
    command::CommandArgs help_args;
    command::CommandCtx help_ctx{ctx.command, ctx.sender, help_args, ctx.services};
    return execute_help(help_ctx);
  }
  const std::string subcommand = arv::lower_ascii_copy(ctx.args[0]);
  if (subcommand == "help") {
    return execute_help(ctx);
  }
  if (subcommand == "list") {
    return execute_list(ctx);
  }
  if (subcommand == "info") {
    return execute_info(ctx);
  }
  if (subcommand == "create") {
    return execute_create(ctx);
  }
  if (subcommand == "tp" || subcommand == "teleport") {
    return execute_tp(ctx);
  }
  if (subcommand == "spawn") {
    return execute_spawn(ctx);
  }
  if (subcommand == "setspawn") {
    return execute_setspawn(ctx);
  }
  if (subcommand == "load") {
    return execute_load(ctx);
  }
  if (subcommand == "unload") {
    return execute_unload(ctx);
  }
  if (subcommand == "delete") {
    return execute_delete(ctx);
  }
  if (subcommand == "rename") {
    return execute_rename(ctx);
  }
  if (subcommand == "default") {
    return execute_default(ctx);
  }
  return command::CommandResult::usage_error(
      "Unknown world subcommand '" + std::string{ctx.args[0]} +
      "'. Use /world help.");
}

} // namespace

void register_world_command(command::CommandRegistry& registry) {
  command::Command world;
  world.name = "world.manager";
  world.domain = command::CommandDomain::ServerBound;
  world.permission = std::string{WorldPermission};
  world.description = "Manages server worlds.";
  world.usage =
      "/world help [page]\n"
      "/world list\n"
      "/world info [world]\n"
      "/world create <name> [--seed <seed>] [--generator <id>] [--type <type>]\n"
      "/world tp <world> [player]\n"
      "/world spawn [world] [player]\n"
      "/world setspawn [world]\n"
      "/world load <world>\n"
      "/world unload <world> [--force]\n"
      "/world delete <world> --confirm\n"
      "/world rename <old_name> <new_name>\n"
      "/world default [world]";
  world.arguments = {
      {"subcommand",
       command::CommandArgumentType::Enum,
       true,
       false,
       {"help", "list", "info", "create", "tp", "teleport", "spawn", "setspawn", "load", "unload", "delete", "rename", "default"},
       "World manager action."},
      {"arguments",
       command::CommandArgumentType::Metadata,
       false,
       true,
       {},
       "Subcommand-specific arguments."},
  };
  world.technical_notes =
      "Parses subcommands server-side and checks each world permission node before calling ServerCommandServices world operations.";
  world.public_to_players = true;
  world.examples = {
      "/world list",
      "/world create mining",
      "/world create creative --seed 12345",
      "/world create flat_test --generator arvane:flat",
      "/world tp mining Hippo",
      "/world delete mining --confirm"};
  world.execute = execute_world;

  const auto command_result = registry.register_command(std::move(world));
  assert(command_result && "world command registration failed");
  const auto alias_world = registry.register_alias("world", "world.manager", true);
  const auto alias_worlds = registry.register_alias("worlds", "world.manager", true);
  const auto alias_wm = registry.register_alias("wm", "world.manager", true);
  assert(alias_world && "world command alias registration failed");
  assert(alias_worlds && "worlds command alias registration failed");
  assert(alias_wm && "wm command alias registration failed");
  (void)command_result;
  (void)alias_world;
  (void)alias_worlds;
  (void)alias_wm;
}

} // namespace arv::base::commands
