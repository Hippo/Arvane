#include "core_commands.hpp"

#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/string.hpp"
#include "arv/core/time.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/world/world_time.hpp"
#include "arv/text/language.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace arv::base::commands {
namespace {

constexpr usize HelpPageSize = 8U;
constexpr u32 MaxSummonCount = 64U;
constexpr u32 MaxGiveCount = 4096U;

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

[[nodiscard]] std::string command_name_for_display(const command::Command& definition) {
  if (definition.name.find('.') == std::string::npos || definition.aliases.empty()) {
    return definition.name;
  }
  return definition.aliases.front();
}

[[nodiscard]] std::string slash(std::string_view command_name) {
  std::string out{"/"};
  out.append(command_name);
  return out;
}

[[nodiscard]] std::string join_tokens(const command::CommandArgs& args, usize start) {
  std::string out;
  for (usize index = start; index < args.size(); ++index) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.append(args[index]);
  }
  return out;
}

[[nodiscard]] std::string format_position(Vec3f position) {
  std::ostringstream out;
  out << position.x << ", " << position.y << ", " << position.z;
  return out.str();
}

[[nodiscard]] std::string format_block_pos(BlockPos pos) {
  std::ostringstream out;
  out << pos.value.x << ", " << pos.value.y << ", " << pos.value.z;
  return out.str();
}

[[nodiscard]] std::string format_chunk_pos(ChunkSectionCoord pos) {
  std::ostringstream out;
  out << pos.value.x << ", " << pos.value.y << ", " << pos.value.z;
  return out.str();
}

[[nodiscard]] BlockPos floor_block_pos(Vec3f position) noexcept {
  return {{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }};
}

[[nodiscard]] WorldID sender_world_id(const command::CommandSender& sender) noexcept {
  return sender.world_id.value_or(server::DefaultWorldID);
}

[[nodiscard]] std::optional<Vec3f> sender_position(
    const command::CommandSender& sender,
    const ServerCommandServices& services) {
  if (sender.position.has_value()) {
    return sender.position;
  }
  if (sender.player_id.has_value() && services.online_player_by_id) {
    const std::optional<command::OnlinePlayerCommandTarget> player =
        services.online_player_by_id(sender.player_id.value());
    if (player.has_value()) {
      return player->position;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool has_namespaced_id(std::string_view id) noexcept {
  return !id.empty() && id.find(':') != std::string_view::npos;
}

[[nodiscard]] bool parse_positive_u32(std::string_view text, u32& out) noexcept {
  const char* begin = text.data();
  const char* end = begin + text.size();
  u32 value{};
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0U) {
    return false;
  }
  out = value;
  return true;
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

[[nodiscard]] bool parse_page_number(std::string_view text, u32& out) noexcept {
  const char* begin = text.data();
  const char* end = begin + text.size();
  u32 value{};
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0U) {
    return false;
  }
  out = value;
  return true;
}

[[nodiscard]] std::string format_aliases(const command::Command& definition) {
  if (definition.aliases.empty()) {
    return "none";
  }
  std::string out;
  for (const std::string& alias : definition.aliases) {
    if (!out.empty()) {
      out.append(", ");
    }
    out.push_back('/');
    out.append(alias);
  }
  return out;
}

[[nodiscard]] std::string format_argument_type(command::CommandArgumentType type) {
  switch (type) {
    case command::CommandArgumentType::String:
      return "string";
    case command::CommandArgumentType::Integer:
      return "integer";
    case command::CommandArgumentType::UnsignedInteger:
      return "unsigned integer";
    case command::CommandArgumentType::Number:
      return "number";
    case command::CommandArgumentType::Boolean:
      return "boolean";
    case command::CommandArgumentType::Enum:
      return "enum";
    case command::CommandArgumentType::Identifier:
      return "identifier";
    case command::CommandArgumentType::TargetSelector:
      return "target selector";
    case command::CommandArgumentType::Position:
      return "position";
    case command::CommandArgumentType::Message:
      return "message";
    case command::CommandArgumentType::Metadata:
      return "metadata";
  }
  return "argument";
}

[[nodiscard]] std::string format_argument_spec(
    const command::CommandArgumentSpec& argument) {
  std::string out = argument.required ? "<" : "[";
  out.append(argument.name);
  out.append(": ");
  out.append(format_argument_type(argument.type));
  if (!argument.enum_values.empty()) {
    out.append("=");
    for (usize index = 0; index < argument.enum_values.size(); ++index) {
      if (index > 0U) {
        out.push_back('|');
      }
      out.append(argument.enum_values[index]);
    }
  }
  if (argument.variadic) {
    out.append("...");
  }
  out.append(argument.required ? ">" : "]");
  if (!argument.description.empty()) {
    out.append(" - ");
    out.append(argument.description);
  }
  return out;
}

[[nodiscard]] command::CommandResult services_unavailable(
    const command::CommandCtx& ctx) {
  return command::CommandResult::internal_error(msg(
      ctx,
      "command.core.services_unavailable",
      "Server command services are unavailable."));
}

[[nodiscard]] command::CommandResult permission_denied(
    command::CommandCtx& ctx,
    std::string permission,
    const command::CommandPermissionResult& result) {
  if (result.has_message) {
    return command::CommandResult::permission_denied(result.message);
  }
  return command::CommandResult::permission_denied(format_msg(
      ctx,
      "command.dispatcher.permission_denied",
      {{"permission", std::move(permission)}},
      "Permission denied: {permission}."));
}

[[nodiscard]] command::CommandResult check_permission(
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
  if (result.allowed) {
    return command::CommandResult::success();
  }
  return permission_denied(ctx, checked.permission, result);
}

[[nodiscard]] bool command_visible_to_sender(
    const command::CommandSender& sender,
    command::CommandServices& services,
    const command::Command& definition) {
  if (definition.domain != command::CommandDomain::ServerBound) {
    return false;
  }
  if (definition.console_only && sender.kind != command::CommandSourceKind::ServerConsole) {
    return false;
  }
  if (sender.kind != command::CommandSourceKind::Player ||
      definition.public_to_players ||
      definition.permission.empty()) {
    return true;
  }
  if (!services.permission_check) {
    return false;
  }
  return services.permission_check(sender, definition).allowed;
}

[[nodiscard]] Expected<command::OnlinePlayerCommandTarget, command::CommandResult>
single_player_target(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    std::string_view selector) {
  if (!services.resolve_player_targets) {
    return Expected<command::OnlinePlayerCommandTarget, command::CommandResult>::failure(
        services_unavailable(ctx));
  }
  std::vector<command::OnlinePlayerCommandTarget> targets =
      services.resolve_player_targets(ctx.sender, selector);
  if (targets.empty()) {
    return Expected<command::OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::usage_error(format_msg(
            ctx,
            "command.core.no_player_target",
            {{"target", std::string{selector}}},
            "No online player matches {target}.")));
  }
  if (targets.size() != 1U) {
    return Expected<command::OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::usage_error(format_msg(
            ctx,
            "command.core.player_target_not_single",
            {{"target", std::string{selector}}, {"count", std::to_string(targets.size())}},
            "Target {target} resolved to {count} players; expected exactly one.")));
  }
  return Expected<command::OnlinePlayerCommandTarget, command::CommandResult>::success(
      targets.front());
}

