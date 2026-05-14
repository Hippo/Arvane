#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace arv_mason {
namespace {

[[nodiscard]] std::string join_args(const MasonPlugin::Args& args, std::size_t first = 0U) {
  std::string out;
  for (std::size_t i = first; i < args.size(); ++i) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out.append(args[i]);
  }
  return out;
}

[[nodiscard]] std::vector<std::string> copy_args(const MasonPlugin::Args& args) {
  std::vector<std::string> out;
  out.reserve(args.size());
  for (std::string_view arg : args) {
    out.emplace_back(arg);
  }
  return out;
}

[[nodiscard]] MasonPlugin::Args view_args(const std::vector<std::string>& args) {
  MasonPlugin::Args out;
  out.reserve(args.size());
  for (const std::string& arg : args) {
    out.emplace_back(arg);
  }
  return out;
}

[[nodiscard]] std::string count_text(std::uint64_t count) {
  std::ostringstream out;
  out << count;
  return out.str();
}

[[nodiscard]] ArvBlockPos floor_block_pos(ArvVec3 position) {
  return {
      static_cast<std::int32_t>(std::floor(position.x)),
      static_cast<std::int32_t>(std::floor(position.y)),
      static_cast<std::int32_t>(std::floor(position.z)),
  };
}

[[nodiscard]] std::string operation_kind_name(PendingOperationKind kind) {
  switch (kind) {
    case PendingOperationKind::Set:
      return "set";
    case PendingOperationKind::Replace:
      return "replace";
    case PendingOperationKind::Paste:
      return "paste";
    case PendingOperationKind::BlueprintPaste:
      return "blueprint paste";
  }
  return "operation";
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

void add_candidate(std::vector<std::string>& candidates, std::string value) {
  if (!contains_string(candidates, value)) {
    candidates.push_back(std::move(value));
  }
}

} // namespace

MasonPlugin::MasonPlugin(arv::Plugin plugin) : plugin_(plugin) {
}

bool MasonPlugin::initialize() {
  config_ = load_config(plugin_.config(), plugin_.logger());
  if (arv::Result permissions = register_permissions(); !permissions) {
    plugin_.logger().error(std::string{"Mason permission registration failed: "} + permissions.message());
    return false;
  }
  if (arv::Result commands = register_commands(); !commands) {
    plugin_.logger().error(std::string{"Mason command registration failed: "} + commands.message());
    return false;
  }
  plugin_.logger().info("Mason plugin initialized.");
  return true;
}

