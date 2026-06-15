#include "arv_perms/arv_perms_plugin.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace arv_perms {
namespace {

constexpr std::string_view PermAdmin = "arvane.permissions.admin";
constexpr std::string_view PermGroupCreate = "arvane.permissions.group.create";
constexpr std::string_view PermGroupDelete = "arvane.permissions.group.delete";
constexpr std::string_view PermGroupEdit = "arvane.permissions.group.edit";
constexpr std::string_view PermGroupInfo = "arvane.permissions.group.info";
constexpr std::string_view PermUserEdit = "arvane.permissions.user.edit";
constexpr std::string_view PermUserInfo = "arvane.permissions.user.info";
constexpr std::string_view PermCheck = "arvane.permissions.check";
constexpr std::string_view PermDefaultEdit = "arvane.permissions.default.edit";
constexpr std::string_view PermReload = "arvane.permissions.reload";
constexpr std::string_view PermSave = "arvane.permissions.save";
constexpr std::string_view PermValidate = "arvane.permissions.validate";
constexpr std::string_view PermCommand = "arvane.command.perms";

constexpr std::string_view ConfigFile = "config.yaml";
constexpr std::string_view GroupsFile = "groups.yaml";
constexpr std::string_view PlayerPermissionsFile = "permissions.json";

constexpr std::string_view PermissionNodes[] = {
    PermCommand,
    PermAdmin,
    PermGroupCreate,
    PermGroupDelete,
    PermGroupEdit,
    PermGroupInfo,
    PermUserEdit,
    PermUserInfo,
    PermCheck,
    PermDefaultEdit,
    PermReload,
    PermSave,
    PermValidate,
};

[[nodiscard]] std::optional<arv::PermissionValue> parse_permission_value(
    std::string_view text) {
  return permission_value_from_string(text);
}

[[nodiscard]] std::string join_groups(const std::vector<GroupId>& groups) {
  std::ostringstream out;
  for (std::size_t index = 0; index < groups.size(); ++index) {
    if (index != 0U) {
      out << ", ";
    }
    out << groups[index];
  }
  return out.str();
}

template<typename T>
void erase_value(std::vector<T>& values, const T& value) {
  values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

} // namespace

ArvPermsPlugin::ArvPermsPlugin(arv::Plugin plugin) : plugin_(plugin) {
}

bool ArvPermsPlugin::initialize() {
  if (!load_store()) {
    return false;
  }
  if (arv::Result permissions = register_permissions(); !permissions) {
    plugin_.logger().error(
        std::string{"arv_perms permission registration failed: "} +
        permissions.message());
    return false;
  }
  if (arv::Result commands = register_commands(); !commands) {
    plugin_.logger().error(
        std::string{"arv_perms command registration failed: "} +
        commands.message());
    return false;
  }
  (void)plugin_.events().subscribe(
      "arvane:event/server_start",
      [this](arv::Event) {
        upsert_runtime_groups();
        apply_all_online_players();
        schedule_reapply();
      });
  plugin_.logger().info("arv_perms initialized.");
  return true;
}

bool ArvPermsPlugin::load_store() {
  auto config_document = plugin_.config().load_file(ConfigFile);
  auto groups_document = plugin_.config().load_file(GroupsFile);
  const bool needs_default_config = !config_document;
  const bool needs_default_groups = !groups_document;

  StoreLoadResult loaded = load_store_from_documents(
      config_document ? &config_document.value : nullptr,
      groups_document ? &groups_document.value : nullptr);
  for (const std::string& diagnostic : loaded.diagnostics) {
    plugin_.logger().error(diagnostic);
  }
  if (!loaded.ok) {
    return false;
  }
  store_ = std::move(loaded.store);
  std::string error;
  if (!save_config_files(error)) {
    const std::string action =
        needs_default_config || needs_default_groups ? "default config" : "config";
    plugin_.logger().warn("arv_perms could not write " + action + ": " + error);
  }
  return true;
}

bool ArvPermsPlugin::save_store(std::string& out_error) {
  if (!save_config_files(out_error)) {
    return false;
  }
  for (const auto& [profile_id, record] : store_.players) {
    (void)profile_id;
    if (!save_player_record(record, out_error)) {
      return false;
    }
  }
  store_.dirty = false;
  return true;
}

bool ArvPermsPlugin::save_config_files(std::string& out_error) {
  auto config_document = plugin_.data().create_document(1);
  if (!config_document) {
    out_error = config_document.result.message();
    return false;
  }
  if (arv::Result encoded = encode_config_document(store_.config, config_document.value);
      !encoded) {
    out_error = encoded.message();
    return false;
  }
  if (arv::Result saved = plugin_.config().save_file(ConfigFile, config_document.value);
      !saved) {
    out_error = saved.message();
    return false;
  }

  auto groups_document = plugin_.data().create_document(1);
  if (!groups_document) {
    out_error = groups_document.result.message();
    return false;
  }
  if (arv::Result encoded = encode_groups_document(store_.groups, groups_document.value);
      !encoded) {
    out_error = encoded.message();
    return false;
  }
  if (arv::Result saved = plugin_.config().save_file(GroupsFile, groups_document.value);
      !saved) {
    out_error = saved.message();
    return false;
  }
  return true;
}

bool ArvPermsPlugin::save_player_record(
    const PlayerPermissionRecord& record,
    std::string& out_error) {
  auto document = plugin_.data().create_document(1);
  if (!document) {
    out_error = document.result.message();
    return false;
  }
  if (arv::Result encoded = encode_player_document(record, document.value); !encoded) {
    out_error = encoded.message();
    return false;
  }
  if (arv::Result saved = plugin_.config().save_player_data_file(
          record.player_id,
          PlayerPermissionsFile,
          document.value);
      !saved) {
    out_error = saved.message();
    return false;
  }
  return true;
}

bool ArvPermsPlugin::load_player_if_present(std::string_view profile_id) {
  if (store_.players.contains(std::string{profile_id})) {
    return true;
  }
  auto loaded = plugin_.config().load_player_data_file(profile_id, PlayerPermissionsFile);
  if (!loaded) {
    return false;
  }
  std::vector<std::string> diagnostics;
  auto record = load_player_record_from_document(profile_id, loaded.value, diagnostics);
  for (const std::string& diagnostic : diagnostics) {
    plugin_.logger().error(diagnostic);
  }
  if (!record.has_value() || !diagnostics.empty()) {
    return false;
  }
  store_.players[record->player_id] = std::move(record.value());
  return true;
}

PlayerPermissionRecord& ArvPermsPlugin::ensure_player_record(
    std::string profile_id,
    std::optional<std::string> last_known_name) {
  (void)load_player_if_present(profile_id);
  return store_.ensure_player(std::move(profile_id), std::move(last_known_name));
}

arv::Result ArvPermsPlugin::register_permissions() {
  for (std::string_view node : PermissionNodes) {
    arv::Result result = plugin_.permissions().register_permission_node({
        .node = node,
        .description = "Manage arv_perms permissions.",
        .default_value = arv::PermissionValue::Unset,
    });
    if (!result && result.code() != arv::ResultCode::DuplicateID) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::register_commands() {
  return plugin_.commands().register_command(
      {
          .executor_id = "arv_perms:perm",
          .name = "perm",
          .domain = arv::CommandDomain::ServerBound,
          .permission = std::string{PermCommand},
          .description = "Manage server permission groups and player assignments.",
          .usage = "/perm <group|user|default|save|reload|validate>",
          .public_to_players = false,
      },
      [this](arv::CommandContext& ctx) { return handle_perm(ctx); });
}

bool ArvPermsPlugin::check_command_permission(
    arv::CommandContext& ctx,
    std::string_view permission) const {
  if (ctx.source_kind() == arv::CommandSourceKind::ServerConsole) {
    return true;
  }
  const std::optional<ArvPlayerID> player = ctx.player();
  if (!player.has_value()) {
    ctx.error("No permission");
    return false;
  }
  auto admin = plugin_.permissions().check_custom(player.value(), PermAdmin);
  if (admin && admin.value == arv::PermissionValue::Allow) {
    return true;
  }
  auto checked = plugin_.permissions().check_custom(player.value(), permission);
  if (checked && checked.value == arv::PermissionValue::Allow) {
    return true;
  }
  ctx.error("No permission");
  return false;
}

arv::Result ArvPermsPlugin::handle_perm(arv::CommandContext& ctx) {
  const Args args = ctx.args();
  if (args.empty() || args[0] == "help") {
    ctx.info("/perm group, user, default, save, reload, validate");
    return arv::Result::ok();
  }
  const std::string_view section = args[0];
  const Args rest{args.begin() + 1, args.end()};
  if (section == "group") {
    return handle_group(ctx, rest);
  }
  if (section == "user") {
    return handle_user(ctx, rest);
  }
  if (section == "default") {
    return handle_default(ctx, rest);
  }
  if (section == "save") {
    return handle_save(ctx);
  }
  if (section == "reload") {
    return handle_reload(ctx);
  }
  if (section == "validate") {
    return handle_validate(ctx);
  }
  ctx.error("Usage: /perm <group|user|default|save|reload|validate>");
  return arv::Result{arv::ResultCode::InvalidArgument, "unknown perm command"};
}

arv::Result ArvPermsPlugin::handle_group(arv::CommandContext& ctx, const Args& args) {
  if (args.empty()) {
    ctx.error("Usage: /perm group <create|delete|list|info|group>");
    return arv::Result{arv::ResultCode::InvalidArgument, "missing group command"};
  }
  if (args[0] == "create") {
    if (!check_command_permission(ctx, PermGroupCreate)) {
      return arv::Result::ok();
    }
    if (args.size() != 2U || !is_valid_group_id(args[1])) {
      ctx.error("Usage: /perm group create <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "invalid group id"};
    }
    const std::string id{args[1]};
    if (store_.groups.contains(id)) {
      ctx.error("Group already exists: " + id);
      return arv::Result::ok();
    }
    PermissionGroupRecord group;
    group.id = id;
    store_.groups[id] = std::move(group);
    upsert_runtime_groups();
    mark_dirty_and_autosave(ctx);
    ctx.success("Created group " + id);
    return arv::Result::ok();
  }
  if (args[0] == "delete") {
    if (!check_command_permission(ctx, PermGroupDelete)) {
      return arv::Result::ok();
    }
    if (args.size() != 2U) {
      ctx.error("Usage: /perm group delete <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "missing group id"};
    }
    const std::string id{args[1]};
    for (const auto& [group_id, group] : store_.groups) {
      if (std::find(group.inherits.begin(), group.inherits.end(), id) != group.inherits.end()) {
        ctx.error("Group is inherited by " + group_id + "; remove inheritance first.");
        return arv::Result::ok();
      }
    }
    for (const auto& [profile_id, player] : store_.players) {
      if (std::find(player.groups.begin(), player.groups.end(), id) != player.groups.end()) {
        ctx.error("Group is assigned to player " + profile_id + "; remove assignment first.");
        return arv::Result::ok();
      }
    }
    if (store_.groups.erase(id) == 0U) {
      ctx.error("Missing group: " + id);
      return arv::Result::ok();
    }
    (void)plugin_.permissions().remove_registered_group(id);
    mark_dirty_and_autosave(ctx);
    ctx.success("Deleted group " + id);
    return arv::Result::ok();
  }
  if (args[0] == "list") {
    if (!check_command_permission(ctx, PermGroupInfo)) {
      return arv::Result::ok();
    }
    std::ostringstream out;
    out << "Groups:";
    for (const auto& [id, group] : store_.groups) {
      out << " " << id << "(" << group.priority << ")";
    }
    ctx.info(out.str());
    return arv::Result::ok();
  }
  if (args[0] == "info") {
    if (!check_command_permission(ctx, PermGroupInfo)) {
      return arv::Result::ok();
    }
    if (args.size() != 2U) {
      ctx.error("Usage: /perm group info <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "missing group id"};
    }
    const auto found = store_.groups.find(std::string{args[1]});
    if (found == store_.groups.end()) {
      ctx.error("Missing group: " + std::string{args[1]});
      return arv::Result::ok();
    }
    ctx.info(
        found->first + ": priority " + std::to_string(found->second.priority) +
        ", inherits [" + join_groups(found->second.inherits) + "]");
    return arv::Result::ok();
  }

  if (!check_command_permission(ctx, PermGroupEdit)) {
    return arv::Result::ok();
  }
  if (args.size() < 2U) {
    ctx.error("Usage: /perm group <group> <priority|inherit|set|unset|core>");
    return arv::Result{arv::ResultCode::InvalidArgument, "missing group action"};
  }
  const std::string group_id{args[0]};
  auto found = store_.groups.find(group_id);
  if (found == store_.groups.end()) {
    ctx.error("Missing group: " + group_id);
    return arv::Result::ok();
  }
  PermissionGroupRecord& group = found->second;
  const std::string_view action = args[1];
  if (action == "priority" && args.size() == 3U) {
    try {
      group.priority = std::stoi(std::string{args[2]});
    } catch (...) {
      ctx.error("Priority must be an integer.");
      return arv::Result::ok();
    }
  } else if (action == "inherit" && args.size() >= 3U) {
    if (args[2] == "list") {
      ctx.info(group.id + " inherits [" + join_groups(group.inherits) + "]");
      return arv::Result::ok();
    }
    if (args.size() != 4U || (args[2] != "add" && args[2] != "remove")) {
      ctx.error("Usage: /perm group <group> inherit add|remove <parent>");
      return arv::Result{arv::ResultCode::InvalidArgument, "invalid inherit command"};
    }
    const std::string parent{args[3]};
    if (args[2] == "add") {
      if (!store_.groups.contains(parent)) {
        ctx.error("Missing parent group: " + parent);
        return arv::Result::ok();
      }
      if (std::find(group.inherits.begin(), group.inherits.end(), parent) == group.inherits.end()) {
        group.inherits.push_back(parent);
      }
    } else {
      erase_value(group.inherits, parent);
    }
  } else if (action == "set" && args.size() == 4U) {
    const std::string node{args[2]};
    const std::optional<arv::PermissionValue> value = parse_permission_value(args[3]);
    if (!value.has_value() || !is_valid_permission_node(node)) {
      ctx.error("Usage: /perm group <group> set <node> allow|deny|unset");
      return arv::Result::ok();
    }
    if (is_permission_unset(value.value())) {
      group.permissions.erase(node);
    } else {
      group.permissions[node] = value.value();
    }
  } else if (action == "unset" && args.size() == 3U) {
    group.permissions.erase(std::string{args[2]});
  } else if (action == "core" && args.size() == 5U && args[2] == "set") {
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(args[3]);
    const std::optional<arv::PermissionValue> value = parse_permission_value(args[4]);
    if (!permission.has_value() || !value.has_value()) {
      ctx.error("Usage: /perm group <group> core set <core_permission> allow|deny|unset");
      return arv::Result::ok();
    }
    (void)set_core_permission(group.core, permission.value(), value.value());
  } else if (action == "core" && args.size() == 4U && args[2] == "unset") {
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(args[3]);
    if (!permission.has_value()) {
      ctx.error("Unknown core permission.");
      return arv::Result::ok();
    }
    (void)set_core_permission(group.core, permission.value(), arv::PermissionValue::Unset);
  } else {
    ctx.error("Usage: /perm group <group> <priority|inherit|set|unset|core>");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid group command"};
  }

  const ValidationResult validation = store_.validate();
  if (!validation.ok()) {
    ctx.error(validation.diagnostics.front().message);
    return arv::Result::ok();
  }
  upsert_runtime_groups();
  apply_all_online_players();
  mark_dirty_and_autosave(ctx);
  ctx.success("Updated group " + group.id);
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::handle_user(arv::CommandContext& ctx, const Args& args) {
  if (args.size() < 2U || !is_valid_profile_id(args[0])) {
    ctx.error("Usage: /perm user <PlayerProfileID> <group|set|unset|core|info|check|check-core>");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid player"};
  }
  const std::string profile_id{args[0]};
  const std::string_view action = args[1];
  const bool read_only = action == "info" || action == "check" || action == "check-core";
  if (!check_command_permission(ctx, read_only ? PermUserInfo : PermUserEdit)) {
    return arv::Result::ok();
  }

  PlayerPermissionRecord& record = ensure_player_record(profile_id);
  if (action == "group") {
    if (args.size() < 3U) {
      ctx.error("Usage: /perm user <player> group add|remove|set|list <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "invalid user group command"};
    }
    if (args[2] == "list") {
      ctx.info(profile_id + " groups [" + join_groups(record.groups) + "]");
      return arv::Result::ok();
    }
    if (args.size() != 4U) {
      ctx.error("Usage: /perm user <player> group add|remove|set <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "invalid user group command"};
    }
    const std::string group{args[3]};
    if (!store_.groups.contains(group)) {
      ctx.error("Missing group: " + group);
      return arv::Result::ok();
    }
    if (args[2] == "add") {
      if (std::find(record.groups.begin(), record.groups.end(), group) == record.groups.end()) {
        record.groups.push_back(group);
      }
    } else if (args[2] == "remove") {
      erase_value(record.groups, group);
    } else if (args[2] == "set") {
      record.groups = {group};
    } else {
      ctx.error("Usage: /perm user <player> group add|remove|set|list <group>");
      return arv::Result{arv::ResultCode::InvalidArgument, "invalid user group command"};
    }
  } else if (action == "set" && args.size() == 4U) {
    const std::string node{args[2]};
    const std::optional<arv::PermissionValue> value = parse_permission_value(args[3]);
    if (!value.has_value() || !is_valid_permission_node(node)) {
      ctx.error("Usage: /perm user <player> set <node> allow|deny|unset");
      return arv::Result::ok();
    }
    if (is_permission_unset(value.value())) {
      record.permission_overrides.erase(node);
    } else {
      record.permission_overrides[node] = value.value();
    }
  } else if (action == "unset" && args.size() == 3U) {
    record.permission_overrides.erase(std::string{args[2]});
  } else if (action == "core" && args.size() == 5U && args[2] == "set") {
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(args[3]);
    const std::optional<arv::PermissionValue> value = parse_permission_value(args[4]);
    if (!permission.has_value() || !value.has_value()) {
      ctx.error("Usage: /perm user <player> core set <core_permission> allow|deny|unset");
      return arv::Result::ok();
    }
    (void)set_core_permission(record.core_overrides, permission.value(), value.value());
  } else if (action == "core" && args.size() == 4U && args[2] == "unset") {
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(args[3]);
    if (!permission.has_value()) {
      ctx.error("Unknown core permission.");
      return arv::Result::ok();
    }
    (void)set_core_permission(record.core_overrides, permission.value(), arv::PermissionValue::Unset);
  } else if (action == "info") {
    ctx.info(profile_id + " groups [" + join_groups(record.groups) + "]");
    return arv::Result::ok();
  } else if (action == "check" && args.size() == 3U) {
    if (!check_command_permission(ctx, PermCheck)) {
      return arv::Result::ok();
    }
    const PermissionTrace trace = store_.trace_custom(record, args[2]);
    ctx.info(std::string{args[2]} + ": " + permission_value_name(trace.player_overrides_result));
    for (const PermissionTraceLine& line : trace.group_lines) {
      if (!is_permission_unset(line.value)) {
        ctx.info(
            "  " + line.group_id + " priority " + std::to_string(line.priority) +
            " " + permission_value_name(line.value) +
            (line.matched_node.empty() ? "" : " via " + line.matched_node));
      }
    }
    if (!is_permission_unset(trace.direct_override)) {
      ctx.info(std::string{"  direct "} + permission_value_name(trace.direct_override));
    }
    return arv::Result::ok();
  } else if (action == "check-core" && args.size() == 3U) {
    if (!check_command_permission(ctx, PermCheck)) {
      return arv::Result::ok();
    }
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(args[2]);
    if (!permission.has_value()) {
      ctx.error("Unknown core permission.");
      return arv::Result::ok();
    }
    const ResolvedPlayerOverrideLayer resolved = store_.resolve_player(record);
    ctx.info(
        std::string{args[2]} + ": " +
        permission_value_name(core_permission_value(resolved.core, permission.value())));
    return arv::Result::ok();
  } else {
    ctx.error("Usage: /perm user <PlayerProfileID> <group|set|unset|core|info|check|check-core>");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid user command"};
  }

  const ValidationResult validation = store_.validate();
  if (!validation.ok()) {
    ctx.error(validation.diagnostics.front().message);
    return arv::Result::ok();
  }
  apply_player_if_online(profile_id);
  mark_dirty_and_autosave(ctx);
  ctx.success("Updated player " + profile_id);
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::handle_default(arv::CommandContext& ctx, const Args& args) {
  if (!check_command_permission(ctx, PermDefaultEdit)) {
    return arv::Result::ok();
  }
  if (args.size() == 1U && args[0] == "list") {
    ctx.info("Default groups [" + join_groups(store_.config.default_groups) + "]");
    return arv::Result::ok();
  }
  if (args.size() != 2U || (args[0] != "add" && args[0] != "remove")) {
    ctx.error("Usage: /perm default add|remove <group> OR /perm default list");
    return arv::Result{arv::ResultCode::InvalidArgument, "invalid default command"};
  }
  const std::string group{args[1]};
  if (args[0] == "add") {
    if (!store_.groups.contains(group)) {
      ctx.error("Missing group: " + group);
      return arv::Result::ok();
    }
    if (std::find(store_.config.default_groups.begin(), store_.config.default_groups.end(), group) ==
        store_.config.default_groups.end()) {
      store_.config.default_groups.push_back(group);
    }
  } else {
    erase_value(store_.config.default_groups, group);
  }
  mark_dirty_and_autosave(ctx);
  ctx.success("Updated default groups.");
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::handle_save(arv::CommandContext& ctx) {
  if (!check_command_permission(ctx, PermSave)) {
    return arv::Result::ok();
  }
  std::string error;
  if (!save_store(error)) {
    ctx.error("Save failed: " + error);
    return arv::Result::ok();
  }
  ctx.success("Permissions saved.");
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::handle_reload(arv::CommandContext& ctx) {
  if (!check_command_permission(ctx, PermReload)) {
    return arv::Result::ok();
  }
  std::vector<std::string> known_players;
  known_players.reserve(store_.players.size());
  for (const auto& [profile_id, player] : store_.players) {
    (void)player;
    known_players.push_back(profile_id);
  }
  if (!load_store()) {
    ctx.error("Reload failed; live permissions unchanged.");
    return arv::Result::ok();
  }
  for (const std::string& profile_id : known_players) {
    (void)load_player_if_present(profile_id);
  }
  upsert_runtime_groups();
  apply_all_online_players();
  ctx.success("Permissions reloaded.");
  return arv::Result::ok();
}

arv::Result ArvPermsPlugin::handle_validate(arv::CommandContext& ctx) {
  if (!check_command_permission(ctx, PermValidate)) {
    return arv::Result::ok();
  }
  const ValidationResult validation = store_.validate();
  if (validation.ok()) {
    ctx.success("Permission configuration is valid.");
    return arv::Result::ok();
  }
  for (const ValidationDiagnostic& diagnostic : validation.diagnostics) {
    ctx.error(diagnostic.message);
  }
  return arv::Result::ok();
}

void ArvPermsPlugin::mark_dirty_and_autosave(arv::CommandContext& ctx) {
  store_.dirty = true;
  if (!store_.config.auto_save) {
    return;
  }
  std::string error;
  if (!save_store(error)) {
    ctx.error("Auto-save failed: " + error);
  }
}

std::vector<ArvCustomPermissionEntry> ArvPermsPlugin::custom_entries(
    const CustomPermissionMap& permissions,
    std::vector<std::string>& stable_nodes) const {
  std::vector<ArvCustomPermissionEntry> entries;
  entries.reserve(permissions.size());
  stable_nodes.reserve(permissions.size());
  for (const auto& [node, value] : permissions) {
    if (is_permission_unset(value)) {
      continue;
    }
    stable_nodes.push_back(node);
    entries.push_back({
        arv::view(stable_nodes.back()),
        static_cast<ArvPermissionValue>(value),
    });
  }
  return entries;
}

void ArvPermsPlugin::upsert_runtime_groups() {
  for (const auto& [id, group] : store_.groups) {
    std::vector<std::string_view> inherits;
    inherits.reserve(group.inherits.size());
    for (const GroupId& parent : group.inherits) {
      inherits.push_back(parent);
    }
    std::vector<std::string> stable_nodes;
    std::vector<ArvCustomPermissionEntry> entries =
        custom_entries(group.permissions, stable_nodes);
    arv::Result result = plugin_.permissions().upsert_group({
        .id = group.id,
        .priority = static_cast<std::int32_t>(group.priority),
        .inherits = inherits,
        .core = group.core,
        .custom_permissions = entries,
    });
    if (!result) {
      plugin_.logger().error("arv_perms failed to upsert group " + id + ": " + result.message());
    }
  }
}

void ArvPermsPlugin::apply_all_online_players() {
  upsert_runtime_groups();
  for (const ArvPlayerID player : plugin_.players().online()) {
    const std::string profile_id = plugin_.players().profile_id(player);
    if (profile_id.empty()) {
      continue;
    }
    PlayerPermissionRecord& record = ensure_player_record(
        profile_id,
        plugin_.players().display_name(player));
    apply_player_record(player, record);
  }
  if (store_.dirty && store_.config.auto_save) {
    std::string error;
    if (!save_store(error)) {
      plugin_.logger().error("arv_perms auto-save failed: " + error);
    }
  }
}

void ArvPermsPlugin::apply_player_if_online(std::string_view profile_id) {
  const std::optional<ArvPlayerID> player = plugin_.players().find_by_profile_id(profile_id);
  if (!player.has_value()) {
    return;
  }
  PlayerPermissionRecord& record = ensure_player_record(std::string{profile_id});
  apply_player_record(player.value(), record);
}

void ArvPermsPlugin::apply_player_record(
    ArvPlayerID player,
    PlayerPermissionRecord& record) {
  std::vector<std::string_view> groups;
  groups.reserve(record.groups.size());
  for (const GroupId& group : record.groups) {
    groups.push_back(group);
  }
  (void)plugin_.permissions().set_groups(player, groups);

  std::vector<std::string> stable_nodes;
  std::vector<ArvCustomPermissionEntry> entries =
      custom_entries(record.permission_overrides, stable_nodes);
  (void)plugin_.permissions().set_overrides(
      player,
      record.core_overrides,
      entries);
}

void ArvPermsPlugin::schedule_reapply() {
  reapply_job_ = arv::Job{[this]() { apply_all_online_players(); }};
  arv::Result result = plugin_.scheduler().sync_repeat_tick(1, 20, reapply_job_);
  if (!result) {
    plugin_.logger().warn(
        std::string{"arv_perms could not schedule online permission reapply: "} +
        result.message());
  }
}

} // namespace arv_perms