[[nodiscard]] Expected<command::OnlinePlayerCommandTarget, command::CommandResult>
sender_player_target(command::CommandCtx& ctx, const ServerCommandServices& services) {
  if (!ctx.sender.player_id.has_value()) {
    return Expected<command::OnlinePlayerCommandTarget, command::CommandResult>::failure(
        command::CommandResult::invalid_source(msg(
            ctx,
            "command.core.player_required",
            "This command requires an online player sender or an explicit player target.")));
  }
  return single_player_target(ctx, services, "@s");
}

[[nodiscard]] std::vector<command::EntityCommandTarget> entity_targets(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    std::string_view selector) {
  if (!services.resolve_entity_targets) {
    return {};
  }
  return services.resolve_entity_targets(ctx.sender, selector);
}

[[nodiscard]] command::CommandResult execute_help(command::CommandCtx& ctx) {
  const command::CommandRegistry* registry = ctx.services.registry;
  if (registry == nullptr) {
    return command::CommandResult::internal_error(msg(
        ctx,
        "command.help.registry_unavailable",
        "Command registry is unavailable."));
  }
  if (ctx.args.size() > 1U) {
    return command::CommandResult::usage_error("Usage: /help [page|command].");
  }

  std::vector<const command::Command*> commands = registry->commands();
  commands.erase(
      std::remove_if(
          commands.begin(),
          commands.end(),
          [&](const command::Command* definition) {
            return definition == nullptr ||
                   !command_visible_to_sender(ctx.sender, ctx.services, *definition);
          }),
      commands.end());
  std::sort(commands.begin(), commands.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->name < rhs->name;
  });

  if (ctx.args.size() == 1U) {
    u32 page{};
    if (!parse_page_number(ctx.args[0], page)) {
      const std::optional<command::CommandLookup> lookup =
          registry->resolve(ctx.args[0]);
      if (!lookup.has_value() || lookup->command == nullptr ||
          !command_visible_to_sender(ctx.sender, ctx.services, *lookup->command)) {
        return command::CommandResult::usage_error(format_msg(
            ctx,
            "command.help.unknown_command",
            {{"command", std::string{ctx.args[0]}}},
            "No help is available for command: {command}."));
      }

      const command::Command& definition = *lookup->command;
      command::CommandResult result = command::CommandResult::success(
          "Help for " + slash(command_name_for_display(definition)) + ":");
      result.add_message(command::CommandMessageKind::Info, "Description: " + definition.description);
      result.add_message(command::CommandMessageKind::Info, "Usage: " + definition.usage);
      result.add_message(command::CommandMessageKind::Info, "Aliases: " + format_aliases(definition));
      result.add_message(command::CommandMessageKind::Info, "Permission: " + definition.permission);
      if (!definition.arguments.empty()) {
        result.add_message(command::CommandMessageKind::Info, "Arguments:");
        for (const command::CommandArgumentSpec& argument : definition.arguments) {
          result.add_message(
              command::CommandMessageKind::Info,
              "  " + format_argument_spec(argument));
        }
      }
      if (!definition.examples.empty()) {
        result.add_message(command::CommandMessageKind::Info, "Examples:");
        for (const std::string& example : definition.examples) {
          result.add_message(command::CommandMessageKind::Info, "  " + example);
        }
      }
      if (!definition.technical_notes.empty()) {
        result.add_message(
            command::CommandMessageKind::Info,
            "Notes: " + definition.technical_notes);
      }
      return result;
    }

    if (page == 0U) {
      page = 1U;
    }
    const usize page_count =
        std::max<usize>(1U, (commands.size() + HelpPageSize - 1U) / HelpPageSize);
    if (page > page_count) {
      return command::CommandResult::usage_error(format_msg(
          ctx,
          "command.help.page_out_of_range",
          {{"page", std::to_string(page)}, {"pages", std::to_string(page_count)}},
          "Help page {page} is out of range. There are {pages} pages."));
    }

    const usize start = static_cast<usize>(page - 1U) * HelpPageSize;
    const usize end = std::min(commands.size(), start + HelpPageSize);
    command::CommandResult result = command::CommandResult::success(format_msg(
        ctx,
        "command.help.page_header",
        {{"page", std::to_string(page)}, {"pages", std::to_string(page_count)}},
        "Commands page {page}/{pages}:"));
    for (usize index = start; index < end; ++index) {
      const command::Command& definition = *commands[index];
      result.add_message(
          command::CommandMessageKind::Info,
          slash(command_name_for_display(definition)) + " (aliases: " +
              format_aliases(definition) + ") - " + definition.description);
    }
    return result;
  }

  command::CommandResult result = command::CommandResult::success();
  const usize page_count =
      std::max<usize>(1U, (commands.size() + HelpPageSize - 1U) / HelpPageSize);
  result.add_message(
      command::CommandMessageKind::Info,
      "Commands page 1/" + std::to_string(page_count) + ":");
  const usize end = std::min(commands.size(), HelpPageSize);
  for (usize index = 0; index < end; ++index) {
    const command::Command& definition = *commands[index];
    result.add_message(
        command::CommandMessageKind::Info,
        slash(command_name_for_display(definition)) + " (aliases: " +
            format_aliases(definition) + ") - " + definition.description);
  }
  return result;
}