arv::Result MasonPlugin::register_permissions() {
  struct Node {
    std::string_view id;
    std::string_view description;
  };
  const Node nodes[] = {
      {ids::perm_use, "Use Mason commands."},
      {ids::perm_select, "Set and inspect Mason selections."},
      {ids::perm_set, "Fill a Mason selection."},
      {ids::perm_replace, "Replace blocks in a Mason selection."},
      {ids::perm_copy, "Copy a Mason selection."},
      {ids::perm_paste, "Paste a Mason clipboard."},
      {ids::perm_undo, "Undo Mason operations."},
      {ids::perm_redo, "Redo Mason operations."},
      {ids::perm_blueprint_save, "Save Mason blueprints."},
      {ids::perm_blueprint_load, "Load Mason blueprints."},
      {ids::perm_blueprint_paste, "Paste Mason blueprints."},
      {ids::perm_blueprint_list, "List Mason blueprints."},
      {ids::perm_blueprint_info, "Inspect Mason blueprints."},
      {ids::perm_limit_bypass, "Bypass Mason operation limits."},
      {ids::perm_region_bypass, "Bypass region restrictions when server policy allows it."},
  };
  for (const Node& node : nodes) {
    arv::Result result = plugin_.permissions().register_permission_node({
        .node = node.id,
        .description = node.description,
        .default_value = arv::PermissionValue::Unset,
    });
    if (!result && result.code() != arv::ResultCode::DuplicateID) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result MasonPlugin::register_commands() {
  auto root = register_command(
      ids::exec_mason,
      "mason",
      ids::perm_use,
      "Mason world editing tools.",
      "/mason help",
      false,
      [this](arv::CommandContext& ctx) { return handle_mason(ctx); });
  if (!root) {
    return root;
  }

  struct CommandRegistration {
    std::string_view executor;
    std::string_view name;
    std::string_view permission;
    std::string_view description;
    std::string_view usage;
  };
  const CommandRegistration commands[] = {
      {ids::exec_select, "mselect", ids::perm_select, "Set or inspect a Mason selection.", "/mselect <a|b|clear|info>"},
      {ids::exec_set, "mset", ids::perm_set, "Fill the current Mason selection.", "/mset <block_state>"},
      {ids::exec_replace, "mreplace", ids::perm_replace, "Replace blocks in the current Mason selection.", "/mreplace <from> <to>"},
      {ids::exec_copy, "mcopy", ids::perm_copy, "Copy the current Mason selection.", "/mcopy"},
      {ids::exec_paste, "mpaste", ids::perm_paste, "Paste the current Mason clipboard.", "/mpaste [--air|--no-air]"},
      {ids::exec_undo, "mundo", ids::perm_undo, "Undo the previous Mason operation.", "/mundo"},
      {ids::exec_redo, "mredo", ids::perm_redo, "Redo the previous Mason operation.", "/mredo"},
      {ids::exec_blueprint, "mbp", ids::perm_use, "Manage Mason blueprints.", "/mbp <save|load|paste|list|info>"},
  };
  for (const CommandRegistration& command : commands) {
    auto handler = [this, name = command.name](arv::CommandContext& ctx) -> arv::Result {
      const Args args = ctx.args();
      if (name == "mselect") {
        return handle_select(ctx, args);
      }
      if (name == "mset") {
        return handle_set(ctx, args);
      }
      if (name == "mreplace") {
        return handle_replace(ctx, args);
      }
      if (name == "mcopy") {
        return handle_copy(ctx, args);
      }
      if (name == "mpaste") {
        return handle_paste(ctx, args);
      }
      if (name == "mundo") {
        return handle_undo(ctx, args);
      }
      if (name == "mredo") {
        return handle_redo(ctx, args);
      }
      if (name == "mbp") {
        return handle_blueprint(ctx, args);
      }
      return arv::Result{arv::ResultCode::InvalidState, "unknown Mason command"};
    };
    arv::Result result = register_command(
        command.executor,
        command.name,
        command.permission,
        command.description,
        command.usage,
        false,
        handler);
    if (!result) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result MasonPlugin::handle_mason(arv::CommandContext& ctx) {
  Args args = ctx.args();
  if (args.empty() || args[0] == "help") {
    ctx.info("Mason: /mason select <a|b|clear|info>, set, replace, copy, paste, undo, redo, blueprint, confirm.");
    ctx.info("Aliases: /mselect /mset /mreplace /mcopy /mpaste /mundo /mredo /mbp.");
    return arv::Result::ok();
  }
  const std::string_view subcommand = args[0];
  Args rest{args.begin() + 1, args.end()};
  if (subcommand == "status") {
    return handle_status(ctx);
  }
  if (subcommand == "select") {
    return handle_select(ctx, rest);
  }
  if (subcommand == "set") {
    return handle_set(ctx, rest);
  }
  if (subcommand == "replace") {
    return handle_replace(ctx, rest);
  }
  if (subcommand == "copy") {
    return handle_copy(ctx, rest);
  }
  if (subcommand == "paste") {
    return handle_paste(ctx, rest);
  }
  if (subcommand == "undo") {
    return handle_undo(ctx, rest);
  }
  if (subcommand == "redo") {
    return handle_redo(ctx, rest);
  }
  if (subcommand == "blueprint" || subcommand == "bp") {
    return handle_blueprint(ctx, rest);
  }
  if (subcommand == "confirm") {
    return handle_confirm(ctx, rest);
  }
  ctx.error("Unknown Mason subcommand. Try /mason help.");
  return arv::Result{arv::ResultCode::InvalidArgument, "unknown Mason subcommand"};
}

arv::Result MasonPlugin::handle_status(arv::CommandContext& ctx) {
  std::ostringstream out;
  out << "Mason 0.1.0 ready. Limits: selection "
      << config_.limits.max_selection_blocks
      << ", clipboard " << config_.limits.max_clipboard_blocks
      << ", blueprint " << config_.limits.max_blueprint_blocks << ".";
  ctx.info(out.str());
  return arv::Result::ok();
}

MasonPlayerState* MasonPlugin::state_for(arv::CommandContext& ctx) {
  const std::optional<ArvPlayerID> player = require_player(ctx);
  if (!player.has_value()) {
    ctx.error("This Mason command requires a player.");
    return nullptr;
  }
  MasonPlayerState& state = players_[*player];
  state.history.set_limit(static_cast<std::size_t>(config_.limits.max_history_entries));
  if (!state.persistent_loaded) {
    load_player_state(*player, state);
    state.persistent_loaded = true;
  }
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

std::optional<std::uint32_t> MasonPlugin::parse_block_variant(
    arv::CommandContext& ctx,
    std::string_view text) const {
  if (text.empty()) {
    ctx.error("Block state must not be empty.");
    return std::nullopt;
  }
  std::vector<std::string> candidates;
  const std::string raw{text};
  add_candidate(candidates, raw);
  if (raw.find(':') == std::string::npos) {
    add_candidate(candidates, "arvane:" + raw);
  }
  const std::vector<std::string> base = candidates;
  for (const std::string& candidate : base) {
    if (candidate.find('[') == std::string::npos) {
      add_candidate(candidates, candidate + "[]");
    }
  }
  for (const std::string& candidate : candidates) {
    auto resolved = plugin_.registries().find_block_variant(candidate);
    if (resolved) {
      return resolved.value;
    }
  }
  ctx.error("Unknown block state: " + raw + ".");
  return std::nullopt;
}

std::string MasonPlugin::stable_block_name(std::uint32_t variant) const {
  auto name = plugin_.registries().block_variant_name(variant);
  return name ? std::string{name.value} : std::string{"arvane:air[]"};
}

std::uint32_t MasonPlugin::air_variant() const {
  auto air = plugin_.registries().air_block_variant();
  return air ? air.value : 0U;
}

std::optional<std::vector<SnapshotBlock>> MasonPlugin::snapshot_selection(
    arv::CommandContext& ctx,
    const SelectionBounds& bounds) const {
  std::vector<SnapshotBlock> snapshot;
  snapshot.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
      bounds.block_count,
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))));
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
    snapshot.push_back({pos, block.value});
  });
  if (!ok) {
    ctx.error("Failed to read selection: " + error + ".");
    return std::nullopt;
  }
  return snapshot;
}

