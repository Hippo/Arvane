#include "blueprint_command.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/base/command_services.hpp"
#include "arv/blueprint/blueprint.hpp"
#include "arv/command/command.hpp"
#include "arv/core/types.hpp"
#include "arv/player/selection.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace arv::base::commands {
namespace {

constexpr u64 MaxBlueprintBlocks = 1'000'000U;

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

[[nodiscard]] command::CommandResult require_services(
    const ServerCommandServices*& services,
    command::CommandCtx& ctx) {
  services = ctx.services.as<ServerCommandServices>();
  if (services == nullptr ||
      !services->player_selection_by_id ||
      !services->read_block ||
      !services->write_block ||
      !services->block_variant_name ||
      !services->block_variant_by_name ||
      !services->air_block_variant ||
      !services->list_blueprints ||
      !services->load_blueprint ||
      !services->save_blueprint) {
    return command::CommandResult::internal_error(
        "Blueprint command services are unavailable.");
  }
  return command::CommandResult::success();
}

[[nodiscard]] command::CommandResult require_player_world(
    const command::CommandCtx& ctx,
    PlayerID& out_player,
    WorldID& out_world) {
  if (!ctx.sender.player_id.has_value()) {
    return command::CommandResult::invalid_source(
        "Blueprint commands require a player source.");
  }
  if (!ctx.sender.world_id.has_value()) {
    return command::CommandResult::invalid_source(
        "Blueprint commands require a player world.");
  }
  out_player = ctx.sender.player_id.value();
  out_world = ctx.sender.world_id.value();
  return command::CommandResult::success();
}

[[nodiscard]] command::CommandResult require_player_position(
    const command::CommandCtx& ctx,
    BlockPos& out_position) {
  if (!ctx.sender.position.has_value()) {
    return command::CommandResult::invalid_source(
        "Blueprint commands require a player position.");
  }
  out_position = floor_block_pos(ctx.sender.position.value());
  return command::CommandResult::success();
}

void for_each_position(
    const player::SelectionBounds& bounds,
    const FunctionRef<void(BlockPos)>& visitor) {
  for (i32 y = bounds.min.value.y; y <= bounds.max.value.y; ++y) {
    for (i32 z = bounds.min.value.z; z <= bounds.max.value.z; ++z) {
      for (i32 x = bounds.min.value.x; x <= bounds.max.value.x; ++x) {
        visitor(BlockPos{{x, y, z}});
        if (x == bounds.max.value.x) {
          break;
        }
      }
      if (z == bounds.max.value.z) {
        break;
      }
    }
    if (y == bounds.max.value.y) {
      break;
    }
  }
}

[[nodiscard]] bool size_fits_block_pos(const player::SelectionBounds& bounds) noexcept {
  return bounds.size_x <= static_cast<u64>(std::numeric_limits<i32>::max()) &&
         bounds.size_y <= static_cast<u64>(std::numeric_limits<i32>::max()) &&
         bounds.size_z <= static_cast<u64>(std::numeric_limits<i32>::max());
}

[[nodiscard]] std::optional<blueprint::BlueprintClipboard> copy_selection_to_blueprint_clipboard(
    const ServerCommandServices& services,
    const player::SelectionBounds& bounds,
    BlockPos origin,
    std::string& out_error) {
  if (!size_fits_block_pos(bounds)) {
    out_error = "Selection is too large for the blueprint format.";
    return std::nullopt;
  }

  blueprint::BlueprintClipboard clipboard;
  clipboard.size = {{
      static_cast<i32>(bounds.size_x),
      static_cast<i32>(bounds.size_y),
      static_cast<i32>(bounds.size_z),
  }};
  clipboard.source_min = bounds.min;
  clipboard.origin = origin;
  clipboard.blocks.reserve(static_cast<usize>(bounds.block_count));

  bool ok = true;
  for_each_position(bounds, [&](BlockPos pos) {
    if (!ok) {
      return;
    }
    std::optional<u32> block = services.read_block(bounds.world, pos);
    if (!block.has_value()) {
      ok = false;
      out_error = "failed to read selection block";
      return;
    }
    std::optional<std::string> stable_name = services.block_variant_name(block.value());
    if (!stable_name.has_value()) {
      ok = false;
      out_error = "selection contains an unknown block variant";
      return;
    }
    clipboard.blocks.push_back({block.value(), std::move(stable_name.value())});
  });
  if (!ok) {
    return std::nullopt;
  }
  return clipboard;
}

[[nodiscard]] std::optional<std::vector<u32>> resolve_palette(
    const ServerCommandServices& services,
    const blueprint::Blueprint& document,
    std::string& out_error) {
  std::vector<u32> palette;
  palette.reserve(document.palette.size());
  for (const std::string& stable_id : document.palette) {
    std::optional<u32> variant = services.block_variant_by_name(stable_id);
    if (!variant.has_value()) {
      out_error = "unknown block state in blueprint: " + stable_id;
      return std::nullopt;
    }
    palette.push_back(variant.value());
  }
  return palette;
}

[[nodiscard]] u64 blueprint_index(const blueprint::Blueprint& document, i32 x, i32 y, i32 z) {
  const u64 ux = static_cast<u64>(x);
  const u64 uy = static_cast<u64>(y);
  const u64 uz = static_cast<u64>(z);
  const u64 sx = static_cast<u64>(document.size.value.x);
  const u64 sz = static_cast<u64>(document.size.value.z);
  return ux + sx * (uz + sz * uy);
}

[[nodiscard]] BlockPos add_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return {{
      lhs.value.x + rhs.value.x,
      lhs.value.y + rhs.value.y,
      lhs.value.z + rhs.value.z,
  }};
}