[[nodiscard]] command::CommandResult execute_time(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->query_world_time ||
      !services->set_world_time ||
      !services->add_world_time) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /time <query|set|add>.");
  }

  const WorldID world_id = sender_world_id(ctx.sender);
  const std::string_view action = ctx.args[0];
  if (action == "query") {
    command::CommandResult access = check_permission(ctx, "arvane.command.time.query");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 1U) {
      return command::CommandResult::usage_error("Usage: /time query.");
    }
    const std::optional<command::WorldTimeCommandState> state =
        services->query_world_time(world_id);
    if (!state.has_value()) {
      return command::CommandResult::failure("World time is unavailable.");
    }
    return command::CommandResult::success(
        "World time is " + std::to_string(state->day_time_tick) +
        " ticks (world tick " + std::to_string(state->world_tick) + ").");
  }

  if (action == "set") {
    command::CommandResult access = check_permission(ctx, "arvane.command.time.set");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 2U) {
      return command::CommandResult::usage_error(
          "Usage: /time set <value|day|noon|night|midnight>.");
    }
    u32 tick{};
    const std::string lowered = lower_ascii_copy(ctx.args[1]);
    const std::optional<command::WorldTimeCommandState> state =
        services->query_world_time(world_id);
    const u32 day_length_ticks =
        state.has_value() ? state->day_length_ticks : world::day_length_ticks_from_seconds(
            world::DefaultDayLengthSeconds,
            DefaultBaseTps);
    if (lowered == "day") {
      tick = world::day_tick_from_hour_minute(8U, 0U, day_length_ticks);
    } else if (lowered == "noon") {
      tick = world::day_tick_from_hour_minute(12U, 0U, day_length_ticks);
    } else if (lowered == "night") {
      tick = world::day_tick_from_hour_minute(20U, 0U, day_length_ticks);
    } else if (lowered == "midnight") {
      tick = world::day_tick_from_hour_minute(0U, 0U, day_length_ticks);
    } else if (!parse_u32(ctx.args[1], tick)) {
      return command::CommandResult::usage_error(
          "Expected non-negative world time tick or named preset.");
    }
    if (!services->set_world_time(world_id, tick)) {
      return command::CommandResult::failure("Unable to set world time.");
    }
    return command::CommandResult::success(
        "Set world time to " + std::to_string(tick) + " ticks.");
  }

  if (action == "add") {
    command::CommandResult access = check_permission(ctx, "arvane.command.time.add");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 2U) {
      return command::CommandResult::usage_error("Usage: /time add <ticks>.");
    }
    u32 ticks{};
    if (!parse_positive_u32(ctx.args[1], ticks)) {
      return command::CommandResult::usage_error("Expected positive tick amount.");
    }
    if (!services->add_world_time(world_id, ticks)) {
      return command::CommandResult::failure("Unable to advance world time.");
    }
    return command::CommandResult::success(
        "Advanced world time by " + std::to_string(ticks) + " ticks.");
  }

  return command::CommandResult::usage_error("Usage: /time <query|set|add>.");
}

[[nodiscard]] Expected<Vec3f, command::CommandResult> summon_position(
    command::CommandCtx& ctx,
    const ServerCommandServices& services,
    usize first_coord_index) {
  command::CommandArgs coord_args;
  for (usize index = first_coord_index; index < first_coord_index + 3U; ++index) {
    coord_args.tokens.push_back(ctx.args[index]);
  }
  command::CommandArgReader reader{coord_args};
  auto parsed = reader.read_position();
  if (!parsed) {
    return Expected<Vec3f, command::CommandResult>::failure(std::move(parsed.error()));
  }
  return command::resolve_position(parsed.value(), sender_position(ctx.sender, services));
}

[[nodiscard]] command::CommandResult execute_summon(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->entity_type_by_name || !services->spawn_entity) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() != 1U && ctx.args.size() != 4U && ctx.args.size() != 5U) {
    return command::CommandResult::usage_error(
        "Usage: /summon <entity_id> [x y z] [count].");
  }

  const std::string_view entity_id = ctx.args[0];
  if (!has_namespaced_id(entity_id)) {
    return command::CommandResult::usage_error(
        "Entity ID must be namespaced, for example arvane:cow.");
  }
  const std::optional<command::EntityTypeCommandRecord> record =
      services->entity_type_by_name(entity_id);
  if (!record.has_value()) {
    return command::CommandResult::usage_error(
        "Unknown entity type: " + std::string{entity_id} + ".");
  }
  if (!record->spawnable) {
    return command::CommandResult::usage_error(
        "Entity type is not spawnable: " + std::string{entity_id} + ".");
  }

  Vec3f position{};
  if (ctx.args.size() == 1U) {
    const std::optional<Vec3f> sender_pos = sender_position(ctx.sender, *services);
    if (!sender_pos.has_value()) {
      return command::CommandResult::invalid_source(
          "Summon without coordinates requires an executor position.");
    }
    position = sender_pos.value();
  } else {
    auto parsed_position = summon_position(ctx, *services, 1U);
    if (!parsed_position) {
      return parsed_position.error();
    }
    position = parsed_position.value();
  }
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z)) {
    return command::CommandResult::usage_error("Spawn position must be finite.");
  }

  u32 count = 1U;
  if (ctx.args.size() == 5U && !parse_positive_u32(ctx.args[4], count)) {
    return command::CommandResult::usage_error("Count must be a positive integer.");
  }
  count = std::min(count, MaxSummonCount);

  u32 spawned = 0U;
  for (u32 index = 0U; index < count; ++index) {
    if (services->spawn_entity(sender_world_id(ctx.sender), entity_id, position).has_value()) {
      ++spawned;
    }
  }
  if (spawned == 0U) {
    return command::CommandResult::failure("Unable to spawn entity.");
  }
  return command::CommandResult::success(
      "Spawned " + std::to_string(spawned) + " " + std::string{entity_id} +
      " at " + format_position(position) + ".");
}