std::optional<std::vector<SnapshotBlock>> MasonPlugin::snapshot_clipboard_destination(
    arv::CommandContext& ctx,
    const MasonClipboard& clipboard,
    ArvWorldHandle world,
    ArvBlockPos paste_origin,
    bool paste_air) const {
  std::vector<SnapshotBlock> snapshot;
  const std::uint32_t air = air_variant();
  for (std::int32_t y = 0; y < clipboard.size.y; ++y) {
    for (std::int32_t z = 0; z < clipboard.size.z; ++z) {
      for (std::int32_t x = 0; x < clipboard.size.x; ++x) {
        const std::uint64_t index = clipboard_index(
            clipboard.size,
            static_cast<std::uint64_t>(x),
            static_cast<std::uint64_t>(y),
            static_cast<std::uint64_t>(z));
        if (index >= clipboard.blocks.size()) {
          continue;
        }
        const ClipboardBlock& block = clipboard.blocks[index];
        if (!paste_air && block.runtime_variant == air) {
          continue;
        }
        const ArvBlockPos source_pos{
            clipboard.source_min.x + x,
            clipboard.source_min.y + y,
            clipboard.source_min.z + z,
        };
        const ArvBlockPos dest = add_pos(paste_origin, sub_pos(source_pos, clipboard.origin));
        auto before = plugin_.world().get_block(world, dest);
        if (!before) {
          ctx.error("Failed to read paste destination: " + std::string{before.result.message()} + ".");
          return std::nullopt;
        }
        snapshot.push_back({dest, before.value});
      }
    }
  }
  return snapshot;
}

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
  SelectionBoundsResult bounds = normalized_bounds(state->selection);
  if (!bounds.ok) {
    ctx.error(bounds.message);
    return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
  }
  if (!require_limit_or_confirmation(
          ctx,
          *state,
          {PendingOperationKind::Set, copy_args(args)},
          bounds.bounds.block_count,
          config_.limits.max_selection_blocks,
          confirmed)) {
    return arv::Result::ok();
  }
  const std::optional<std::uint32_t> to = parse_block_variant(ctx, args[0]);
  if (!to.has_value()) {
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid block state"};
  }
  auto snapshot = snapshot_selection(ctx, bounds.bounds);
  if (!snapshot.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "snapshot failed"};
  }
  OperationPlan plan = build_set_plan_from_snapshot(bounds.bounds.world, *snapshot, *to);
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
  SelectionBoundsResult bounds = normalized_bounds(state->selection);
  if (!bounds.ok) {
    ctx.error(bounds.message);
    return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
  }
  if (!require_limit_or_confirmation(
          ctx,
          *state,
          {PendingOperationKind::Replace, copy_args(args)},
          bounds.bounds.block_count,
          config_.limits.max_selection_blocks,
          confirmed)) {
    return arv::Result::ok();
  }
  const std::optional<std::uint32_t> from = parse_block_variant(ctx, args[0]);
  const std::optional<std::uint32_t> to = parse_block_variant(ctx, args[1]);
  if (!from.has_value() || !to.has_value()) {
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid block state"};
  }
  auto snapshot = snapshot_selection(ctx, bounds.bounds);
  if (!snapshot.has_value()) {
    return arv::Result{arv::ResultCode::InternalError, "snapshot failed"};
  }
  OperationPlan plan = build_replace_plan_from_snapshot(
      bounds.bounds.world,
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
  SelectionBoundsResult bounds = normalized_bounds(state->selection);
  if (!bounds.ok) {
    ctx.error(bounds.message);
    return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
  }
  if (!has_limit_bypass(ctx) && bounds.bounds.block_count > config_.limits.max_clipboard_blocks) {
    ctx.error(
        "Clipboard exceeds limit: " + count_text(bounds.bounds.block_count) +
        " blocks. Limit: " + count_text(config_.limits.max_clipboard_blocks) + ".");
    return arv::Result{arv::ResultCode::InvalidArgument, "clipboard limit exceeded"};
  }
  const std::optional<ArvBlockPos> origin = require_block_position(ctx);
  if (!origin.has_value()) {
    ctx.error("Copy requires a player position.");
    return arv::Result{arv::ResultCode::InvalidState, "missing position"};
  }
  auto clipboard = copy_selection_to_clipboard(ctx, bounds.bounds, *origin);
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
    ctx.error("Clipboard is empty. Use /mason copy or /mason blueprint load first.");
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
    case PendingOperationKind::BlueprintPaste:
      return handle_blueprint(ctx, pending_args, true);
  }
  return arv::Result{arv::ResultCode::InvalidState, "unknown pending operation"};
}