[[nodiscard]] BlockPos sub_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return {{
      lhs.value.x - rhs.value.x,
      lhs.value.y - rhs.value.y,
      lhs.value.z - rhs.value.z,
  }};
}

[[nodiscard]] command::CommandResult blueprint_info_result(
    std::string_view name,
    const blueprint::Blueprint& document) {
  const u64 blocks = blueprint::blueprint_volume(document.size);
  std::ostringstream out;
  out << "Blueprint " << name << ".abp: "
      << document.size.value.x << " x " << document.size.value.y
      << " x " << document.size.value.z << " = " << blocks
      << " blocks, palette " << document.palette.size() << ".";
  command::CommandResult result = command::CommandResult::success();
  result.add_message(command::CommandMessageKind::Info, out.str());
  return result;
}

[[nodiscard]] command::CommandResult load_blueprint_document(
    const ServerCommandServices& services,
    WorldID world,
    std::string_view name,
    blueprint::BlueprintCodecResult& out_decoded) {
  const std::optional<std::string> text = services.load_blueprint(world, name);
  if (!text.has_value()) {
    return command::CommandResult::failure(
        "Blueprint not found: " + std::string{name} + ".abp.");
  }
  out_decoded = blueprint::decode_blueprint(text.value());
  if (!out_decoded.ok) {
    return command::CommandResult::usage_error(
        "Blueprint is corrupt: " + out_decoded.message);
  }
  return command::CommandResult::success();
}