[[nodiscard]] command::CommandResult execute_kill(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->resolve_entity_targets || !services->kill_entity) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 1U) {
    return command::CommandResult::usage_error("Usage: /kill [target].");
  }
  const std::string_view target = ctx.args.empty() ? std::string_view{"@s"} : ctx.args[0];
  std::vector<command::EntityCommandTarget> targets = entity_targets(ctx, *services, target);
  if (targets.empty()) {
    return command::CommandResult::usage_error(
        "No entities matched target: " + std::string{target} + ".");
  }
  u32 affected = 0U;
  for (const command::EntityCommandTarget& entity : targets) {
    if (services->kill_entity(entity.entity_id)) {
      ++affected;
    }
  }
  if (affected == 0U) {
    return command::CommandResult::failure("No entities were affected.");
  }
  return command::CommandResult::success(
      "Affected " + std::to_string(affected) + " entities.");
}

[[nodiscard]] command::CommandResult execute_give(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->resolve_player_targets ||
      !services->item_by_name ||
      !services->give_player_item) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() < 2U) {
    return command::CommandResult::usage_error(
        "Usage: /give <player> <item_id> [count] [metadata].");
  }

  auto target = single_player_target(ctx, *services, ctx.args[0]);
  if (!target) {
    return target.error();
  }
  const std::string_view item_id = ctx.args[1];
  if (!has_namespaced_id(item_id)) {
    return command::CommandResult::usage_error(
        "Item ID must be namespaced, for example arvane:stone.");
  }
  const std::optional<command::ItemCommandRecord> item = services->item_by_name(item_id);
  if (!item.has_value()) {
    return command::CommandResult::usage_error(
        "Unknown item: " + std::string{item_id} + ".");
  }

  u32 count = 1U;
  if (ctx.args.size() >= 3U && !parse_positive_u32(ctx.args[2], count)) {
    return command::CommandResult::usage_error("Count must be a positive integer.");
  }
  count = std::min(count, MaxGiveCount);
  std::optional<std::string> metadata;
  if (ctx.args.size() > 3U) {
    metadata = join_tokens(ctx.args, 3U);
  }
  const command::InventoryInsertCommandResult inserted =
      services->give_player_item(
          target.value().player_id,
          item->item_id,
          static_cast<u16>(std::min<u32>(count, std::numeric_limits<u16>::max())),
          std::move(metadata));
  if (!inserted.success) {
    return command::CommandResult::failure(inserted.message.empty()
                                               ? "Unable to give item."
                                               : inserted.message);
  }
  if (inserted.inserted == 0U) {
    return command::CommandResult::failure("Inventory is full; no items were inserted.");
  }
  std::string text = "Gave " + std::to_string(inserted.inserted) + " " +
                     std::string{item_id} + " to " + target.value().display_name + ".";
  if (inserted.leftover > 0U) {
    text.append(" Leftover: ");
    text.append(std::to_string(inserted.leftover));
    text.push_back('.');
  }
  return command::CommandResult::success(std::move(text));
}

[[nodiscard]] command::CommandResult execute_clear(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->resolve_player_targets ||
      !services->item_by_name ||
      !services->clear_player_inventory) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /clear [player] [item_id].");
  }

  Expected<command::OnlinePlayerCommandTarget, command::CommandResult> target =
      ctx.args.empty() ? sender_player_target(ctx, *services)
                       : single_player_target(ctx, *services, ctx.args[0]);
  if (!target) {
    return target.error();
  }

  std::optional<u32> item_id;
  if (ctx.args.size() == 2U) {
    const std::string_view item_name = ctx.args[1];
    if (!has_namespaced_id(item_name)) {
      return command::CommandResult::usage_error(
          "Item ID must be namespaced, for example arvane:dirt.");
    }
    const std::optional<command::ItemCommandRecord> item = services->item_by_name(item_name);
    if (!item.has_value()) {
      return command::CommandResult::usage_error(
          "Unknown item: " + std::string{item_name} + ".");
    }
    item_id = item->item_id;
  }

  const command::InventoryClearCommandResult cleared =
      services->clear_player_inventory(target.value().player_id, item_id);
  if (!cleared.success) {
    return command::CommandResult::failure(cleared.message.empty()
                                               ? "Unable to clear inventory."
                                               : cleared.message);
  }
  return command::CommandResult::success(
      "Removed " + std::to_string(cleared.removed_items) + " items across " +
      std::to_string(cleared.removed_stacks) + " stacks from " +
      target.value().display_name + ".");
}