void MasonPlugin::save_player_state(ArvPlayerID player, const MasonPlayerState& state) const {
  if (!config_.behavior.save_selection_per_player) {
    return;
  }
  auto document = plugin_.data().create_document(1);
  if (!document) {
    return;
  }
  const arv::DataDocument& doc = document.value;
  (void)doc.set_u64("schema_version", 1);
  (void)doc.set_bool("selection.has_world", state.selection.world.has_value());
  if (state.selection.world.has_value()) {
    (void)doc.set_u64("selection.world", *state.selection.world);
  }
  (void)doc.set_bool("selection.has_a", state.selection.a.has_value());
  if (state.selection.a.has_value()) {
    (void)doc.set_i64("selection.a.x", state.selection.a->x);
    (void)doc.set_i64("selection.a.y", state.selection.a->y);
    (void)doc.set_i64("selection.a.z", state.selection.a->z);
  }
  (void)doc.set_bool("selection.has_b", state.selection.b.has_value());
  if (state.selection.b.has_value()) {
    (void)doc.set_i64("selection.b.x", state.selection.b->x);
    (void)doc.set_i64("selection.b.y", state.selection.b->y);
    (void)doc.set_i64("selection.b.z", state.selection.b->z);
  }
  (void)plugin_.persistent_data().player(player).save(ids::player_state, doc);
}

