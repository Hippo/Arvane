#include "arv_perms/permission_store.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

namespace arv_perms {
namespace {

struct CorePermissionInfo {
  std::string_view name;
  arv::CorePermission permission;
};

constexpr CorePermissionInfo CorePermissions[] = {
    {"can_take_damage", arv::CorePermission::CanTakeDamage},
    {"can_modify_world", arv::CorePermission::CanModifyWorld},
    {"can_interact", arv::CorePermission::CanInteract},
    {"can_open_inventory", arv::CorePermission::CanOpenInventory},
    {"has_creative_inventory", arv::CorePermission::HasCreativeInventory},
    {"can_fly", arv::CorePermission::CanFly},
    {"has_collision", arv::CorePermission::HasCollision},
    {"is_visible_to_others", arv::CorePermission::IsVisibleToOthers},
};

[[nodiscard]] ArvPermissionValue to_abi(arv::PermissionValue value) noexcept {
  return static_cast<ArvPermissionValue>(value);
}

[[nodiscard]] arv::PermissionValue from_abi(ArvPermissionValue value) noexcept {
  return static_cast<arv::PermissionValue>(value);
}

[[nodiscard]] std::string lower_ascii(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char ch : text) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

[[nodiscard]] bool wildcard_matches(
    std::string_view pattern,
    std::string_view node) noexcept {
  if (pattern == "*") {
    return true;
  }
  if (pattern.size() < 2 || pattern.substr(pattern.size() - 2) != ".*") {
    return false;
  }
  const std::string_view prefix = pattern.substr(0, pattern.size() - 1);
  return node.size() >= prefix.size() && node.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] arv::PermissionValue custom_permission_value(
    const CustomPermissionMap& permissions,
    std::string_view node,
    std::string* out_match = nullptr) {
  const auto exact = permissions.find(std::string{node});
  if (exact != permissions.end()) {
    if (out_match != nullptr) {
      *out_match = exact->first;
    }
    return exact->second;
  }

  arv::PermissionValue best = arv::PermissionValue::Unset;
  std::size_t best_specificity = 0U;
  std::string best_node;
  for (const auto& [pattern, value] : permissions) {
    if (!wildcard_matches(pattern, node)) {
      continue;
    }
    const std::size_t specificity = pattern.size();
    if (specificity > best_specificity ||
        (specificity == best_specificity && is_permission_deny(value))) {
      best = value;
      best_specificity = specificity;
      best_node = pattern;
    }
  }
  if (out_match != nullptr) {
    *out_match = std::move(best_node);
  }
  return best;
}

void merge_core(
    arv::CorePermissionSet& target,
    const arv::CorePermissionSet& source,
    bool equal_priority) {
  for (const CorePermissionInfo& info : CorePermissions) {
    const arv::PermissionValue incoming =
        core_permission_value(source, info.permission);
    if (is_permission_unset(incoming)) {
      continue;
    }
    const arv::PermissionValue current =
        core_permission_value(target, info.permission);
    if (equal_priority && is_permission_deny(current) && is_permission_allow(incoming)) {
      continue;
    }
    (void)set_core_permission(target, info.permission, incoming);
  }
}

void merge_custom(
    CustomPermissionMap& target,
    const CustomPermissionMap& source,
    bool equal_priority) {
  for (const auto& [node, incoming] : source) {
    if (is_permission_unset(incoming)) {
      continue;
    }
    const auto found = target.find(node);
    if (equal_priority && found != target.end() &&
        is_permission_deny(found->second) && is_permission_allow(incoming)) {
      continue;
    }
    target[node] = incoming;
  }
}

void append_diag(ValidationResult& result, std::string message) {
  result.diagnostics.push_back({std::move(message)});
}

[[nodiscard]] bool expand_group(
    const PermissionStore& store,
    std::string_view group_id,
    std::vector<GroupId>& out,
    std::vector<GroupId>& stack) {
  if (std::find(stack.begin(), stack.end(), group_id) != stack.end()) {
    return false;
  }
  const auto found = store.groups.find(std::string{group_id});
  if (found == store.groups.end()) {
    return false;
  }
  stack.push_back(found->first);
  for (const GroupId& parent : found->second.inherits) {
    if (!expand_group(store, parent, out, stack)) {
      stack.pop_back();
      return false;
    }
  }
  stack.pop_back();
  if (std::find(out.begin(), out.end(), found->first) == out.end()) {
    out.push_back(found->first);
  }
  return true;
}

[[nodiscard]] std::vector<GroupId> effective_groups_for(
    const PermissionStore& store,
    const PlayerPermissionRecord& player) {
  std::vector<GroupId> expanded;
  for (const GroupId& group : player.groups) {
    std::vector<GroupId> stack;
    (void)expand_group(store, group, expanded, stack);
  }
  std::sort(expanded.begin(), expanded.end(), [&](const GroupId& lhs, const GroupId& rhs) {
    const PermissionGroupRecord& left = store.groups.at(lhs);
    const PermissionGroupRecord& right = store.groups.at(rhs);
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return lhs < rhs;
  });
  return expanded;
}

[[nodiscard]] std::string child_path(std::string_view parent, std::string_view child) {
  if (parent.empty()) {
    return std::string{child};
  }
  std::string path{parent};
  path.push_back('.');
  path.append(child);
  return path;
}

[[nodiscard]] std::vector<GroupId> document_group_list(
    const arv::DataDocument& document,
    std::string_view path) {
  std::vector<GroupId> groups;
  const arv::DataKind kind = document.kind(path);
  if (kind == arv::DataKind::Invalid || kind == arv::DataKind::Null) {
    return groups;
  }
  if (kind == arv::DataKind::String) {
    groups.emplace_back(document.string_value(path, ""));
    return groups;
  }
  if (kind == arv::DataKind::Array) {
    const std::size_t count = document.size(path);
    groups.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
      auto value = document.array_string_value(path, index);
      if (value) {
        groups.emplace_back(value.value());
      }
    }
  }
  return groups;
}

void parse_core_entries(
    arv::CorePermissionSet& core,
    const arv::DataDocument& document,
    std::string_view path,
    std::vector<std::string>& diagnostics,
    std::string_view owner) {
  if (document.kind(path) == arv::DataKind::Invalid ||
      document.kind(path) == arv::DataKind::Null) {
    return;
  }
  if (document.kind(path) != arv::DataKind::Array) {
    diagnostics.push_back(std::string{owner} + ": core must be a list");
    return;
  }
  const std::size_t count = document.size(path);
  for (std::size_t index = 0U; index < count; ++index) {
    const std::string base = child_path(path, std::to_string(index));
    const std::string name = std::string{document.string_value(
        child_path(base, "name"),
        "")};
    const std::optional<arv::CorePermission> permission =
        core_permission_from_name(name);
    if (!permission.has_value()) {
      diagnostics.push_back(std::string{owner} + ": unknown core permission '" + name + "'");
      continue;
    }
    const std::optional<arv::PermissionValue> value =
        permission_value_from_string(document.string_value(child_path(base, "value"), ""));
    if (!value.has_value()) {
      diagnostics.push_back(std::string{owner} + ": invalid TriState for '" + name + "'");
      continue;
    }
    (void)set_core_permission(core, permission.value(), value.value());
  }
}

void parse_custom_entries(
    CustomPermissionMap& permissions,
    const arv::DataDocument& document,
    std::string_view path,
    std::vector<std::string>& diagnostics,
    std::string_view owner) {
  if (document.kind(path) == arv::DataKind::Invalid ||
      document.kind(path) == arv::DataKind::Null) {
    return;
  }
  if (document.kind(path) != arv::DataKind::Array) {
    diagnostics.push_back(std::string{owner} + ": permissions must be a list");
    return;
  }
  const std::size_t count = document.size(path);
  for (std::size_t index = 0U; index < count; ++index) {
    const std::string base = child_path(path, std::to_string(index));
    const std::string node = std::string{document.string_value(
        child_path(base, "node"),
        "")};
    if (!is_valid_permission_node(node)) {
      diagnostics.push_back(std::string{owner} + ": invalid permission node '" + node + "'");
      continue;
    }
    const std::optional<arv::PermissionValue> value =
        permission_value_from_string(document.string_value(child_path(base, "value"), ""));
    if (!value.has_value()) {
      diagnostics.push_back(std::string{owner} + ": invalid TriState for '" + node + "'");
      continue;
    }
    if (!is_permission_unset(value.value())) {
      permissions[node] = value.value();
    }
  }
}

[[nodiscard]] arv::Result push_string_list(
    const arv::DataDocument& document,
    std::string_view path,
    const std::vector<std::string>& values) {
  if (arv::Result result = document.set_array(path); !result) {
    return result;
  }
  for (const std::string& value : values) {
    if (arv::Result result = document.array_push_string(path, value); !result) {
      return result;
    }
  }
  return arv::Result::ok();
}

[[nodiscard]] arv::Result encode_core_entries(
    const arv::DataDocument& document,
    std::string_view path,
    const arv::CorePermissionSet& core) {
  if (arv::Result result = document.set_array(path); !result) {
    return result;
  }
  std::size_t index = 0U;
  for (const CorePermissionInfo& info : CorePermissions) {
    const arv::PermissionValue value = core_permission_value(core, info.permission);
    if (is_permission_unset(value)) {
      continue;
    }
    if (arv::Result result = document.array_push_object(path); !result) {
      return result;
    }
    const std::string base = child_path(path, std::to_string(index++));
    if (arv::Result result = document.set_string(child_path(base, "name"), info.name);
        !result) {
      return result;
    }
    if (arv::Result result = document.set_string(child_path(base, "value"), permission_value_name(value));
        !result) {
      return result;
    }
  }
  return arv::Result::ok();
}

[[nodiscard]] arv::Result encode_custom_entries(
    const arv::DataDocument& document,
    std::string_view path,
    const CustomPermissionMap& permissions) {
  if (arv::Result result = document.set_array(path); !result) {
    return result;
  }
  std::size_t index = 0U;
  for (const auto& [node, value] : permissions) {
    if (is_permission_unset(value)) {
      continue;
    }
    if (arv::Result result = document.array_push_object(path); !result) {
      return result;
    }
    const std::string base = child_path(path, std::to_string(index++));
    if (arv::Result result = document.set_string(child_path(base, "node"), node);
        !result) {
      return result;
    }
    if (arv::Result result = document.set_string(child_path(base, "value"), permission_value_name(value));
        !result) {
      return result;
    }
  }
  return arv::Result::ok();
}

} // namespace