[[nodiscard]] command::CommandResult execute_effect(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->resolve_entity_targets ||
      !services->apply_status_effect ||
      !services->clear_status_effects ||
      !services->list_status_effects) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error(
        "Usage: /effect <give|clear|list> ...");
  }

  const std::string_view action = ctx.args[0];
  if (action == "give") {
    command::CommandResult access = check_permission(ctx, "arvane.command.effect.give");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 4U && ctx.args.size() != 5U) {
      return command::CommandResult::usage_error(
          "Usage: /effect give <target> <effect_id> <duration_seconds> [amplifier].");
    }
    const std::string_view effect_id = ctx.args[2];
    if (!has_namespaced_id(effect_id)) {
      return command::CommandResult::usage_error(
          "Effect ID must be namespaced, for example arvane:speed.");
    }
    u32 duration_seconds{};
    if (!parse_positive_u32(ctx.args[3], duration_seconds)) {
      return command::CommandResult::usage_error(
          "Duration must be a positive whole number of seconds.");
    }
    u32 amplifier = 1U;
    if (ctx.args.size() == 5U && !parse_positive_u32(ctx.args[4], amplifier)) {
      return command::CommandResult::usage_error("Amplifier must be a positive integer.");
    }
    const command::DebugCommandStats stats =
        services->debug_stats ? services->debug_stats(sender_world_id(ctx.sender))
                              : command::DebugCommandStats{0U, DefaultBaseTps};
    const TickRate tps = stats.tick_rate == 0U ? DefaultBaseTps : stats.tick_rate;
    const u64 raw_ticks = static_cast<u64>(duration_seconds) * static_cast<u64>(tps);
    const u32 duration_ticks = raw_ticks > std::numeric_limits<u32>::max()
                                   ? std::numeric_limits<u32>::max()
                                   : static_cast<u32>(raw_ticks);
    std::vector<command::EntityCommandTarget> targets =
        entity_targets(ctx, *services, ctx.args[1]);
    if (targets.empty()) {
      return command::CommandResult::usage_error(
          "No entities matched target: " + std::string{ctx.args[1]} + ".");
    }
    u32 applied = 0U;
    for (const command::EntityCommandTarget& target : targets) {
      if (services->apply_status_effect(
              target.entity_id,
              effect_id,
              duration_ticks,
              amplifier)) {
        ++applied;
      }
    }
    return command::CommandResult::success(
        "Applied " + std::string{effect_id} + " to " +
        std::to_string(applied) + " entities.");
  }

  if (action == "clear") {
    command::CommandResult access = check_permission(ctx, "arvane.command.effect.clear");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 2U && ctx.args.size() != 3U) {
      return command::CommandResult::usage_error(
          "Usage: /effect clear <target> [effect_id].");
    }
    std::optional<std::string_view> effect_id;
    if (ctx.args.size() == 3U) {
      if (!has_namespaced_id(ctx.args[2])) {
        return command::CommandResult::usage_error(
            "Effect ID must be namespaced, for example arvane:speed.");
      }
      effect_id = ctx.args[2];
    }
    std::vector<command::EntityCommandTarget> targets =
        entity_targets(ctx, *services, ctx.args[1]);
    if (targets.empty()) {
      return command::CommandResult::usage_error(
          "No entities matched target: " + std::string{ctx.args[1]} + ".");
    }
    u32 cleared = 0U;
    for (const command::EntityCommandTarget& target : targets) {
      cleared += services->clear_status_effects(target.entity_id, effect_id);
    }
    return command::CommandResult::success(
        "Cleared " + std::to_string(cleared) + " effects.");
  }

  if (action == "list") {
    command::CommandResult access = check_permission(ctx, "arvane.command.effect.list");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 2U) {
      return command::CommandResult::usage_error("Usage: /effect list <target>.");
    }
    std::vector<command::EntityCommandTarget> targets =
        entity_targets(ctx, *services, ctx.args[1]);
    if (targets.empty()) {
      return command::CommandResult::usage_error(
          "No entities matched target: " + std::string{ctx.args[1]} + ".");
    }
    command::CommandResult result = command::CommandResult::success(
        "Active effects for " + std::to_string(targets.size()) + " entities:");
    for (const command::EntityCommandTarget& target : targets) {
      const std::vector<command::StatusEffectCommandState> effects =
          services->list_status_effects(target.entity_id);
      if (effects.empty()) {
        result.add_message(
            command::CommandMessageKind::Info,
            target.display_name + ": none");
        continue;
      }
      for (const command::StatusEffectCommandState& effect : effects) {
        result.add_message(
            command::CommandMessageKind::Info,
            target.display_name + ": " + effect.effect_id + " " +
                std::to_string(effect.remaining_ticks) + " ticks amp " +
                std::to_string(effect.amplifier));
      }
    }
    return result;
  }

  return command::CommandResult::usage_error("Usage: /effect <give|clear|list> ...");
}

[[nodiscard]] command::CommandResult execute_weather(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->query_world_weather ||
      !services->set_world_weather) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error(
        "Usage: /weather <query|clear|rain|storm|set>.");
  }
  const std::string_view action = ctx.args[0];
  const WorldID world_id = sender_world_id(ctx.sender);
  if (action == "query") {
    command::CommandResult access = check_permission(ctx, "arvane.command.weather.query");
    if (!access) {
      return access;
    }
    if (ctx.args.size() != 1U) {
      return command::CommandResult::usage_error("Usage: /weather query.");
    }
    const std::optional<std::string> weather = services->query_world_weather(world_id);
    if (!weather.has_value()) {
      return command::CommandResult::failure("World weather is unavailable.");
    }
    return command::CommandResult::success("Current weather is " + weather.value() + ".");
  }

  command::CommandResult access = check_permission(ctx, "arvane.command.weather.set");
  if (!access) {
    return access;
  }
  std::string weather;
  if (action == "clear" || action == "rain" || action == "storm") {
    if (ctx.args.size() != 1U) {
      return command::CommandResult::usage_error("Usage: /weather <clear|rain|storm>.");
    }
    weather = std::string{action};
  } else if (action == "set") {
    if (ctx.args.size() != 2U) {
      return command::CommandResult::usage_error("Usage: /weather set <weather_id>.");
    }
    weather = std::string{ctx.args[1]};
    if (weather != "clear" && weather != "rain" && weather != "storm" &&
        !has_namespaced_id(weather)) {
      return command::CommandResult::usage_error(
          "Weather ID must be clear, rain, storm, or namespaced.");
    }
  } else {
    return command::CommandResult::usage_error(
        "Usage: /weather <query|clear|rain|storm|set>.");
  }

  if (!services->set_world_weather(world_id, weather)) {
    return command::CommandResult::failure("Unable to set weather.");
  }
  return command::CommandResult::success("Set weather to " + weather + ".");
}