void MasonPlugin::load_player_state(ArvPlayerID player, MasonPlayerState& state) const {
  if (!config_.behavior.save_selection_per_player) {
    return;
  }
  auto loaded = plugin_.persistent_data().player(player).load(ids::player_state);
  if (!loaded) {
    return;
  }
  const arv::DataDocument& doc = loaded.value;
  auto has_world = doc.bool_value("selection.has_world");
  if (has_world && has_world.value) {
    auto world = doc.u64_value("selection.world");
    if (world) {
      state.selection.world = world.value;
    }
  }
  auto read_pos = [&](std::string_view prefix) -> std::optional<ArvBlockPos> {
    auto x = doc.i64_value(std::string{prefix} + ".x");
    auto y = doc.i64_value(std::string{prefix} + ".y");
    auto z = doc.i64_value(std::string{prefix} + ".z");
    if (!x || !y || !z) {
      return std::nullopt;
    }
    return ArvBlockPos{
        static_cast<std::int32_t>(x.value),
        static_cast<std::int32_t>(y.value),
        static_cast<std::int32_t>(z.value),
    };
  };
  auto has_a = doc.bool_value("selection.has_a");
  if (has_a && has_a.value) {
    state.selection.a = read_pos("selection.a");
  }
  auto has_b = doc.bool_value("selection.has_b");
  if (has_b && has_b.value) {
    state.selection.b = read_pos("selection.b");
  }
}

std::vector<std::string> MasonPlugin::load_blueprint_index(ArvWorldHandle world) const {
  std::vector<std::string> names;
  auto loaded = plugin_.persistent_data().world(world).load(ids::blueprint_index);
  if (!loaded) {
    return names;
  }
  auto text = loaded.value.string_value("names");
  if (!text) {
    return names;
  }
  std::string_view value = text.value;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find('\n', start);
    std::string_view name = end == std::string_view::npos
                                ? value.substr(start)
                                : value.substr(start, end - start);
    if (!name.empty() && valid_blueprint_name(name)) {
      names.emplace_back(name);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1U;
  }
  return names;
}

void MasonPlugin::save_blueprint_index(ArvWorldHandle world, const std::vector<std::string>& names) const {
  auto document = plugin_.data().create_document(1);
  if (!document) {
    return;
  }
  std::string text;
  for (const std::string& name : names) {
    text.append(name);
    text.push_back('\n');
  }
  (void)document.value.set_string("names", text);
  (void)plugin_.persistent_data().world(world).save(ids::blueprint_index, document.value);
}

bool MasonPlugin::save_blueprint_document(
    ArvWorldHandle world,
    std::string_view name,
    std::string_view abp_text) const {
  auto document = plugin_.data().create_document(1);
  if (!document) {
    return false;
  }
  (void)document.value.set_string("format", blueprint_format_id);
  (void)document.value.set_string("name", name);
  (void)document.value.set_string("abp", abp_text);
  return static_cast<bool>(plugin_.persistent_data().world(world).save(
      blueprint_key(name),
      document.value));
}