std::optional<arv::PermissionValue> permission_value_from_string(
    std::string_view text) {
  const std::string lowered = lower_ascii(text);
  if (lowered == "allow" || lowered == "allowed" || lowered == "true" ||
      lowered == "yes") {
    return arv::PermissionValue::Allow;
  }
  if (lowered == "deny" || lowered == "denied" || lowered == "false" ||
      lowered == "no") {
    return arv::PermissionValue::Deny;
  }
  if (lowered == "unset" || lowered == "none" || lowered == "null") {
    return arv::PermissionValue::Unset;
  }
  return std::nullopt;
}

const char* permission_value_name(arv::PermissionValue value) noexcept {
  switch (value) {
    case arv::PermissionValue::Allow:
      return "ALLOW";
    case arv::PermissionValue::Deny:
      return "DENY";
    case arv::PermissionValue::Unset:
      return "UNSET";
  }
  return "UNSET";
}

bool is_permission_allow(arv::PermissionValue value) noexcept {
  return value == arv::PermissionValue::Allow;
}

bool is_permission_deny(arv::PermissionValue value) noexcept {
  return value == arv::PermissionValue::Deny;
}

bool is_permission_unset(arv::PermissionValue value) noexcept {
  return value == arv::PermissionValue::Unset;
}

std::optional<arv::CorePermission> core_permission_from_name(
    std::string_view name) noexcept {
  for (const CorePermissionInfo& info : CorePermissions) {
    if (info.name == name) {
      return info.permission;
    }
  }
  return std::nullopt;
}