[[nodiscard]] command::CommandResult execute_blueprint(command::CommandCtx& ctx) {
  const ServerCommandServices* services{};
  command::CommandResult service_result = require_services(services, ctx);
  if (!service_result) {
    return service_result;
  }
  if (ctx.args.empty()) {
    return command::CommandResult::usage_error(
        "Usage: /blueprint <save|load|paste|list|info>.");
  }

  PlayerID player_id;
  WorldID world;
  command::CommandResult source_result = require_player_world(ctx, player_id, world);
  if (!source_result) {
    return source_result;
  }

  const std::string_view action = ctx.args[0];
  if (action == "list") {
    const std::vector<std::string> names = services->list_blueprints(world);
    if (names.empty()) {
      command::CommandResult result = command::CommandResult::success();
      result.add_message(command::CommandMessageKind::Info, "No blueprints saved.");
      return result;
    }
    std::ostringstream out;
    out << "Blueprints:";
    for (const std::string& name : names) {
      out << ' ' << name;
    }
    command::CommandResult result = command::CommandResult::success();
    result.add_message(command::CommandMessageKind::Info, out.str());
    return result;
  }

  if (ctx.args.size() < 2U) {
    return command::CommandResult::usage_error("Blueprint name is required.");
  }
  const std::string name{ctx.args[1]};
  if (!blueprint::valid_blueprint_name(name)) {
    return command::CommandResult::usage_error(
        "Invalid blueprint name. Use only letters, numbers, underscore, dash, and dot.");
  }

  if (action == "save") {
    BlockPos origin;
    command::CommandResult position_result = require_player_position(ctx, origin);
    if (!position_result) {
      return position_result;
    }
    const std::optional<command::PlayerBlockSelection> current =
        services->player_selection_by_id(player_id);
    if (!current.has_value()) {
      return command::CommandResult::invalid_source("Command sender is not an online player.");
    }
    const player::SelectionBoundsResult bounds =
        player::normalized_bounds(to_player_selection(current.value()));
    if (!bounds.ok) {
      return command::CommandResult::failure(bounds.message);
    }
    if (bounds.bounds.block_count > MaxBlueprintBlocks) {
      return command::CommandResult::usage_error(
          "Blueprint exceeds limit: " + std::to_string(bounds.bounds.block_count) +
          " blocks. Limit: " + std::to_string(MaxBlueprintBlocks) + ".");
    }

    std::string error;
    std::optional<blueprint::BlueprintClipboard> clipboard =
        copy_selection_to_blueprint_clipboard(*services, bounds.bounds, origin, error);
    if (!clipboard.has_value()) {
      return command::CommandResult::internal_error("Failed to copy selection: " + error + ".");
    }
    blueprint::Blueprint document =
        blueprint::blueprint_from_clipboard(name, clipboard.value(), std::string{ids::ModuleName});
    blueprint::BlueprintCodecResult encoded = blueprint::encode_blueprint(document);
    if (!encoded.ok) {
      return command::CommandResult::usage_error(encoded.message);
    }
    if (!services->save_blueprint(world, name, encoded.text)) {
      return command::CommandResult::internal_error("Failed to save blueprint.");
    }
    return command::CommandResult::success("Blueprint saved: " + name + ".abp.");
  }

  if (action == "load" || action == "info" || action == "paste") {
    blueprint::BlueprintCodecResult decoded;
    command::CommandResult loaded =
        load_blueprint_document(*services, world, name, decoded);
    if (!loaded) {
      return loaded;
    }
    const u64 blocks = blueprint::blueprint_volume(decoded.blueprint.size);
    if (blocks > MaxBlueprintBlocks) {
      return command::CommandResult::usage_error(
          "Blueprint exceeds limit: " + std::to_string(blocks) +
          " blocks. Limit: " + std::to_string(MaxBlueprintBlocks) + ".");
    }
    if (action == "info") {
      return blueprint_info_result(name, decoded.blueprint);
    }
    if (action == "load") {
      command::CommandResult result =
          command::CommandResult::success("Blueprint loaded: " + name + ".abp.");
      result.add_message(
          command::CommandMessageKind::Info,
          "Use /blueprint paste " + name + " to paste it at your current position.");
      return result;
    }

    BlockPos paste_origin;
    command::CommandResult position_result = require_player_position(ctx, paste_origin);
    if (!position_result) {
      return position_result;
    }
    bool paste_air = false;
    for (usize index = 2U; index < ctx.args.size(); ++index) {
      if (ctx.args[index] == "--air") {
        paste_air = true;
      } else if (ctx.args[index] == "--no-air") {
        paste_air = false;
      } else {
        return command::CommandResult::usage_error(
            "Usage: /blueprint paste <name> [--air|--no-air].");
      }
    }

    std::string error;
    std::optional<std::vector<u32>> palette =
        resolve_palette(*services, decoded.blueprint, error);
    if (!palette.has_value()) {
      return command::CommandResult::usage_error("Blueprint cannot be resolved: " + error + ".");
    }
    const std::optional<u32> air = services->air_block_variant();
    if (!air.has_value()) {
      return command::CommandResult::internal_error("Air block variant is unavailable.");
    }

    u64 changed = 0U;
    for (i32 y = 0; y < decoded.blueprint.size.value.y; ++y) {
      for (i32 z = 0; z < decoded.blueprint.size.value.z; ++z) {
        for (i32 x = 0; x < decoded.blueprint.size.value.x; ++x) {
          const u64 index = blueprint_index(decoded.blueprint, x, y, z);
          if (index >= decoded.blueprint.blocks.size()) {
            continue;
          }
          const u32 palette_index = decoded.blueprint.blocks[index];
          if (palette_index >= palette->size()) {
            return command::CommandResult::usage_error(
                "Blueprint block references an invalid palette index.");
          }
          const u32 variant = palette->at(palette_index);
          if (!paste_air && variant == air.value()) {
            continue;
          }
          const BlockPos source{{x, y, z}};
          const BlockPos dest = add_pos(
              paste_origin,
              sub_pos(source, decoded.blueprint.origin));
          const std::optional<u32> before = services->read_block(world, dest);
          if (!before.has_value()) {
            return command::CommandResult::internal_error("Failed to read paste destination.");
          }
          if (before.value() == variant) {
            continue;
          }
          if (!services->write_block(world, dest, variant)) {
            return command::CommandResult::internal_error("Failed to write paste destination.");
          }
          ++changed;
        }
      }
    }
    return command::CommandResult::success(
        "Blueprint pasted: changed " + std::to_string(changed) + " blocks.");
  }

  return command::CommandResult::usage_error(
      "Usage: /blueprint <save|load|paste|list|info>.");
}

} // namespace

void register_blueprint_command(command::CommandRegistry& registry) {
  command::Command blueprint;
  blueprint.name = "world.blueprint";
  blueprint.domain = command::CommandDomain::ServerBound;
  blueprint.permission = std::string{ids::BlueprintPermissionNode};
  blueprint.description = "Saves, inspects, and pastes Arvane blueprints.";
  blueprint.usage = "/blueprint <save|load|paste|list|info>";
  blueprint.execute = execute_blueprint;

  const auto command_result = registry.register_command(std::move(blueprint));
  assert(command_result && "blueprint command registration failed");
  const auto alias_result = registry.register_alias("blueprint", "world.blueprint", true);
  assert(alias_result && "blueprint command alias registration failed");
  const auto short_alias_result = registry.register_alias("bp", "world.blueprint", true);
  assert(short_alias_result && "bp command alias registration failed");
  (void)command_result;
  (void)alias_result;
  (void)short_alias_result;
}

} // namespace arv::base::commands