[[nodiscard]] command::CommandResult execute_difficulty(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->query_world_difficulty ||
      !services->set_world_difficulty) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 1U) {
    return command::CommandResult::usage_error(
        "Usage: /difficulty [peaceful|easy|normal|hard].");
  }
  const WorldID world_id = sender_world_id(ctx.sender);
  const std::optional<std::string> previous =
      services->query_world_difficulty(world_id);
  if (!previous.has_value()) {
    return command::CommandResult::failure("World difficulty is unavailable.");
  }
  if (ctx.args.empty()) {
    return command::CommandResult::success(
        "Current difficulty is " + previous.value() + ".");
  }
  const std::string value = lower_ascii_copy(ctx.args[0]);
  if (value != "peaceful" && value != "easy" && value != "normal" && value != "hard") {
    return command::CommandResult::usage_error(
        "Difficulty must be peaceful, easy, normal, or hard.");
  }
  if (!services->set_world_difficulty(world_id, value)) {
    return command::CommandResult::failure("Unable to set difficulty.");
  }
  return command::CommandResult::success(
      "Changed difficulty from " + previous.value() + " to " + value + ".");
}

[[nodiscard]] command::CommandResult execute_gamerule(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->list_game_rules ||
      !services->get_game_rule ||
      !services->set_game_rule) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 2U) {
    return command::CommandResult::usage_error("Usage: /gamerule [rule] [value].");
  }
  const WorldID world_id = sender_world_id(ctx.sender);
  if (ctx.args.empty()) {
    const std::vector<command::GameRuleCommandEntry> rules =
        services->list_game_rules(world_id);
    command::CommandResult result = command::CommandResult::success(
        "Game rules (" + std::to_string(rules.size()) + "):");
    for (const command::GameRuleCommandEntry& rule : rules) {
      result.add_message(
          command::CommandMessageKind::Info,
          rule.name + " = " + rule.value + " (" + rule.type + ")");
    }
    return result;
  }

  if (ctx.args.size() == 1U) {
    const std::optional<command::GameRuleCommandEntry> rule =
        services->get_game_rule(world_id, ctx.args[0]);
    if (!rule.has_value()) {
      return command::CommandResult::usage_error(
          "Unknown game rule: " + std::string{ctx.args[0]} + ".");
    }
    return command::CommandResult::success(
        rule->name + " = " + rule->value + " (" + rule->type + ").");
  }

  const command::GameRuleSetCommandResult updated =
      services->set_game_rule(world_id, ctx.args[0], ctx.args[1]);
  if (!updated.success) {
    return command::CommandResult::usage_error(
        updated.message.empty() ? "Unable to update game rule." : updated.message);
  }
  return command::CommandResult::success(
      "Changed " + std::string{ctx.args[0]} + " from " + updated.old_value +
      " to " + updated.new_value + ".");
}

[[nodiscard]] command::CommandResult execute_save_all(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->save_all) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 1U || (!ctx.args.empty() && ctx.args[0] != "flush")) {
    return command::CommandResult::usage_error("Usage: /save-all [flush].");
  }
  const bool flush = !ctx.args.empty();
  const command::SaveAllCommandResult saved = services->save_all(flush);
  if (!saved.success) {
    return command::CommandResult::failure(
        saved.message.empty() ? "Save failed." : saved.message);
  }
  std::string text = flush ? "Saved and flushed " : "Saved ";
  text.append(std::to_string(saved.saved_chunks));
  text.append(" chunks, ");
  text.append(std::to_string(saved.saved_players));
  text.append(" players, ");
  text.append(std::to_string(saved.saved_entities));
  text.append(" entities.");
  return command::CommandResult::success(std::move(text));
}

[[nodiscard]] command::CommandResult execute_say(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->broadcast_system_message) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /say <message>.");
  }
  const std::string message = join_tokens(ctx.args, 0U);
  services->broadcast_system_message("[Server] " + message);
  return command::CommandResult::success("Broadcasted: " + message);
}

[[nodiscard]] command::CommandResult execute_me(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->broadcast_system_message) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /me <action>.");
  }
  const std::string actor = ctx.sender.display_name.empty() ? "server" : ctx.sender.display_name;
  const std::string action = join_tokens(ctx.args, 0U);
  services->broadcast_system_message("* " + actor + " " + action);
  return command::CommandResult::success("Broadcasted action: " + action);
}

[[nodiscard]] command::CommandResult execute_list(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->online_players) {
    return services_unavailable(ctx);
  }
  if (!ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /list.");
  }
  std::vector<command::OnlinePlayerCommandTarget> players = services->online_players();
  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.display_name < rhs.display_name;
  });
  command::CommandResult result = command::CommandResult::success(
      "Online players: " + std::to_string(players.size()) + ".");
  if (players.empty()) {
    result.add_message(command::CommandMessageKind::Info, "No players online.");
    return result;
  }
  std::string names;
  for (const command::OnlinePlayerCommandTarget& player : players) {
    if (!names.empty()) {
      names.append(", ");
    }
    names.append(player.display_name);
  }
  result.add_message(command::CommandMessageKind::Info, names);
  return result;
}

[[nodiscard]] command::CommandResult execute_kick(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->resolve_player_targets || !services->kick_player) {
    return services_unavailable(ctx);
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /kick <player> [reason].");
  }
  auto target = single_player_target(ctx, *services, ctx.args[0]);
  if (!target) {
    return target.error();
  }
  const std::string reason =
      ctx.args.size() > 1U ? join_tokens(ctx.args, 1U) : std::string{"Kicked by server."};
  if (!services->kick_player(target.value().player_id, reason)) {
    return command::CommandResult::failure("Unable to kick player.");
  }
  return command::CommandResult::success(
      "Kicked " + target.value().display_name + ": " + reason);
}