std::string_view core_permission_name(arv::CorePermission permission) noexcept {
  for (const CorePermissionInfo& info : CorePermissions) {
    if (info.permission == permission) {
      return info.name;
    }
  }
  return {};
}

bool set_core_permission(
    arv::CorePermissionSet& set,
    arv::CorePermission permission,
    arv::PermissionValue value) noexcept {
  switch (permission) {
    case arv::CorePermission::CanTakeDamage:
      set.can_take_damage = to_abi(value);
      return true;
    case arv::CorePermission::CanModifyWorld:
      set.can_modify_world = to_abi(value);
      return true;
    case arv::CorePermission::CanInteract:
      set.can_interact = to_abi(value);
      return true;
    case arv::CorePermission::CanOpenInventory:
      set.can_open_inventory = to_abi(value);
      return true;
    case arv::CorePermission::HasCreativeInventory:
      set.has_creative_inventory = to_abi(value);
      return true;
    case arv::CorePermission::CanFly:
      set.can_fly = to_abi(value);
      return true;
    case arv::CorePermission::HasCollision:
      set.has_collision = to_abi(value);
      return true;
    case arv::CorePermission::IsVisibleToOthers:
      set.is_visible_to_others = to_abi(value);
      return true;
  }
  return false;
}

arv::PermissionValue core_permission_value(
    const arv::CorePermissionSet& set,
    arv::CorePermission permission) noexcept {
  switch (permission) {
    case arv::CorePermission::CanTakeDamage:
      return from_abi(set.can_take_damage);
    case arv::CorePermission::CanModifyWorld:
      return from_abi(set.can_modify_world);
    case arv::CorePermission::CanInteract:
      return from_abi(set.can_interact);
    case arv::CorePermission::CanOpenInventory:
      return from_abi(set.can_open_inventory);
    case arv::CorePermission::HasCreativeInventory:
      return from_abi(set.has_creative_inventory);
    case arv::CorePermission::CanFly:
      return from_abi(set.can_fly);
    case arv::CorePermission::HasCollision:
      return from_abi(set.has_collision);
    case arv::CorePermission::IsVisibleToOthers:
      return from_abi(set.is_visible_to_others);
  }
  return arv::PermissionValue::Unset;
}

