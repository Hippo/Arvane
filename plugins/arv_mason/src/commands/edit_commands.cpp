#include "arv_mason/mason_plugin.hpp"

#include "command_utils.hpp"

#include "arv_mason/ids.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace arv_mason {
namespace {

[[nodiscard]] std::string operation_kind_name(PendingOperationKind kind) {
  switch (kind) {
    case PendingOperationKind::Set:
      return "set";
    case PendingOperationKind::Replace:
      return "replace";
    case PendingOperationKind::Paste:
      return "paste";
  }
  return "operation";
}

} // namespace

arv::Result MasonPlugin::handle_set(
    arv::CommandContext& ctx,
    const Args& args,
    bool confirmed) {
  if (!check_custom_permission(ctx, ids::perm_set) || !check_modify_permission(ctx)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  if (args.size() != 1U) {
    ctx.error("Usage: /mason set <block_state>.");
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
  if (!require_limit_or_confirmation(
          ctx,
          *state,
          {PendingOperationKind::Set, copy_args(args)},
          bounds->block_count,
          config_.limits.max_selection_blocks,
          confirmed)) {
    return arv::Result::ok();
  }
  const std::optional<std::uint32_t> to = parse_block_variant(ctx, args[0]);
  if (!to.has_value()) {
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid block state"};
  }
  auto snapshot = snapshot_selection(ctx, *bounds);
  if (!snapshot.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "snapshot failed"};
  }
  OperationPlan plan = build_set_plan_from_snapshot(bounds->world, *snapshot, *to);
  return execute_plan(
      ctx,
      *ctx.player(),
      *state,
      std::move(plan),
      "Set complete");
}

arv::Result MasonPlugin::handle_replace(
    arv::CommandContext& ctx,
    const Args& args,
    bool confirmed) {
  if (!check_custom_permission(ctx, ids::perm_replace) || !check_modify_permission(ctx)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  if (args.size() != 2U) {
    ctx.error("Usage: /mason replace <from> <to>.");
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
  if (!require_limit_or_confirmation(
          ctx,
          *state,
          {PendingOperationKind::Replace, copy_args(args)},
          bounds->block_count,
          config_.limits.max_selection_blocks,
          confirmed)) {
    return arv::Result::ok();
  }
  const std::optional<std::uint32_t> from = parse_block_variant(ctx, args[0]);
  const std::optional<std::uint32_t> to = parse_block_variant(ctx, args[1]);
  if (!from.has_value() || !to.has_value()) {
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid block state"};
  }
  auto snapshot = snapshot_selection(ctx, *bounds);
  if (!snapshot.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "snapshot failed"};
  }
  OperationPlan plan = build_replace_plan_from_snapshot(
      bounds->world,
      *snapshot,
      *from,
      *to);
  return execute_plan(
      ctx,
      *ctx.player(),
      *state,
      std::move(plan),
      "Replace complete");
}

arv::Result MasonPlugin::execute_plan(
    arv::CommandContext& ctx,
    ArvPlayerID player,
    MasonPlayerState& state,
    OperationPlan plan,
    std::string complete_message) {
  if (plan.changes.empty()) {
    ctx.success(complete_message + ": changed 0 blocks.");
    return arv::Result::ok();
  }
  const std::uint64_t batch = config_.limits.max_operation_blocks_per_tick;
  if (plan.changes.size() > batch) {
    const std::size_t count = plan.changes.size();
    const std::string summary = plan.summary;
    schedule_batched_plan(player, std::move(plan), summary);
    ctx.success(
        "Operation started: " + count_text(count) +
        " blocks in batches of " + count_text(batch) + ".");
    return arv::Result::ok();
  }
  std::string error;
  if (!apply_changes_sync(plan, error)) {
    ctx.error("Operation failed: " + error + ".");
    return arv::Result{arv::ResultCode::InternalError, "operation failed"};
  }
  push_history(player, state, plan);
  ctx.success(complete_message + ": changed " + count_text(plan.changes.size()) + " blocks.");
  return arv::Result::ok();
}

bool MasonPlugin::apply_changes_sync(const OperationPlan& plan, std::string& out_error) const {
  for (const BlockChange& change : plan.changes) {
    arv::Result result = plugin_.world().set_block(plan.world, change.pos, change.after);
    if (!result) {
      out_error = result.message();
      return false;
    }
  }
  return true;
}

void MasonPlugin::schedule_batched_plan(
    ArvPlayerID player,
    OperationPlan plan,
    std::string history_summary) {
  struct RunningOperation {
    OperationPlan plan;
    std::size_t cursor{};
    std::string summary;
  };
  auto running = std::make_shared<RunningOperation>(
      RunningOperation{std::move(plan), 0U, std::move(history_summary)});
  auto step = std::make_shared<std::function<void()>>();
  *step = [this, player, running, step]() {
    const std::size_t batch = static_cast<std::size_t>(
        config_.limits.max_operation_blocks_per_tick);
    const std::size_t end = std::min(running->cursor + batch, running->plan.changes.size());
    for (; running->cursor < end; ++running->cursor) {
      const BlockChange& change = running->plan.changes[running->cursor];
      arv::Result result = plugin_.world().set_block(running->plan.world, change.pos, change.after);
      if (!result) {
        plugin_.logger().error(std::string{"Mason operation failed: "} + result.message());
        return;
      }
    }
    if (running->cursor >= running->plan.changes.size()) {
      MasonPlayerState& state = players_[player];
      push_history(player, state, running->plan);
      plugin_.logger().info("Mason operation complete: " + running->summary);
      return;
    }
    arv::Result scheduled = plugin_.scheduler().sync_tick(1, [step]() { (*step)(); });
    if (!scheduled) {
      plugin_.logger().error(std::string{"Mason could not schedule operation batch: "} + scheduled.message());
    }
  };
  arv::Result scheduled = plugin_.scheduler().sync_tick(1, [step]() { (*step)(); });
  if (!scheduled) {
    std::string error;
    if (!apply_changes_sync(running->plan, error)) {
      plugin_.logger().error("Mason fallback sync operation failed: " + error);
      return;
    }
    MasonPlayerState& state = players_[player];
    push_history(player, state, running->plan);
  }
}

arv::Result MasonPlugin::handle_confirm(arv::CommandContext& ctx, const Args& args) {
  if (!args.empty()) {
    ctx.error("Usage: /mason confirm.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  if (!state->pending.has_value()) {
    ctx.info("No Mason operation is awaiting confirmation.");
    return arv::Result::ok();
  }
  PendingOperation pending = std::move(*state->pending);
  state->pending.reset();
  const Args pending_args = view_args(pending.args);
  ctx.info("Confirmed Mason " + operation_kind_name(pending.kind) + " operation.");
  switch (pending.kind) {
    case PendingOperationKind::Set:
      return handle_set(ctx, pending_args, true);
    case PendingOperationKind::Replace:
      return handle_replace(ctx, pending_args, true);
    case PendingOperationKind::Paste:
      return handle_paste(ctx, pending_args, true);
  }
  return arv::Result{arv::ResultCode::InvalidState, "unknown pending operation"};
}

} // namespace arv_mason