std::optional<std::string> MasonPlugin::load_blueprint_document(
    ArvWorldHandle world,
    std::string_view name) const {
  auto loaded = plugin_.persistent_data().world(world).load(blueprint_key(name));
  if (!loaded) {
    return std::nullopt;
  }
  auto text = loaded.value.string_value("abp");
  if (!text) {
    return std::nullopt;
  }
  return std::string{text.value};
}

std::optional<MasonClipboard> MasonPlugin::clipboard_from_blueprint(
    arv::CommandContext& ctx,
    const Blueprint& blueprint) const {
  MasonClipboard clipboard;
  clipboard.size = blueprint.size;
  clipboard.source_min = {0, 0, 0};
  clipboard.origin = blueprint.origin;
  clipboard.blocks.reserve(blueprint.blocks.size());

  std::vector<std::uint32_t> runtime_palette;
  runtime_palette.reserve(blueprint.palette.size());
  for (const std::string& stable_id : blueprint.palette) {
    auto resolved = plugin_.registries().find_block_variant(stable_id);
    if (!resolved) {
      ctx.error("Blueprint references an unknown block state: " + stable_id + ".");
      return std::nullopt;
    }
    runtime_palette.push_back(resolved.value);
  }
  for (const std::uint32_t state : blueprint.blocks) {
    if (state >= runtime_palette.size()) {
      ctx.error("Blueprint block references an invalid palette index.");
      return std::nullopt;
    }
    clipboard.blocks.push_back({runtime_palette[state], blueprint.palette[state]});
  }
  return clipboard;
}