std::vector<std::string_view> core_permission_names() {
  std::vector<std::string_view> names;
  names.reserve(std::size(CorePermissions));
  for (const CorePermissionInfo& info : CorePermissions) {
    names.push_back(info.name);
  }
  return names;
}

bool is_valid_group_id(std::string_view text) noexcept {
  if (text.empty()) {
    return false;
  }
  for (const char ch : text) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  return true;
}

bool is_valid_profile_id(std::string_view text) noexcept {
  if (text.size() != 66U || text[0] != '0' || text[1] != 'x') {
    return false;
  }
  for (std::size_t index = 2; index < text.size(); ++index) {
    const char ch = text[index];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
      return false;
    }
  }
  return true;
}

bool is_valid_permission_node(std::string_view node) noexcept {
  if (node.empty() || node.front() == '.' || node.back() == '.') {
    return false;
  }
  bool previous_dot = false;
  for (const char ch : node) {
    const bool valid =
        (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_' ||
        ch == '-' ||
        ch == '.' ||
        ch == '*';
    if (!valid) {
      return false;
    }
    if (ch == '.' && previous_dot) {
      return false;
    }
    previous_dot = ch == '.';
  }
  if (node == "*") {
    return true;
  }
  const std::size_t wildcard = node.find('*');
  return wildcard == std::string_view::npos ||
         (wildcard + 1U == node.size() && wildcard > 0U && node[wildcard - 1U] == '.');
}

PlayerPermissionRecord& PermissionStore::ensure_player(
    std::string profile_id,
    std::optional<std::string> last_known_name) {
  auto [iter, inserted] = players.try_emplace(profile_id);
  PlayerPermissionRecord& record = iter->second;
  if (inserted) {
    record.player_id = std::move(profile_id);
    record.groups = config.default_groups;
    dirty = true;
  }
  if (last_known_name.has_value() && config.track_last_known_names &&
      record.last_known_name != last_known_name) {
    record.last_known_name = std::move(last_known_name);
    dirty = true;
  }
  return record;
}

const PlayerPermissionRecord* PermissionStore::find_player(
    std::string_view profile_id) const noexcept {
  const auto found = players.find(std::string{profile_id});
  return found == players.end() ? nullptr : &found->second;
}

ValidationResult PermissionStore::validate() const {
  ValidationResult result;
  for (const auto& [id, group] : groups) {
    if (!is_valid_group_id(id)) {
      append_diag(result, "groups.yaml: group id is invalid: " + id);
    }
    std::set<GroupId> inherited;
    for (const GroupId& parent : group.inherits) {
      if (!groups.contains(parent)) {
        append_diag(result, "groups.yaml: group '" + id + "' inherits missing group '" + parent + "'");
      }
      if (!inherited.insert(parent).second) {
        append_diag(result, "groups.yaml: group '" + id + "' has duplicate inherit '" + parent + "'");
      }
    }
  }

  for (const auto& [id, group] : groups) {
    std::vector<GroupId> stack;
    std::vector<GroupId> ignored;
    if (!expand_group(*this, group.id, ignored, stack)) {
      append_diag(result, "groups.yaml: circular inheritance detected at group '" + id + "'");
    }
  }

  for (const GroupId& group : config.default_groups) {
    if (!groups.contains(group)) {
      append_diag(result, "config.yaml: default group references missing group '" + group + "'");
    }
  }

  for (const auto& [profile_id, player] : players) {
    if (!is_valid_profile_id(profile_id)) {
      append_diag(result, "player data: invalid PlayerProfileID '" + profile_id + "'");
    }
    std::set<GroupId> assigned;
    for (const GroupId& group : player.groups) {
      if (!groups.contains(group)) {
        append_diag(result, "player data: player '" + profile_id + "' references missing group '" + group + "'");
      }
      if (!assigned.insert(group).second) {
        append_diag(result, "player data: player '" + profile_id + "' has duplicate group '" + group + "'");
      }
    }
  }
  return result;
}

ResolvedPlayerOverrideLayer PermissionStore::resolve_player(
    const PlayerPermissionRecord& player) const {
  ResolvedPlayerOverrideLayer resolved;
  resolved.effective_groups = effective_groups_for(*this, player);
  std::optional<int> previous_priority;
  for (const GroupId& id : resolved.effective_groups) {
    const PermissionGroupRecord& group = groups.at(id);
    const bool equal_priority =
        previous_priority.has_value() && previous_priority.value() == group.priority;
    merge_core(resolved.core, group.core, equal_priority);
    merge_custom(resolved.permissions, group.permissions, equal_priority);
    previous_priority = group.priority;
  }
  merge_core(resolved.core, player.core_overrides, false);
  merge_custom(resolved.permissions, player.permission_overrides, false);
  return resolved;
}

PermissionTrace PermissionStore::trace_custom(
    const PlayerPermissionRecord& player,
    std::string_view node) const {
  PermissionTrace trace;
  trace.node = std::string{node};
  trace.assigned_groups = player.groups;
  trace.effective_groups = effective_groups_for(*this, player);
  for (const GroupId& id : trace.effective_groups) {
    const PermissionGroupRecord& group = groups.at(id);
    std::string matched;
    const arv::PermissionValue value =
        custom_permission_value(group.permissions, node, &matched);
    trace.group_lines.push_back({group.id, group.priority, value, std::move(matched)});
  }
  trace.direct_override = custom_permission_value(
      player.permission_overrides,
      node);
  trace.player_overrides_result = custom_permission_value(
      resolve_player(player).permissions,
      node);
  return trace;
}

PermissionStore default_store() {
  PermissionStore store;

  PermissionGroupRecord player;
  player.id = "player";
  player.priority = 0;
  (void)set_core_permission(
      player.core,
      arv::CorePermission::CanInteract,
      arv::PermissionValue::Allow);
  (void)set_core_permission(
      player.core,
      arv::CorePermission::CanOpenInventory,
      arv::PermissionValue::Allow);
  player.permissions["arvane.command.help"] = arv::PermissionValue::Allow;
  player.permissions["arvane.chat.send"] = arv::PermissionValue::Allow;
  store.groups[player.id] = std::move(player);

  PermissionGroupRecord owner;
  owner.id = "owner";
  owner.priority = 1000;
  owner.inherits = {"player"};
  owner.permissions["arvane.*"] = arv::PermissionValue::Allow;
  owner.permissions["permissions.*"] = arv::PermissionValue::Allow;
  store.groups[owner.id] = std::move(owner);

  store.dirty = false;
  return store;
}

StoreLoadResult load_store_from_documents(
    const arv::DataDocument* config_document,
    const arv::DataDocument* groups_document) {
  StoreLoadResult result;
  PermissionStore store = default_store();

  if (config_document != nullptr && config_document->valid()) {
    store.config.default_groups = document_group_list(*config_document, "default_groups");
    if (store.config.default_groups.empty()) {
      store.config.default_groups.push_back("player");
    }
    if (config_document->kind("operator_group") == arv::DataKind::String) {
      const std::string value = std::string{
          config_document->string_value("operator_group", "")};
      store.config.operator_group = value.empty()
                                        ? std::optional<GroupId>{}
                                        : std::optional<GroupId>{value};
    }
    store.config.auto_save = config_document->bool_value("auto_save", true);
    store.config.save_interval_seconds =
        static_cast<int>(config_document->i64_value("save_interval_seconds", 60));
    store.config.track_last_known_names =
        config_document->bool_value("track_last_known_names", true);
  }

  if (groups_document != nullptr && groups_document->valid()) {
    if (groups_document->kind("groups") != arv::DataKind::Array) {
      result.diagnostics.push_back("groups.yaml: groups must be a list");
    } else {
      store.groups.clear();
      const std::size_t count = groups_document->size("groups");
      for (std::size_t index = 0U; index < count; ++index) {
        const std::string base = child_path("groups", std::to_string(index));
        PermissionGroupRecord group;
        group.id = std::string{groups_document->string_value(child_path(base, "id"), "")};
        if (group.id.empty()) {
          result.diagnostics.push_back(
              "groups.yaml: groups[" + std::to_string(index) + "] missing id");
          continue;
        }
        group.priority = static_cast<int>(
            groups_document->i64_value(child_path(base, "priority"), 0));
        group.inherits = document_group_list(*groups_document, child_path(base, "inherits"));
        parse_core_entries(
            group.core,
            *groups_document,
            child_path(base, "core"),
            result.diagnostics,
            "groups.yaml: group '" + group.id + "'");
        parse_custom_entries(
            group.permissions,
            *groups_document,
            child_path(base, "permissions"),
            result.diagnostics,
            "groups.yaml: group '" + group.id + "'");
        store.groups[group.id] = std::move(group);
      }
    }
  }

  const ValidationResult validation = store.validate();
  for (const ValidationDiagnostic& diagnostic : validation.diagnostics) {
    result.diagnostics.push_back(diagnostic.message);
  }
  result.ok = result.diagnostics.empty();
  result.store = std::move(store);
  result.store.dirty = false;
  return result;
}

std::optional<PlayerPermissionRecord> load_player_record_from_document(
    std::string_view profile_id,
    const arv::DataDocument& document,
    std::vector<std::string>& diagnostics) {
  if (!is_valid_profile_id(profile_id)) {
    diagnostics.push_back("player data: invalid PlayerProfileID '" + std::string{profile_id} + "'");
    return std::nullopt;
  }
  PlayerPermissionRecord player;
  player.player_id = std::string{profile_id};
  if (document.kind("player_id") == arv::DataKind::String) {
    const std::string stored_id = std::string{document.string_value("player_id", "")};
    if (!stored_id.empty() && stored_id != profile_id) {
      diagnostics.push_back(
          "player data: stored PlayerProfileID does not match file owner '" +
          std::string{profile_id} + "'");
    }
  }
  if (document.kind("last_known_name") == arv::DataKind::String) {
    player.last_known_name = std::string{document.string_value("last_known_name", "")};
  }
  player.groups = document_group_list(document, "groups");
  parse_core_entries(
      player.core_overrides,
      document,
      "core_overrides",
      diagnostics,
      "player data: player '" + player.player_id + "'");
  parse_custom_entries(
      player.permission_overrides,
      document,
      "permission_overrides",
      diagnostics,
      "player data: player '" + player.player_id + "'");
  return player;
}

arv::Result encode_config_document(
    const ArvPermsConfig& config,
    const arv::DataDocument& document) {
  if (arv::Result result = push_string_list(
          document,
          "default_groups",
          config.default_groups);
      !result) {
    return result;
  }
  if (config.operator_group.has_value()) {
    if (arv::Result result = document.set_string(
            "operator_group",
            config.operator_group.value());
        !result) {
      return result;
    }
  } else if (arv::Result result = document.set_string("operator_group", ""); !result) {
    return result;
  }
  if (arv::Result result = document.set_bool("auto_save", config.auto_save); !result) {
    return result;
  }
  if (arv::Result result = document.set_i64(
          "save_interval_seconds",
          config.save_interval_seconds);
      !result) {
    return result;
  }
  return document.set_bool(
      "track_last_known_names",
      config.track_last_known_names);
}

arv::Result encode_groups_document(
    const std::map<GroupId, PermissionGroupRecord>& groups,
    const arv::DataDocument& document) {
  if (arv::Result result = document.set_array("groups"); !result) {
    return result;
  }
  std::size_t index = 0U;
  for (const auto& [id, group] : groups) {
    (void)id;
    if (arv::Result result = document.array_push_object("groups"); !result) {
      return result;
    }
    const std::string base = child_path("groups", std::to_string(index++));
    if (arv::Result result = document.set_string(child_path(base, "id"), group.id);
        !result) {
      return result;
    }
    if (arv::Result result = document.set_i64(child_path(base, "priority"), group.priority);
        !result) {
      return result;
    }
    if (arv::Result result = push_string_list(
            document,
            child_path(base, "inherits"),
            group.inherits);
        !result) {
      return result;
    }
    if (arv::Result result = encode_core_entries(
            document,
            child_path(base, "core"),
            group.core);
        !result) {
      return result;
    }
    if (arv::Result result = encode_custom_entries(
            document,
            child_path(base, "permissions"),
            group.permissions);
        !result) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result encode_player_document(
    const PlayerPermissionRecord& player,
    const arv::DataDocument& document) {
  if (arv::Result result = document.set_string("player_id", player.player_id); !result) {
    return result;
  }
  if (player.last_known_name.has_value()) {
    if (arv::Result result = document.set_string(
            "last_known_name",
            player.last_known_name.value());
        !result) {
      return result;
    }
  }
  if (arv::Result result = push_string_list(document, "groups", player.groups);
      !result) {
    return result;
  }
  if (arv::Result result = encode_core_entries(
          document,
          "core_overrides",
          player.core_overrides);
      !result) {
    return result;
  }
  return encode_custom_entries(
      document,
      "permission_overrides",
      player.permission_overrides);
}

} // namespace arv_perms