[[nodiscard]] command::CommandResult execute_debug(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->debug_stats) {
    return services_unavailable(ctx);
  }
  if (ctx.args.size() > 1U) {
    return command::CommandResult::usage_error(
        "Usage: /debug [entity-count|chunk-count|position|tick].");
  }
  if (ctx.args.empty()) {
    command::CommandResult result = command::CommandResult::success(
        "Debug subcommands: entity-count, chunk-count, position, tick.");
    return result;
  }
  const std::string_view action = ctx.args[0];
  const command::DebugCommandStats stats = services->debug_stats(sender_world_id(ctx.sender));
  if (action == "entity-count") {
    return command::CommandResult::success(
        "Loaded entities: " + std::to_string(stats.entity_count) + ".");
  }
  if (action == "chunk-count") {
    return command::CommandResult::success(
        "Chunks loaded=" + std::to_string(stats.loaded_chunk_count) +
        ", active=" + std::to_string(stats.active_chunk_count) + ".");
  }
  if (action == "position") {
    const std::optional<Vec3f> pos = sender_position(ctx.sender, *services);
    if (!pos.has_value()) {
      return command::CommandResult::invalid_source(
          "Debug position requires an executor position.");
    }
    const BlockPos block = floor_block_pos(pos.value());
    const ChunkSectionCoord chunk = block_to_chunk(block);
    return command::CommandResult::success(
        "Position " + format_position(pos.value()) + "; block " +
        format_block_pos(block) + "; chunk " + format_chunk_pos(chunk) + ".");
  }
  if (action == "tick") {
    return command::CommandResult::success(
        "Tick " + std::to_string(stats.tick_index) + " at " +
        std::to_string(stats.tick_rate) + " TPS; jobs gen=" +
        std::to_string(stats.pending_generation_jobs) + ", light=" +
        std::to_string(stats.pending_lighting_jobs) + ", fluid=" +
        std::to_string(stats.pending_fluid_jobs) + ".");
  }
  return command::CommandResult::usage_error(
      "Usage: /debug [entity-count|chunk-count|position|tick].");
}

using ExecuteFn = command::CommandResult (*)(command::CommandCtx&);

command::Command make_command(
    std::string name,
    std::string description,
    std::string permission,
    std::string usage,
    std::vector<command::CommandArgumentSpec> arguments,
    std::string technical_notes,
    std::vector<std::string> examples,
    ExecuteFn execute) {
  command::Command definition;
  definition.name = std::move(name);
  definition.domain = command::CommandDomain::ServerBound;
  definition.description = std::move(description);
  definition.permission = std::move(permission);
  definition.usage = std::move(usage);
  definition.arguments = std::move(arguments);
  definition.technical_notes = std::move(technical_notes);
  definition.examples = std::move(examples);
  definition.execute = execute;
  return definition;
}

void register_command(
    command::CommandRegistry& registry,
    command::Command definition,
    std::initializer_list<std::string_view> aliases) {
  const std::string name = definition.name;
  const auto command_result = registry.register_command(std::move(definition));
  assert(command_result && "core command registration failed");
  (void)command_result;
  for (std::string_view alias : aliases) {
    const auto alias_result = registry.register_alias(std::string{alias}, name, true);
    assert(alias_result && "core command alias registration failed");
    (void)alias_result;
  }
}

command::CommandArgumentSpec arg(
    std::string name,
    command::CommandArgumentType type,
    bool required,
    std::string description,
    std::vector<std::string> enum_values = {},
    bool variadic = false) {
  return {
      std::move(name),
      type,
      required,
      variadic,
      std::move(enum_values),
      std::move(description),
  };
}

} // namespace