arv::Result MasonPlugin::handle_blueprint(
    arv::CommandContext& ctx,
    const Args& args,
    bool confirmed) {
  if (args.empty()) {
    ctx.error("Usage: /mason blueprint <save|load|paste|list|info>.");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }
  const std::string_view action = args[0];
  const std::optional<ArvWorldHandle> world = require_world(ctx);
  if (!world.has_value()) {
    ctx.error("Blueprint commands require a player world.");
    return arv::Result{arv::ResultCode::InvalidState, "world required"};
  }

  if (action == "list") {
    if (!check_custom_permission(ctx, ids::perm_blueprint_list)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    const std::vector<std::string> names = load_blueprint_index(*world);
    if (names.empty()) {
      ctx.info("No blueprints saved.");
      return arv::Result::ok();
    }
    std::ostringstream out;
    out << "Blueprints:";
    for (const std::string& name : names) {
      out << ' ' << name;
    }
    ctx.info(out.str());
    return arv::Result::ok();
  }

  if (args.size() < 2U) {
    ctx.error("Blueprint name is required.");
    return arv::Result{arv::ResultCode::InvalidArgument, "blueprint name required"};
  }
  const std::string name{args[1]};
  if (!valid_blueprint_name(name)) {
    ctx.error("Invalid blueprint name. Use only letters, numbers, underscore, dash, and dot.");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid blueprint name"};
  }

  if (action == "save") {
    if (!check_custom_permission(ctx, ids::perm_blueprint_save)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    MasonPlayerState* state = state_for(ctx);
    if (state == nullptr) {
      return arv::Result{arv::ResultCode::InvalidState, "player required"};
    }
    SelectionBoundsResult bounds = normalized_bounds(state->selection);
    if (!bounds.ok) {
      ctx.error(bounds.message);
      return arv::Result{arv::ResultCode::InvalidState, "selection incomplete"};
    }
    if (!has_limit_bypass(ctx) && bounds.bounds.block_count > config_.limits.max_blueprint_blocks) {
      ctx.error(
          "Blueprint exceeds limit: " + count_text(bounds.bounds.block_count) +
          " blocks. Limit: " + count_text(config_.limits.max_blueprint_blocks) + ".");
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint limit exceeded"};
    }
    const std::optional<ArvBlockPos> origin = require_block_position(ctx);
    if (!origin.has_value()) {
      ctx.error("Blueprint save requires a player position.");
      return arv::Result{arv::ResultCode::InvalidState, "position required"};
    }
    auto clipboard = copy_selection_to_clipboard(ctx, bounds.bounds, *origin);
    if (!clipboard.has_value()) {
      return arv::Result{arv::ResultCode::InternalError, "copy failed"};
    }
    Blueprint blueprint = blueprint_from_clipboard(name, *clipboard);
    BlueprintCodecResult encoded = encode_blueprint(blueprint);
    if (!encoded.ok) {
      ctx.error(encoded.message);
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint encode failed"};
    }
    if (!save_blueprint_document(*world, name, encoded.text)) {
      ctx.error("Failed to save blueprint.");
      return arv::Result{arv::ResultCode::InternalError, "blueprint save failed"};
    }
    std::vector<std::string> names = load_blueprint_index(*world);
    if (!contains_string(names, name)) {
      names.push_back(name);
      std::sort(names.begin(), names.end());
      save_blueprint_index(*world, names);
    }
    ctx.success("Blueprint saved: " + name + ".abp.");
    return arv::Result::ok();
  }

  if (action == "load" || action == "info" || action == "paste") {
    const std::string permission = action == "load"
                                       ? std::string{ids::perm_blueprint_load}
                                   : action == "info"
                                       ? std::string{ids::perm_blueprint_info}
                                       : std::string{ids::perm_blueprint_paste};
    if (!check_custom_permission(ctx, permission)) {
      return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
    }
    auto document = load_blueprint_document(*world, name);
    if (!document.has_value()) {
      ctx.error("Blueprint not found: " + name + ".abp.");
      return arv::Result{arv::ResultCode::NotFound, "blueprint not found"};
    }
    BlueprintCodecResult decoded = decode_blueprint(*document);
    if (!decoded.ok) {
      ctx.error("Blueprint is corrupt: " + decoded.message);
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint corrupt"};
    }
    const std::uint64_t blocks = blueprint_volume(decoded.blueprint.size);
    if (!has_limit_bypass(ctx) && blocks > config_.limits.max_blueprint_blocks) {
      ctx.error(
          "Blueprint exceeds limit: " + count_text(blocks) +
          " blocks. Limit: " + count_text(config_.limits.max_blueprint_blocks) + ".");
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint limit exceeded"};
    }
    if (action == "info") {
      std::ostringstream out;
      out << "Blueprint " << name << ".abp: "
          << decoded.blueprint.size.x << " x " << decoded.blueprint.size.y
          << " x " << decoded.blueprint.size.z << " = " << blocks
          << " blocks, palette " << decoded.blueprint.palette.size() << ".";
      ctx.info(out.str());
      return arv::Result::ok();
    }
    auto clipboard = clipboard_from_blueprint(ctx, decoded.blueprint);
    if (!clipboard.has_value()) {
      return arv::Result{arv::ResultCode::InvalidArgument, "blueprint resolve failed"};
    }
    MasonPlayerState* state = state_for(ctx);
    if (state == nullptr) {
      return arv::Result{arv::ResultCode::InvalidState, "player required"};
    }
    state->clipboard = std::move(*clipboard);
    if (action == "load") {
      ctx.success("Blueprint loaded: " + name + ".abp.");
      return arv::Result::ok();
    }
    Args paste_args;
    for (std::size_t i = 2U; i < args.size(); ++i) {
      paste_args.push_back(args[i]);
    }
    if (!require_limit_or_confirmation(
            ctx,
            *state,
            {PendingOperationKind::BlueprintPaste, copy_args(args)},
            blocks,
            config_.limits.max_blueprint_blocks,
            confirmed)) {
      return arv::Result::ok();
    }
    return handle_paste(ctx, paste_args, true);
  }

  ctx.error("Usage: /mason blueprint <save|load|paste|list|info>.");
  return arv::Result{arv::ResultCode::InvalidArgument, "unknown blueprint action"};
}

} // namespace arv_mason
