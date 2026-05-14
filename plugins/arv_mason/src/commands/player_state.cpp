#include "arv_mason/mason_plugin.hpp"

#include "command_utils.hpp"

#include "arv_mason/ids.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arv_mason {
namespace {

[[nodiscard]] ArvBlockPos floor_block_pos(ArvVec3 position) {
  return {
      static_cast<std::int32_t>(std::floor(position.x)),
      static_cast<std::int32_t>(std::floor(position.y)),
      static_cast<std::int32_t>(std::floor(position.z)),
  };
}

} // namespace

MasonPlayerState* MasonPlugin::state_for(arv::CommandContext& ctx) {
  const std::optional<ArvPlayerID> player = require_player(ctx);
  if (!player.has_value()) {
    ctx.error("This Mason command requires a player.");
    return nullptr;
  }
  MasonPlayerState& state = players_[*player];
  state.history.set_limit(static_cast<std::size_t>(config_.limits.max_history_entries));
  return &state;
}

std::optional<ArvPlayerID> MasonPlugin::require_player(arv::CommandContext& ctx) const {
  return ctx.player();
}

std::optional<ArvWorldHandle> MasonPlugin::require_world(arv::CommandContext& ctx) const {
  return ctx.world();
}

std::optional<ArvBlockPos> MasonPlugin::require_block_position(arv::CommandContext& ctx) const {
  auto position = ctx.position();
  if (!position.has_value()) {
    return std::nullopt;
  }
  return floor_block_pos(*position);
}

std::optional<SelectionBounds> MasonPlugin::current_selection_bounds(
    arv::CommandContext& ctx) const {
  const std::optional<ArvPlayerID> player = require_player(ctx);
  if (!player.has_value()) {
    ctx.error("This Mason command requires a player.");
    return std::nullopt;
  }
  auto selection = plugin_.players().current_selection(*player);
  if (!selection) {
    ctx.error(std::string{"Unable to read current selection: "} + selection.result.message());
    return std::nullopt;
  }
  SelectionBoundsResult bounds = normalized_bounds(selection.value);
  if (!bounds.ok) {
    ctx.error(bounds.message);
    return std::nullopt;
  }
  return bounds.bounds;
}

bool MasonPlugin::check_custom_permission(
    arv::CommandContext& ctx,
    std::string_view permission) const {
  const std::optional<ArvPlayerID> player = ctx.player();
  if (!player.has_value()) {
    return true;
  }
  auto checked = plugin_.permissions().check_custom(*player, permission);
  if (!checked || checked.value != arv::PermissionValue::Allow) {
    ctx.error("Permission denied: " + std::string{permission} + ".");
    return false;
  }
  return true;
}

bool MasonPlugin::check_modify_permission(arv::CommandContext& ctx) const {
  const std::optional<ArvPlayerID> player = ctx.player();
  if (!player.has_value()) {
    ctx.error("World edits require a player source.");
    return false;
  }
  auto checked = plugin_.permissions().check_core(*player, arv::CorePermission::CanModifyWorld);
  if (!checked || !checked.value) {
    ctx.error("Permission denied: can_modify_world is required.");
    return false;
  }
  return true;
}

bool MasonPlugin::has_limit_bypass(arv::CommandContext& ctx) const {
  const std::optional<ArvPlayerID> player = ctx.player();
  if (!player.has_value()) {
    return true;
  }
  auto checked = plugin_.permissions().check_custom(*player, ids::perm_limit_bypass);
  return checked && checked.value == arv::PermissionValue::Allow;
}

bool MasonPlugin::require_limit_or_confirmation(
    arv::CommandContext& ctx,
    MasonPlayerState& state,
    PendingOperation pending,
    std::uint64_t count,
    std::uint64_t limit,
    bool confirmed) {
  const bool bypass = has_limit_bypass(ctx);
  if (!bypass && count > limit) {
    ctx.error(
        "Operation exceeds limit: " + count_text(count) +
        " blocks. Limit: " + count_text(limit) + ".");
    return false;
  }
  if (!bypass && !confirmed &&
      count >= config_.behavior.require_confirmation_above_blocks) {
    state.pending = std::move(pending);
    ctx.warn(
        "Operation affects " + count_text(count) +
        " blocks. Run /mason confirm to proceed.");
    return false;
  }
  return true;
}

} // namespace arv_mason
