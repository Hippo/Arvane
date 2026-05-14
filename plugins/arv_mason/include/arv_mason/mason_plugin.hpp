#pragma once

#include "arv_mason/blueprints/blueprint.hpp"
#include "arv_mason/config.hpp"
#include "arv_mason/history/history.hpp"
#include "arv_mason/selection/selection.hpp"

#include <arv/plugin.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arv_mason {

enum class PendingOperationKind {
  Set,
  Replace,
  Paste,
  BlueprintPaste,
};

struct PendingOperation {
  PendingOperationKind kind{PendingOperationKind::Set};
  std::vector<std::string> args{};
};

struct MasonPlayerState {
  bool persistent_loaded{};
  MasonSelection selection{};
  std::optional<MasonClipboard> clipboard{};
  MasonHistory history{};
  std::optional<PendingOperation> pending{};
};

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
    return plugin_.commands().register_executor(
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
  [[nodiscard]] arv::Result handle_select(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_set(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_replace(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_copy(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_paste(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_undo(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_redo(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_blueprint(arv::CommandContext& ctx, const Args& args, bool confirmed = false);
  [[nodiscard]] arv::Result handle_confirm(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_status(arv::CommandContext& ctx);

  [[nodiscard]] MasonPlayerState* state_for(arv::CommandContext& ctx);
  [[nodiscard]] std::optional<ArvPlayerID> require_player(arv::CommandContext& ctx) const;
  [[nodiscard]] std::optional<ArvWorldHandle> require_world(arv::CommandContext& ctx) const;
  [[nodiscard]] std::optional<ArvBlockPos> require_block_position(arv::CommandContext& ctx) const;

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
  void save_player_state(ArvPlayerID player, const MasonPlayerState& state) const;
  void load_player_state(ArvPlayerID player, MasonPlayerState& state) const;

  [[nodiscard]] std::vector<std::string> load_blueprint_index(ArvWorldHandle world) const;
  void save_blueprint_index(ArvWorldHandle world, const std::vector<std::string>& names) const;
  [[nodiscard]] bool save_blueprint_document(ArvWorldHandle world, std::string_view name, std::string_view abp_text) const;
  [[nodiscard]] std::optional<std::string> load_blueprint_document(ArvWorldHandle world, std::string_view name) const;
  [[nodiscard]] std::optional<MasonClipboard> clipboard_from_blueprint(
      arv::CommandContext& ctx,
      const Blueprint& blueprint) const;

  arv::Plugin plugin_;
  MasonConfig config_{};
  std::unordered_map<ArvPlayerID, MasonPlayerState> players_{};
};

} // namespace arv_mason
