#include "arv_mason/mason_plugin.hpp"

#include "command_utils.hpp"

#include "arv_mason/ids.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arv_mason {

std::optional<MasonClipboard> MasonPlugin::copy_selection_to_clipboard(
    arv::CommandContext& ctx,
    const SelectionBounds& bounds,
    ArvBlockPos origin) const {
  MasonClipboard clipboard;
  clipboard.size = {
      static_cast<std::int32_t>(bounds.size_x),
      static_cast<std::int32_t>(bounds.size_y),
      static_cast<std::int32_t>(bounds.size_z),
  };
  clipboard.source_min = bounds.min;
  clipboard.origin = origin;
  clipboard.blocks.reserve(static_cast<std::size_t>(bounds.block_count));

  bool ok = true;
  std::string error;
  for_each_position(bounds, [&](ArvBlockPos pos) {
    if (!ok) {
      return;
    }
    auto block = plugin_.world().get_block(bounds.world, pos);
    if (!block) {
      ok = false;
      error = block.result.message();
      return;
    }
    clipboard.blocks.push_back({block.value, stable_block_name(block.value)});
  });
  if (!ok) {
    ctx.error("Failed to copy selection: " + error + ".");
    return std::nullopt;
  }
  return clipboard;
}

arv::Result MasonPlugin::handle_copy(arv::CommandContext& ctx, const Args& args) {
  if (!check_custom_permission(ctx, ids::perm_copy)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  if (!args.empty()) {
    ctx.error("Usage: /mason copy.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  std::optional<SelectionBounds> bounds = current_selection_bounds(ctx);
  if (!bounds.has_value()) {
    return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
  }
  if (!has_limit_bypass(ctx) && bounds->block_count > config_.limits.max_clipboard_blocks) {
    ctx.error(
        "Clipboard exceeds limit: " + count_text(bounds->block_count) +
        " blocks. Limit: " + count_text(config_.limits.max_clipboard_blocks) + ".");
    return arv::Result{arv::ResultCode::InvalidArgument, "clipboard limit exceeded"};
  }
  const std::optional<ArvBlockPos> origin = require_block_position(ctx);
  if (!origin.has_value()) {
    ctx.error("Copy requires a player position.");
    return arv::Result{arv::ResultCode::InvalidState, "missing position"};
  }
  auto clipboard = copy_selection_to_clipboard(ctx, *bounds, *origin);
  if (!clipboard.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "copy failed"};
  }
  state->clipboard = std::move(*clipboard);
  ctx.success("Copied " + count_text(state->clipboard->block_count()) + " blocks.");
  return arv::Result::ok();
}

arv::Result MasonPlugin::handle_paste(
    arv::CommandContext& ctx,
    const Args& args,
    bool confirmed) {
  if (!check_custom_permission(ctx, ids::perm_paste) || !check_modify_permission(ctx)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  bool paste_air = config_.behavior.default_paste_air;
  for (std::string_view arg : args) {
    if (arg == "--air") {
      paste_air = true;
    } else if (arg == "--no-air") {
      paste_air = false;
    } else {
      ctx.error("Usage: /mason paste [--air|--no-air].");
      return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
    }
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  if (!state->clipboard.has_value() || state->clipboard->empty()) {
    ctx.error("Clipboard is empty. Use /mason copy first.");
    return arv::Result{arv::ResultCode::InvalidState, "clipboard empty"};
  }
  const MasonClipboard& clipboard = *state->clipboard;
  if (!require_limit_or_confirmation(
          ctx,
          *state,
          {PendingOperationKind::Paste, copy_args(args)},
          clipboard.block_count(),
          config_.limits.max_clipboard_blocks,
          confirmed)) {
    return arv::Result::ok();
  }
  const std::optional<ArvWorldHandle> world = require_world(ctx);
  const std::optional<ArvBlockPos> paste_origin = require_block_position(ctx);
  if (!world.has_value() || !paste_origin.has_value()) {
    ctx.error("Paste requires a player world and position.");
    return arv::Result{arv::ResultCode::InvalidState, "missing position"};
  }
  auto snapshot = snapshot_clipboard_destination(
      ctx,
      clipboard,
      *world,
      *paste_origin,
      paste_air);
  if (!snapshot.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "snapshot failed"};
  }
  OperationPlan plan = build_paste_plan_from_snapshot(
      *world,
      *snapshot,
      clipboard,
      *paste_origin,
      air_variant(),
      paste_air);
  return execute_plan(
      ctx,
      *ctx.player(),
      *state,
      std::move(plan),
      "Paste complete");
}

} // namespace arv_mason
