#include "gamemode_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <array>
#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace arv::base::commands {
namespace {

struct BuiltInModeCommand {
  std::string_view mode_id{};
  std::string_view alias{};
};

constexpr std::array<BuiltInModeCommand, 5> BuiltInModeCommands{{
    {ids::SurvivalGameModeID, "gms"},
    {ids::CreativeGameModeID, "gmc"},
    {ids::SpectatorGameModeID, "gmsp"},
    {ids::LobbyGameModeID, "gml"},
    {ids::NoClipGameModeID, "gmn"},
}};

[[nodiscard]] std::string gamemode_permission_node(std::string_view mode_id) {
  std::string permission{"arvane.command.gamemode."};
  permission.append(mode_id);
  return permission;
}

[[nodiscard]] command::CommandResult permission_denied(
    command::CommandCtx& ctx,
    std::string permission,
    const command::CommandPermissionResult& result) {
  if (result.has_message) {
    return command::CommandResult::permission_denied(result.message);
  }
  return command::CommandResult::permission_denied(arv::text::format_or_fallback(
      ctx.services.language,
      "command.dispatcher.permission_denied",
      {{"permission", std::move(permission)}},
      "Permission denied: {permission}."));
}

[[nodiscard]] command::CommandResult check_dynamic_gamemode_permission(
    command::CommandCtx& ctx,
    std::string_view mode_id) {
  if (ctx.sender.kind != command::CommandSourceKind::Player) {
    return command::CommandResult::success();
  }

  const std::string permission = gamemode_permission_node(mode_id);
  if (!ctx.services.permission_check) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.dispatcher.permission_service_unavailable",
        "Command permission service is unavailable."));
  }

  command::Command checked_command = ctx.command;
  checked_command.permission = permission;
  const command::CommandPermissionResult result =
      ctx.services.permission_check(ctx.sender, checked_command);
  if (result.allowed) {
    return command::CommandResult::success();
  }
  return permission_denied(ctx, permission, result);
}

[[nodiscard]] command::CommandResult set_sender_game_mode(
    command::CommandCtx& ctx,
    std::string_view mode_id,
    bool check_permission) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr || !services->set_player_game_mode) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.gamemode.services_unavailable",
        "Server command services are unavailable."));
  }
  if (!ctx.sender.player_id.has_value()) {
    return command::CommandResult::invalid_source(arv::text::text_or_fallback(
        ctx.services.language,
        "command.gamemode.player_required",
        "Changing game mode requires a player sender."));
  }
  if (check_permission) {
    command::CommandResult access = check_dynamic_gamemode_permission(ctx, mode_id);
    if (!access) {
      return access;
    }
  }

  if (!services->set_player_game_mode(ctx.sender.player_id.value(), mode_id)) {
    return command::CommandResult::usage_error(arv::text::format_or_fallback(
        ctx.services.language,
        "command.gamemode.unknown_mode",
        {{"mode", std::string{mode_id}}},
        "Unknown game mode: {mode}."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
      "command.gamemode.success",
      {{"mode", std::string{mode_id}}},
      "Set game mode to {mode}."));
}

[[nodiscard]] command::CommandResult execute_gamemode(command::CommandCtx& ctx) {
  if (ctx.args.size() != 1U) {
    return command::CommandResult::usage_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.gamemode.usage",
        "Usage: /gamemode <mode>."));
  }

  command::CommandArgReader args = ctx.reader();
  auto mode = args.read_token("game mode");
  if (!mode) {
    return mode.error();
  }
  auto end = args.expect_end();
  if (!end) {
    return end.error();
  }
  return set_sender_game_mode(ctx, mode.value(), true);
}

[[nodiscard]] command::Command make_mode_command(std::string_view mode_id) {
  command::Command definition;
  definition.name = "player.gamemode.";
  definition.name.append(mode_id);
  definition.domain = command::CommandDomain::ServerBound;
  definition.permission = gamemode_permission_node(mode_id);
  definition.description = "Changes your game mode.";
  definition.usage = "/gamemode ";
  definition.usage.append(mode_id);
  definition.execute = [mode = std::string{mode_id}](command::CommandCtx& ctx) {
    if (!ctx.args.empty()) {
      return command::CommandResult::usage_error(arv::text::text_or_fallback(
          ctx.services.language,
          "command.gamemode.alias_usage",
          "Usage: /gm<mode>."));
    }
    return set_sender_game_mode(ctx, mode, false);
  };
  return definition;
}

} // namespace

void register_gamemode_command(command::CommandRegistry& registry) {
  command::Command gamemode;
  gamemode.name = "gamemode";
  gamemode.domain = command::CommandDomain::ServerBound;
  gamemode.description = "Changes your game mode.";
  gamemode.usage = "/gamemode <mode>";
  gamemode.execute = execute_gamemode;

  auto command_result = registry.register_command(std::move(gamemode));
  assert(command_result && "gamemode command registration failed");
  (void)command_result;

  for (const BuiltInModeCommand& mode : BuiltInModeCommands) {
    command_result = registry.register_command(make_mode_command(mode.mode_id));
    assert(command_result && "gamemode mode command registration failed");
    (void)command_result;
    std::string command_name{"player.gamemode."};
    command_name.append(mode.mode_id);
    const auto alias_result =
        registry.register_alias(std::string{mode.alias}, std::move(command_name), true);
    assert(alias_result && "gamemode command alias registration failed");
    (void)alias_result;
  }
}

} // namespace arv::base::commands
