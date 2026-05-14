#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace arv_mason {
namespace {

[[nodiscard]] std::string count_text(std::uint64_t count) {
  std::ostringstream out;
  out << count;
  return out.str();
}

} // namespace

void MasonPlugin::push_history(
    ArvPlayerID,
    MasonPlayerState& state,
    const OperationPlan& plan) {
  HistoryEntry entry;
  entry.world = plan.world;
  entry.changes = plan.changes;
  entry.summary = plan.summary;
  state.history.push(std::move(entry));
}

arv::Result MasonPlugin::handle_undo(arv::CommandContext& ctx, const Args& args) {
  if (!args.empty()) {
    ctx.error("Usage: /mason undo.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  if (!check_custom_permission(ctx, ids::perm_undo) || !check_modify_permission(ctx)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  auto entry = state->history.take_undo();
  if (!entry.has_value()) {
    ctx.info("Nothing to undo.");
    return arv::Result::ok();
  }
  HistoryEntry inverse = inverse_history_entry(*entry, "undo " + entry->summary);
  OperationPlan plan{inverse.world, inverse.changes, inverse.summary};
  std::string error;
  if (!apply_changes_sync(plan, error)) {
    state->history.push_undo_without_clearing_redo(std::move(*entry));
    ctx.error("Undo failed: " + error + ".");
    return arv::Result{arv::ResultCode::InternalError, "undo failed"};
  }
  state->history.push_redo(std::move(*entry));
  ctx.success("Undo complete: changed " + count_text(plan.changes.size()) + " blocks.");
  return arv::Result::ok();
}

arv::Result MasonPlugin::handle_redo(arv::CommandContext& ctx, const Args& args) {
  if (!args.empty()) {
    ctx.error("Usage: /mason redo.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  if (!check_custom_permission(ctx, ids::perm_redo) || !check_modify_permission(ctx)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  MasonPlayerState* state = state_for(ctx);
  if (state == nullptr) {
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }
  auto entry = state->history.take_redo();
  if (!entry.has_value()) {
    ctx.info("Nothing to redo.");
    return arv::Result::ok();
  }
  OperationPlan plan{entry->world, entry->changes, "redo " + entry->summary};
  std::string error;
  if (!apply_changes_sync(plan, error)) {
    state->history.push_redo(std::move(*entry));
    ctx.error("Redo failed: " + error + ".");
    return arv::Result{arv::ResultCode::InternalError, "redo failed"};
  }
  state->history.push_undo_without_clearing_redo(std::move(*entry));
  ctx.success("Redo complete: changed " + count_text(plan.changes.size()) + " blocks.");
  return arv::Result::ok();
}

} // namespace arv_mason