void register_core_commands(command::CommandRegistry& registry) {
  register_command(
      registry,
      make_command(
          "help",
          "Displays command help.",
          "arvane.command.help",
          "/help [page|command]",
          {arg("page_or_command", command::CommandArgumentType::String, false, "Page number or command name.")},
          "Queries the live CommandRegistry so built-in, pack, and plugin commands are discoverable.",
          {"/help", "/help 2", "/help summon"},
          execute_help),
      {"?", "commands"});

  register_command(
      registry,
      make_command(
          "time",
          "Reads or manipulates world time.",
          "arvane.command.time",
          "/time query\n/time set <value|day|noon|night|midnight>\n/time add <ticks>",
          {arg("action", command::CommandArgumentType::Enum, true, "Time operation.", {"query", "set", "add"}),
           arg("value", command::CommandArgumentType::String, false, "Tick value, preset, or add amount.")},
          "Uses the authoritative WorldRuntime clock for the executor's world.",
          {"/time query", "/time set day", "/time set 6000", "/time add 1000"},
          execute_time),
      {"t"});

  register_command(
      registry,
      make_command(
          "summon",
          "Spawns an entity at a location.",
          "arvane.command.summon",
          "/summon <entity_id> [x y z] [count]",
          {arg("entity_id", command::CommandArgumentType::Identifier, true, "Namespaced entity type."),
           arg("position", command::CommandArgumentType::Position, false, "Absolute or relative x y z position."),
           arg("count", command::CommandArgumentType::UnsignedInteger, false, "Number to spawn, clamped to a safe maximum.")},
          "Resolves entity types through the server entity registry and inserts through WorldRuntime.",
          {"/summon arvane:cow", "/summon arvane:zombie 100 72 -45", "/summon arvane:cow 100 72 -45 5"},
          execute_summon),
      {"spawnentity", "entityspawn"});

  register_command(
      registry,
      make_command(
          "kill",
          "Kills or removes entities.",
          "arvane.command.kill",
          "/kill [target]",
          {arg("target", command::CommandArgumentType::TargetSelector, false, "@s, @p, @a, @e, or player name.")},
          "Routes through damage/death when health exists and otherwise schedules clean despawn.",
          {"/kill", "/kill @s", "/kill @e", "/kill PlayerName"},
          execute_kill),
      {"remove"});

  register_command(
      registry,
      make_command(
          "give",
          "Gives an item to a player.",
          "arvane.command.give",
          "/give <player> <item_id> [count] [metadata]",
          {arg("player", command::CommandArgumentType::TargetSelector, true, "Single player selector or exact player name."),
           arg("item_id", command::CommandArgumentType::Identifier, true, "Namespaced item id."),
           arg("count", command::CommandArgumentType::UnsignedInteger, false, "Item count."),
           arg("metadata", command::CommandArgumentType::Metadata, false, "Optional stack data.", {}, true)},
          "Uses the canonical inventory insertion path and stack metadata parser.",
          {"/give @s arvane:stone", "/give PlayerName arvane:torch 64", "/give @p arvane:buggy_item 1"},
          execute_give),
      {"i", "item"});

  register_command(
      registry,
      make_command(
          "clear",
          "Clears items from a player inventory.",
          "arvane.command.clear",
          "/clear [player] [item_id]",
          {arg("player", command::CommandArgumentType::TargetSelector, false, "Single player selector or exact player name."),
           arg("item_id", command::CommandArgumentType::Identifier, false, "Namespaced item id to remove.")},
          "Mutates the authoritative player inventory and lets normal inventory sync publish changes.",
          {"/clear", "/clear @s", "/clear PlayerName", "/clear @s arvane:dirt"},
          execute_clear),
      {"clearinv", "ci"});

  register_command(
      registry,
      make_command(
          "effect",
          "Applies, clears, or lists status effects.",
          "arvane.command.effect",
          "/effect give <target> <effect_id> <duration_seconds> [amplifier]\n/effect clear <target> [effect_id]\n/effect list <target>",
          {arg("action", command::CommandArgumentType::Enum, true, "Effect operation.", {"give", "clear", "list"}),
           arg("target", command::CommandArgumentType::TargetSelector, true, "Entity selector or player name."),
           arg("effect_id", command::CommandArgumentType::Identifier, false, "Namespaced effect id."),
           arg("duration_seconds", command::CommandArgumentType::UnsignedInteger, false, "Duration in seconds."),
           arg("amplifier", command::CommandArgumentType::UnsignedInteger, false, "Effect amplifier, default 1.")},
          "Stores authoritative V1 status effect state in WorldRuntime for future consumers.",
          {"/effect give @s arvane:speed 60", "/effect clear @s arvane:speed", "/effect list @s"},
          execute_effect),
      {"status"});

  register_command(
      registry,
      make_command(
          "weather",
          "Reads or changes world weather state.",
          "arvane.command.weather",
          "/weather query\n/weather <clear|rain|storm>\n/weather set <weather_id>",
          {arg("action", command::CommandArgumentType::Enum, true, "Weather operation.", {"query", "clear", "rain", "storm", "set"}),
           arg("weather_id", command::CommandArgumentType::Identifier, false, "Namespaced weather id.")},
          "Stores authoritative V1 weather state on WorldRuntime.",
          {"/weather query", "/weather clear", "/weather rain", "/weather set arvane:snow"},
          execute_weather),
      {"w"});

  register_command(
      registry,
      make_command(
          "difficulty",
          "Reads or changes world difficulty.",
          "arvane.command.difficulty",
          "/difficulty [peaceful|easy|normal|hard]",
          {arg("difficulty", command::CommandArgumentType::Enum, false, "Difficulty value.", {"peaceful", "easy", "normal", "hard"})},
          "Stores authoritative V1 difficulty state on WorldRuntime for systems to consume.",
          {"/difficulty", "/difficulty normal", "/difficulty hard"},
          execute_difficulty),
      {"diff"});

  register_command(
      registry,
      make_command(
          "gamerule",
          "Reads or changes server/world game rules.",
          "arvane.command.gamerule",
          "/gamerule [rule] [value]",
          {arg("rule", command::CommandArgumentType::String, false, "Game rule name."),
           arg("value", command::CommandArgumentType::String, false, "Typed rule value.")},
          "Uses a typed WorldRuntime game-rule table; V1 built-ins are boolean rules.",
          {"/gamerule", "/gamerule doDaylightCycle", "/gamerule doDaylightCycle false", "/gamerule doMobSpawning true"},
          execute_gamerule),
      {"rule", "rules"});

  register_command(
      registry,
      make_command(
          "save-all",
          "Forces world/player data to save.",
          "arvane.command.save",
          "/save-all [flush]",
          {arg("flush", command::CommandArgumentType::Enum, false, "Block until save completion when supported.", {"flush"})},
          "Uses the existing persistence save path for chunks, players, and persistent entities.",
          {"/save-all", "/save-all flush"},
          execute_save_all),
      {"save"});

  register_command(
      registry,
      make_command(
          "say",
          "Broadcasts a server message to all players.",
          "arvane.command.say",
          "/say <message>",
          {arg("message", command::CommandArgumentType::Message, true, "Message body.", {}, true)},
          "Routes through the server system-message broadcast service.",
          {"/say Server restarting soon", "/broadcast Welcome to the test world"},
          execute_say),
      {"broadcast"});

  register_command(
      registry,
      make_command(
          "me",
          "Broadcasts an action message from the executor.",
          "arvane.command.me",
          "/me <action>",
          {arg("action", command::CommandArgumentType::Message, true, "Action body.", {}, true)},
          "Formats an action message and sends it through the system-message broadcast service.",
          {"/me waves", "/me is testing vehicles"},
          execute_me),
      {"action"});

  register_command(
      registry,
      make_command(
          "list",
          "Lists online players.",
          "arvane.command.list",
          "/list",
          {},
          "Queries the server session/player manager rather than scanning entities.",
          {"/list", "/players"},
          execute_list),
      {"players", "who"});

  register_command(
      registry,
      make_command(
          "kick",
          "Disconnects a player from the server with an optional reason.",
          "arvane.command.kick",
          "/kick <player> [reason]",
          {arg("player", command::CommandArgumentType::String, true, "Exact online player name."),
           arg("reason", command::CommandArgumentType::Message, false, "Disconnect reason.", {}, true)},
          "Uses the canonical session disconnect path.",
          {"/kick PlayerName", "/kick PlayerName Restarting server"},
          execute_kick),
      {"disconnect"});

  register_command(
      registry,
      make_command(
          "debug",
          "Provides controlled debug information.",
          "arvane.command.debug",
          "/debug [entity-count|chunk-count|position|tick]",
          {arg("subcommand", command::CommandArgumentType::Enum, false, "Debug value.", {"entity-count", "chunk-count", "position", "tick"})},
          "Reads server/world runtime metrics only; no renderer-only state is required.",
          {"/debug entity-count", "/debug chunk-count", "/debug position", "/debug tick"},
          execute_debug),
      {"dbg"});
}

} // namespace arv::base::commands
