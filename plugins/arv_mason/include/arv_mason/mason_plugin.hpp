#pragma once

#include "arv_mason/config.hpp"
#include "arv_mason/history/history.hpp"
#include "arv_mason/selection/selection.hpp"

#include <arv/plugin.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arv_mason {

enum class PendingOperationKind {
  Set,
  Replace,
  Paste,
};

struct PendingOperation {
  PendingOperationKind kind{PendingOperationKind::Set};
  std::vector<std::string> args{};
};

struct MasonPlayerState {
  std::optional<MasonClipboard> clipboard{};
  MasonHistory history{};
  std::optional<PendingOperation> pending{};
};

using MasonMessageArgs =
    std::initializer_list<std::pair<std::string_view, std::string_view>>;

class MasonPlugin {
public:
  using Args = std::vector<std::string_view>;

  explicit MasonPlugin(arv::Plugin plugin);

  [[nodiscard]] bool initialize();

private:
  [[nodiscard]] arv::Result register_permissions();
  [[nodiscard]] arv::Result register_commands();
  [[nodiscard]] arv::Result register_command(
      std::string_view executor_id,
      std::string_view name,
      std::string_view permission,
      std::string_view description,
      std::string_view usage,
      bool public_to_players,
      auto handler) const {
    return plugin_.commands().register_command(
        {
            .executor_id = executor_id,
            .name = name,
            .domain = arv::CommandDomain::ServerBound,
            .permission = permission,
            .description = description,
            .usage = usage,
            .public_to_players = public_to_players,
        },
        handler);
  }

  [[nodiscard]] arv::Result handle_mason(arv::CommandContext& ctx);
  [[nodiscard]] arv::Result handle_marker(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_set(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_replace(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_copy(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_paste(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_undo(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_redo(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_confirm(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_status(arv::CommandContext& ctx);

  [[nodiscard]] std::string message_source(
      std::string_view key,
      MasonMessageArgs args = {}) const;
  void reply(
      arv::CommandContext& ctx,
      arv::CommandMessageKind kind,
      std::string_view key,
      MasonMessageArgs args = {}) const;
  void info(
      arv::CommandContext& ctx,
      std::string_view key,
      MasonMessageArgs args = {}) const;
  void success(
      arv::CommandContext& ctx,
      std::string_view key,
      MasonMessageArgs args = {}) const;
  void warn(
      arv::CommandContext& ctx,
      std::string_view key,
      MasonMessageArgs args = {}) const;
  void error(
      arv::CommandContext& ctx,
      std::string_view key,
      MasonMessageArgs args = {}) const;

  [[nodiscard]] MasonPlayerState* state_for(arv::CommandContext& ctx);
  [[nodiscard]] std::optional<ArvPlayerID> require_player(arv::CommandContext& ctx) const;
  [[nodiscard]] std::optional<ArvWorldHandle> require_world(arv::CommandContext& ctx) const;
  [[nodiscard]] std::optional<ArvBlockPos> require_block_position(arv::CommandContext& ctx) const;
  [[nodiscard]] std::optional<SelectionBounds> current_selection_bounds(
      arv::CommandContext& ctx) const;

  [[nodiscard]] bool check_custom_permission(
      arv::CommandContext& ctx,
      std::string_view permission) const;
  [[nodiscard]] bool check_modify_permission(arv::CommandContext& ctx) const;
  [[nodiscard]] bool has_limit_bypass(arv::CommandContext& ctx) const;
  [[nodiscard]] bool require_limit_or_confirmation(
      arv::CommandContext& ctx,
      MasonPlayerState& state,
      PendingOperation pending,
      std::uint64_t count,
      std::uint64_t limit,
      bool confirmed);

  [[nodiscard]] std::optional<std::uint32_t> parse_block_variant(
      arv::CommandContext& ctx,
      std::string_view text) const;
  [[nodiscard]] std::string stable_block_name(std::uint32_t variant) const;
  [[nodiscard]] std::uint32_t air_variant() const;

  [[nodiscard]] std::optional<std::vector<SnapshotBlock>> snapshot_selection(
      arv::CommandContext& ctx,
      const SelectionBounds& bounds) const;
  [[nodiscard]] std::optional<std::vector<SnapshotBlock>> snapshot_clipboard_destination(
      arv::CommandContext& ctx,
      const MasonClipboard& clipboard,
      ArvWorldHandle world,
      ArvBlockPos paste_origin,
      bool paste_air) const;
  [[nodiscard]] std::optional<MasonClipboard> copy_selection_to_clipboard(
      arv::CommandContext& ctx,
      const SelectionBounds& bounds,
      ArvBlockPos origin) const;

  [[nodiscard]] arv::Result execute_plan(
      arv::CommandContext& ctx,
      ArvPlayerID player,
      MasonPlayerState& state,
      OperationPlan plan,
      std::string complete_message);
  [[nodiscard]] bool apply_changes_sync(const OperationPlan& plan, std::string& out_error) const;
  void schedule_batched_plan(
      ArvPlayerID player,
      OperationPlan plan,
      std::string history_summary);

  void push_history(ArvPlayerID player, MasonPlayerState& state, const OperationPlan& plan);

  arv::Plugin plugin_;
  MasonConfig config_{};
  MasonMessages messages_{};
  std::unordered_map<ArvPlayerID, MasonPlayerState> players_{};
};

} // namespace arv_mason
