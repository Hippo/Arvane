#include "explosion_command.hpp"

#include "arv/base/command_services.hpp"
#include "arv/command/command.hpp"
#include "arv/core/string.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/text/language.hpp"

#include <cassert>
#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace arv::base::commands {
namespace {

constexpr f32 MaxCommandExplosionRadius = 32.0F;
constexpr f32 MaxCommandExplosionPower = 96.0F;
constexpr u32 MaxCommandDestroyedBlocks = 8192U;
constexpr u32 MaxCommandRays = 4096U;
constexpr u32 MaxCommandDrops = 1024U;

[[nodiscard]] std::string msg(
    const command::CommandCtx& ctx,
    std::string_view key,
    std::string_view fallback) {
  return arv::text::text_or_fallback(ctx.services.language, key, fallback);
}

[[nodiscard]] std::string format_position(Vec3f position) {
  std::ostringstream out;
  out << position.x << ", " << position.y << ", " << position.z;
  return out.str();
}

[[nodiscard]] WorldID sender_world_id(const command::CommandSender& sender) noexcept {
  return sender.world_id.value_or(server::DefaultWorldID);
}

[[nodiscard]] std::optional<Vec3f> sender_position(
    const command::CommandSender& sender,
    const command::ServerCommandServices& services) {
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

[[nodiscard]] bool is_flag(std::string_view token) noexcept {
  return token.size() > 2U && token[0] == '-' && token[1] == '-';
}

template<typename T>
[[nodiscard]] bool parse_number(std::string_view token, T& out) noexcept {
  T value{};
  const char* begin = token.data();
  const char* end = begin + token.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return false;
  }
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  out = value;
  return true;
}

[[nodiscard]] bool looks_like_number(std::string_view token) noexcept {
  f32 ignored{};
  return parse_number(token, ignored);
}

[[nodiscard]] command::CommandResult validate_power_radius(
    const command::ExplosionCommandRequest& request) {
  if (request.power.has_value() &&
      (request.power.value() <= 0.0F || request.power.value() > MaxCommandExplosionPower)) {
    return command::CommandResult::usage_error(
        "Explosion power must be > 0 and <= " +
        std::to_string(MaxCommandExplosionPower) + ".");
  }
  if (request.radius.has_value() &&
      (request.radius.value() <= 0.0F || request.radius.value() > MaxCommandExplosionRadius)) {
    return command::CommandResult::usage_error(
        "Explosion radius must be > 0 and <= " +
        std::to_string(MaxCommandExplosionRadius) + ".");
  }
  if (request.max_destroyed_blocks.has_value() &&
      request.max_destroyed_blocks.value() > MaxCommandDestroyedBlocks) {
    return command::CommandResult::usage_error(
        "Explosion max-blocks exceeds command cap.");
  }
  if (request.max_rays.has_value() && request.max_rays.value() > MaxCommandRays) {
    return command::CommandResult::usage_error(
        "Explosion max-rays exceeds command cap.");
  }
  if (request.max_drops.has_value() && request.max_drops.value() > MaxCommandDrops) {
    return command::CommandResult::usage_error(
        "Explosion max-drops exceeds command cap.");
  }
  return command::CommandResult::success();
}

[[nodiscard]] command::CommandResult execute_explode(command::CommandCtx& ctx) {
  auto* services = ctx.services.as<command::ServerCommandServices>();
  if (services == nullptr || !services->create_explosion) {
    return command::CommandResult::internal_error(msg(
        ctx,
        "command.explode.services_unavailable",
        "Explosion services are unavailable."));
  }

  auto reader = ctx.reader();
  auto parsed_position = reader.read_position();
  if (!parsed_position) {
    return parsed_position.error();
  }
  auto origin = command::resolve_position(
      parsed_position.value(),
      sender_position(ctx.sender, *services));
  if (!origin) {
    return origin.error();
  }

  command::ExplosionCommandRequest request;
  request.world_id = sender_world_id(ctx.sender);
  request.origin = origin.value();
  request.profile_id = "arvane:tnt";
  request.source_entity = ctx.sender.entity_id;

  usize cursor = reader.cursor();
  if (cursor < ctx.args.size() && !is_flag(ctx.args[cursor]) &&
      !looks_like_number(ctx.args[cursor])) {
    request.profile_id = std::string{ctx.args[cursor++]};
  }
  if (cursor < ctx.args.size() && !is_flag(ctx.args[cursor])) {
    f32 power{};
    if (!parse_number(ctx.args[cursor], power)) {
      return command::CommandResult::usage_error("Explosion power must be a number.");
    }
    request.power = power;
    ++cursor;
  }
  if (cursor < ctx.args.size() && !is_flag(ctx.args[cursor])) {
    f32 radius{};
    if (!parse_number(ctx.args[cursor], radius)) {
      return command::CommandResult::usage_error("Explosion radius must be a number.");
    }
    request.radius = radius;
    ++cursor;
  }

  while (cursor < ctx.args.size()) {
    const std::string_view flag = ctx.args[cursor++];
    if (flag == "--no-block-damage") {
      request.block_damage_enabled = false;
    } else if (flag == "--no-entity-damage") {
      request.entity_damage_enabled = false;
    } else if (flag == "--no-drops") {
      request.drops_enabled = false;
    } else if (flag == "--fire") {
      request.fire_enabled = true;
    } else if (flag == "--no-fire") {
      request.fire_enabled = false;
    } else if (flag == "--no-sound") {
      request.future_sound_enabled = false;
    } else if (flag == "--seed") {
      if (cursor >= ctx.args.size()) {
        return command::CommandResult::usage_error("--seed requires a value.");
      }
      u64 seed{};
      if (!parse_number(ctx.args[cursor++], seed)) {
        return command::CommandResult::usage_error("--seed must be an unsigned integer.");
      }
      request.seed = seed;
    } else if (flag == "--max-blocks") {
      if (cursor >= ctx.args.size()) {
        return command::CommandResult::usage_error("--max-blocks requires a value.");
      }
      u32 value{};
      if (!parse_number(ctx.args[cursor++], value)) {
        return command::CommandResult::usage_error("--max-blocks must be an unsigned integer.");
      }
      request.max_destroyed_blocks = value;
    } else if (flag == "--max-rays") {
      if (cursor >= ctx.args.size()) {
        return command::CommandResult::usage_error("--max-rays requires a value.");
      }
      u32 value{};
      if (!parse_number(ctx.args[cursor++], value)) {
        return command::CommandResult::usage_error("--max-rays must be an unsigned integer.");
      }
      request.max_rays = value;
    } else if (flag == "--max-drops") {
      if (cursor >= ctx.args.size()) {
        return command::CommandResult::usage_error("--max-drops requires a value.");
      }
      u32 value{};
      if (!parse_number(ctx.args[cursor++], value)) {
        return command::CommandResult::usage_error("--max-drops must be an unsigned integer.");
      }
      request.max_drops = value;
    } else {
      return command::CommandResult::usage_error(
          "Unknown explosion flag: " + std::string{flag} + ".");
    }
  }

  if (command::CommandResult validation = validate_power_radius(request); !validation) {
    return validation;
  }

  const command::ExplosionCommandResult result = services->create_explosion(request);
  if (!result.success) {
    return command::CommandResult::failure(
        result.message.empty() ? "Unable to create explosion." : result.message);
  }
  return command::CommandResult::success(
      "Created explosion " + result.profile_id + " at " +
      format_position(result.origin) + ". Destroyed " +
      std::to_string(result.destroyed_blocks) + " blocks and affected " +
      std::to_string(result.affected_entities) + " entities.");
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

void register_explosion_command(command::CommandRegistry& registry) {
  command::Command command;
  command.name = "explode";
  command.domain = command::CommandDomain::ServerBound;
  command.permission = "command.explode";
  command.description = "Creates a server-authoritative explosion at the given position.";
  command.usage = "/explode <x y z> [profile] [power] [radius] [flags...]";
  command.arguments = {
      arg("position", command::CommandArgumentType::Position, true, "Absolute or relative x y z position."),
      arg("profile", command::CommandArgumentType::Identifier, false, "Explosion profile id."),
      arg("power", command::CommandArgumentType::Number, false, "Explosion power override."),
      arg("radius", command::CommandArgumentType::Number, false, "Explosion radius override."),
      arg("flags", command::CommandArgumentType::String, false, "Named explosion overrides.", {}, true),
  };
  command.technical_notes =
      "Constructs an ExplosionRequest and lets the server-owned explosion system mutate world state.";
  command.examples = {
      "/explode ~ ~ ~",
      "/explode ~ ~ ~ arvane:tnt",
      "/explode ~ ~ ~ arvane:tnt 20 8",
      "/explode ~ ~ ~ arvane:tnt --no-block-damage",
      "/explode ~ ~ ~ arvane:shockwave --no-block-damage",
      "/explode 0 80 0 arvane:debug 50 20 --no-drops --seed 123",
      "/boom ~ ~ ~ arvane:vehicle --seed 99",
  };
  command.execute = execute_explode;
  const auto command_result = registry.register_command(std::move(command));
  assert(command_result && "explode command registration failed");
  (void)command_result;
  const auto alias_result = registry.register_alias("boom", "explode", true);
  assert(alias_result && "explode command alias registration failed");
  (void)alias_result;
}

} // namespace arv::base::commands
