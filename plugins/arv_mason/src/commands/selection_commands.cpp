#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <optional>
#include <sstream>
#include <string>

namespace arv_mason {

arv::Result MasonPlugin::handle_select(arv::CommandContext& ctx, const Args& args) {
  if (!check_custom_permission(ctx, ids::perm_select)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  if (args.empty()) {
    ctx.error("Usage: /mason select <a|b|clear|info>.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }

  const std::optional<ArvPlayerID> player = ctx.player();
  const std::string_view action = args[0];
  if (action == "clear") {
    state->selection = {};
    save_player_state(*player, *state);
    ctx.success("Selection cleared.");
    return arv::Result::ok();
  }
  if (action == "info") {
    SelectionBoundsResult bounds = normalized_bounds(state->selection);
    if (!bounds.ok) {
      std::ostringstream out;
      out << bounds.message;
      if (state->selection.a.has_value()) {
        out << " A=" << format_pos(*state->selection.a) << ".";
      }
      if (state->selection.b.has_value()) {
        out << " B=" << format_pos(*state->selection.b) << ".";
      }
      ctx.info(out.str());
      return arv::Result::ok();
    }
    ctx.info("Selection: " + format_bounds(bounds.bounds) + ".");
    return arv::Result::ok();
  }
  if (action != "a" && action != "b") {
    ctx.error("Usage: /mason select <a|b|clear|info>.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }

  const std::optional<ArvWorldHandle> world = require_world(ctx);
  const std::optional<ArvBlockPos> pos = require_block_position(ctx);
  if (!world.has_value() || !pos.has_value()) {
    ctx.error("Selection points require a player world and position. Target block context is not exposed yet, so Mason uses your current block.");
    return arv::Result{arv::ResultCode::InvalidState, "missing player position"};
  }
  if (state->selection.world.has_value() && *state->selection.world != *world) {
    state->selection = {};
  }
  state->selection.world = *world;
  if (action == "a") {
    state->selection.a = *pos;
    ctx.success("Selection A set to " + format_pos(*pos) + ".");
  } else {
    state->selection.b = *pos;
    ctx.success("Selection B set to " + format_pos(*pos) + ".");
  }
  SelectionBoundsResult bounds = normalized_bounds(state->selection);
  if (bounds.ok) {
    ctx.info("Selection complete: " + format_bounds(bounds.bounds) + ".");
  }
  save_player_state(*player, *state);
  return arv::Result::ok();
}

} // namespace arv_mason
