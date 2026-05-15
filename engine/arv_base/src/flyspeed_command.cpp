#include "flyspeed_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <cassert>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>

namespace arv::base::commands {
namespace {

constexpr f32 MinFlySpeedMultiplier = 1.0F;
constexpr f32 MaxFlySpeedMultiplier = 10.0F;

[[nodiscard]] std::string format_speed(f32 value) {
  std::ostringstream out;
  out << value;
  return out.str();
}

[[nodiscard]] command::CommandResult execute_flyspeed(command::CommandCtx& ctx) {
  const ServerCommandServices* services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->set_player_fly_speed ||
      !services->default_player_fly_speed ||
      !services->max_player_fly_speed) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.services_unavailable",
        "Server command services are unavailable."));
  }
  if (!ctx.sender.player_id.has_value()) {
    return command::CommandResult::invalid_source(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.player_required",
        "Fly speed changes require a player sender."));
  }
  if (ctx.args.size() != 1U) {
    return command::CommandResult::usage_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.usage",
        "Usage: /flyspeed <multiplier 1-10>."));
  }

  command::CommandArgReader args = ctx.reader();
  auto multiplier = args.read_f32("fly speed multiplier");
  if (!multiplier) {
    return multiplier.error();
  }
  auto end = args.expect_end();
  if (!end) {
    return end.error();
  }
  if (!std::isfinite(multiplier.value()) ||
      multiplier.value() < MinFlySpeedMultiplier ||
      multiplier.value() > MaxFlySpeedMultiplier) {
    return command::CommandResult::usage_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.invalid_range",
        "Fly speed must be a multiplier from 1 to 10."));
  }

  const f32 default_fly_speed = services->default_player_fly_speed();
  if (!std::isfinite(default_fly_speed) || default_fly_speed <= 0.0F) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.unable_to_update",
        "Unable to update fly speed."));
  }
  const f32 fly_speed = default_fly_speed * multiplier.value();
  const f32 server_limit = services->max_player_fly_speed();
  if (!std::isfinite(server_limit) || fly_speed > server_limit) {
    return command::CommandResult::usage_error(arv::text::format_or_fallback(
        ctx.services.language,
        "command.flyspeed.exceeds_server_limit",
        {{"speed", format_speed(multiplier.value())},
         {"actual", format_speed(fly_speed)},
         {"limit", format_speed(server_limit)}},
        "Fly speed {speed}x ({actual} blocks/s) exceeds the server movement limit of {limit} blocks/s."));
  }

  if (!services->set_player_fly_speed(ctx.sender.player_id.value(), fly_speed)) {
    return command::CommandResult::internal_error(arv::text::text_or_fallback(
        ctx.services.language,
        "command.flyspeed.unable_to_update",
        "Unable to update fly speed."));
  }

  return command::CommandResult::success(arv::text::format_or_fallback(
      ctx.services.language,
      "command.flyspeed.success",
      {{"speed", format_speed(multiplier.value())},
       {"actual", format_speed(fly_speed)}},
      "Set fly speed to {speed}x."));
}

} // namespace

void register_flyspeed_command(command::CommandRegistry& registry) {
  command::Command flyspeed;
  flyspeed.name = "player.flyspeed";
  flyspeed.domain = command::CommandDomain::ServerBound;
  flyspeed.permission = std::string{ids::FlySpeedPermissionNode};
  flyspeed.description = "Changes your fly movement speed.";
  flyspeed.usage = "/flyspeed <multiplier>";
  flyspeed.execute = execute_flyspeed;

  const auto command_result = registry.register_command(std::move(flyspeed));
  assert(command_result && "flyspeed command registration failed");
  const auto alias_result = registry.register_alias("flyspeed", "player.flyspeed", true);
  assert(alias_result && "flyspeed command alias registration failed");
  (void)command_result;
  (void)alias_result;
}

} // namespace arv::base::commands
