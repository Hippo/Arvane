#include "selection_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/player/selection.hpp"

#include <cassert>
#include <cmath>
#include <sstream>
#include <string>

namespace arv::base::commands {
namespace {

[[nodiscard]] BlockPos floor_block_pos(Vec3f position) noexcept {
  return {{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }};
}

[[nodiscard]] player::PlayerSelection to_player_selection(
    const command::PlayerBlockSelection& selection) {
  return {selection.world, selection.a, selection.b};
}

[[nodiscard]] command::PlayerBlockSelection to_command_selection(
    const player::PlayerSelection& selection) {
  return {selection.world, selection.a, selection.b};
}

[[nodiscard]] command::CommandResult require_player_source(
    const command::CommandCtx& ctx,
    PlayerID& out_player) {
  if (!ctx.sender.player_id.has_value()) {
    return command::CommandResult::invalid_source(
        "Selection commands require a player source.");
  }
  out_player = ctx.sender.player_id.value();
  return command::CommandResult::success();
}

[[nodiscard]] command::CommandResult execute_selection(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->player_selection_by_id ||
      !services->set_player_selection) {
    return command::CommandResult::internal_error(
        "Selection command services are unavailable.");
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error("Usage: /select <a|b|clear|info>.");
  }

  PlayerID player_id;
  command::CommandResult player_result = require_player_source(ctx, player_id);
  if (!player_result) {
    return player_result;
  }

  const std::string_view action = ctx.args[0];
  const std::optional<command::PlayerBlockSelection> current =
      services->player_selection_by_id(player_id);
  if (!current.has_value()) {
    return command::CommandResult::invalid_source("Command sender is not an online player.");
  }
  player::PlayerSelection selection = to_player_selection(current.value());

  if (action == "clear") {
    if (!services->set_player_selection(player_id, {})) {
      return command::CommandResult::internal_error("Unable to clear selection.");
    }
    return command::CommandResult::success("Selection cleared.");
  }

  if (action == "info") {
    const player::SelectionBoundsResult bounds = player::normalized_bounds(selection);
    if (!bounds.ok) {
      std::ostringstream out;
      out << bounds.message;
      if (selection.a.has_value()) {
        out << " A=" << player::format_pos(selection.a.value()) << ".";
      }
      if (selection.b.has_value()) {
        out << " B=" << player::format_pos(selection.b.value()) << ".";
      }
      command::CommandResult result = command::CommandResult::success();
      result.add_message(command::CommandMessageKind::Info, out.str());
      return result;
    }
    command::CommandResult result = command::CommandResult::success();
    result.add_message(
        command::CommandMessageKind::Info,
        "Selection: " + player::format_bounds(bounds.bounds) + ".");
    return result;
  }

  if (action != "a" && action != "b") {
    return command::CommandResult::usage_error("Usage: /select <a|b|clear|info>.");
  }
  if (!ctx.sender.world_id.has_value() || !ctx.sender.position.has_value()) {
    return command::CommandResult::invalid_source(
        "Selection points require a player world and position.");
  }

  const WorldID world = ctx.sender.world_id.value();
  const BlockPos pos = floor_block_pos(ctx.sender.position.value());
  if (selection.world.has_value() && selection.world.value() != world) {
    selection = {};
  }
  selection.world = world;
  if (action == "a") {
    selection.a = pos;
  } else {
    selection.b = pos;
  }
  if (!services->set_player_selection(player_id, to_command_selection(selection))) {
    return command::CommandResult::internal_error("Unable to update selection.");
  }

  command::CommandResult result = command::CommandResult::success(
      std::string{"Selection "} + (action == "a" ? "A" : "B") +
      " set to " + player::format_pos(pos) + ".");
  const player::SelectionBoundsResult bounds = player::normalized_bounds(selection);
  if (bounds.ok) {
    result.add_message(
        command::CommandMessageKind::Info,
        "Selection complete: " + player::format_bounds(bounds.bounds) + ".");
  }
  return result;
}

} // namespace

void register_selection_command(command::CommandRegistry& registry) {
  command::Command select;
  select.name = "player.selection";
  select.domain = command::CommandDomain::ServerBound;
  select.permission = std::string{ids::SelectionPermissionNode};
  select.description = "Sets or inspects the current player selection.";
  select.usage = "/select <a|b|clear|info>";
  select.execute = execute_selection;

  const auto command_result = registry.register_command(std::move(select));
  assert(command_result && "selection command registration failed");
  const auto alias_result = registry.register_alias("select", "player.selection", true);
  assert(alias_result && "select command alias registration failed");
  const auto short_alias_result = registry.register_alias("sel", "player.selection", true);
  assert(short_alias_result && "sel command alias registration failed");
  (void)command_result;
  (void)alias_result;
  (void)short_alias_result;
}

} // namespace arv::base::commands
