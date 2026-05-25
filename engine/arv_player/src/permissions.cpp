#include "arv/player/permissions.hpp"

#include "arv/core/string.hpp"

#include <algorithm>

namespace arv::player {
namespace {

enum class CorePermissionField : u8 {
  CanTakeDamage,
  CanModifyWorld,
  CanInteract,
  CanOpenInventory,
  HasCreativeInventory,
  CanFly,
  HasCollision,
  IsVisibleToOthers,
};

constexpr CorePermissionField CorePermissionFields[] = {
    CorePermissionField::CanTakeDamage,
    CorePermissionField::CanModifyWorld,
    CorePermissionField::CanInteract,
    CorePermissionField::CanOpenInventory,
    CorePermissionField::HasCreativeInventory,
    CorePermissionField::CanFly,
    CorePermissionField::HasCollision,
    CorePermissionField::IsVisibleToOthers,
};

[[nodiscard]] TriState core_value(
    const CorePermissionSet& permissions,
    CorePermissionField field) noexcept {
  switch (field) {
    case CorePermissionField::CanTakeDamage:
      return permissions.can_take_damage;
    case CorePermissionField::CanModifyWorld:
      return permissions.can_modify_world;
    case CorePermissionField::CanInteract:
      return permissions.can_interact;
    case CorePermissionField::CanOpenInventory:
      return permissions.can_open_inventory;
    case CorePermissionField::HasCreativeInventory:
      return permissions.has_creative_inventory;
    case CorePermissionField::CanFly:
      return permissions.can_fly;
    case CorePermissionField::HasCollision:
      return permissions.has_collision;
    case CorePermissionField::IsVisibleToOthers:
      return permissions.is_visible_to_others;
  }
  return TriState::Unset;
}

template <typename Permissions, typename Value>
void set_core_value(
    Permissions& permissions,
    CorePermissionField field,
    Value value) noexcept {
  switch (field) {
    case CorePermissionField::CanTakeDamage:
      permissions.can_take_damage = value;
      break;
    case CorePermissionField::CanModifyWorld:
      permissions.can_modify_world = value;
      break;
    case CorePermissionField::CanInteract:
      permissions.can_interact = value;
      break;
    case CorePermissionField::CanOpenInventory:
      permissions.can_open_inventory = value;
      break;
    case CorePermissionField::HasCreativeInventory:
      permissions.has_creative_inventory = value;
      break;
    case CorePermissionField::CanFly:
      permissions.can_fly = value;
      break;
    case CorePermissionField::HasCollision:
      permissions.has_collision = value;
      break;
    case CorePermissionField::IsVisibleToOthers:
      permissions.is_visible_to_others = value;
      break;
  }
}

[[nodiscard]] bool resolved_core_value(
    const ResolvedCorePermissions& permissions,
    CorePermissionField field) noexcept {
  switch (field) {
    case CorePermissionField::CanTakeDamage:
      return permissions.can_take_damage;
    case CorePermissionField::CanModifyWorld:
      return permissions.can_modify_world;
    case CorePermissionField::CanInteract:
      return permissions.can_interact;
    case CorePermissionField::CanOpenInventory:
      return permissions.can_open_inventory;
    case CorePermissionField::HasCreativeInventory:
      return permissions.has_creative_inventory;
    case CorePermissionField::CanFly:
      return permissions.can_fly;
    case CorePermissionField::HasCollision:
      return permissions.has_collision;
    case CorePermissionField::IsVisibleToOthers:
      return permissions.is_visible_to_others;
  }
  return false;
}

[[nodiscard]] CorePermissionSet core_from_resolved(
    ResolvedCorePermissions permissions) noexcept {
  CorePermissionSet result;
  for (const CorePermissionField field : CorePermissionFields) {
    set_core_value(
        result,
        field,
        resolved_core_value(permissions, field) ? TriState::Allow : TriState::Deny);
  }
  return result;
}

[[nodiscard]] ResolvedCorePermissions apply_core_override(
    ResolvedCorePermissions base,
    const CorePermissionSet& overrides) {
  for (const CorePermissionField field : CorePermissionFields) {
    const TriState value = core_value(overrides, field);
    if (value == TriState::Unset) {
      continue;
    }
    set_core_value(base, field, value == TriState::Allow);
  }
  return base;
}

void merge_core_for_group(
    CorePermissionSet& target,
    const CorePermissionSet& source,
    bool equal_priority) {
  for (const CorePermissionField field : CorePermissionFields) {
    const TriState incoming = core_value(source, field);
    if (incoming == TriState::Unset) {
      continue;
    }

    const TriState current = core_value(target, field);
    if (equal_priority && current == TriState::Deny && incoming == TriState::Allow) {
      continue;
    }
    set_core_value(target, field, incoming);
  }
}

void merge_custom_for_group(
    CustomPermissionMap& target,
    const CustomPermissionMap& source,
    bool equal_priority) {
  for (const auto& [node, incoming] : source) {
    if (incoming == TriState::Unset) {
      continue;
    }

    const auto found = target.find(node);
    if (equal_priority && found != target.end() && found->second == TriState::Deny &&
        incoming == TriState::Allow) {
      continue;
    }
    target[node] = incoming;
  }
}

void merge_layer_for_group(
    PermissionLayer& target,
    const PermissionLayer& source,
    bool equal_priority) {
  merge_core_for_group(target.core, source.core, equal_priority);
  merge_custom_for_group(target.custom, source.custom, equal_priority);
}

void merge_custom_final(CustomPermissionMap& target, const CustomPermissionMap& source) {
  for (const auto& [node, incoming] : source) {
    if (incoming == TriState::Unset) {
      continue;
    }
    const auto found = target.find(node);
    if (found != target.end() && found->second == TriState::Deny &&
        incoming == TriState::Allow) {
      continue;
    }
    target[node] = incoming;
  }
}

[[nodiscard]] bool wildcard_matches(std::string_view pattern, std::string_view node) noexcept {
  if (pattern == "*") {
    return true;
  }
  if (pattern.size() < 2 || pattern.substr(pattern.size() - 2) != ".*") {
    return false;
  }
  const std::string_view prefix = pattern.substr(0, pattern.size() - 1);
  return node.size() >= prefix.size() && node.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<CorePermissionField> core_permission_field_from_name(
    std::string_view name) noexcept {
  if (name == "can_take_damage") {
    return CorePermissionField::CanTakeDamage;
  }
  if (name == "can_modify_world") {
    return CorePermissionField::CanModifyWorld;
  }
  if (name == "can_interact") {
    return CorePermissionField::CanInteract;
  }
  if (name == "can_open_inventory") {
    return CorePermissionField::CanOpenInventory;
  }
  if (name == "has_creative_inventory") {
    return CorePermissionField::HasCreativeInventory;
  }
  if (name == "can_fly") {
    return CorePermissionField::CanFly;
  }
  if (name == "has_collision") {
    return CorePermissionField::HasCollision;
  }
  if (name == "is_visible_to_others") {
    return CorePermissionField::IsVisibleToOthers;
  }
  return std::nullopt;
}

} // namespace

std::optional<TriState> tri_state_from_string(std::string_view text) {
  const std::string lowered = lower_ascii_copy(text);
  if (lowered == "allow" || lowered == "allowed" || lowered == "true" ||
      lowered == "yes") {
    return TriState::Allow;
  }
  if (lowered == "deny" || lowered == "denied" || lowered == "false" ||
      lowered == "no") {
    return TriState::Deny;
  }
  if (lowered == "unset" || lowered == "none" || lowered == "null") {
    return TriState::Unset;
  }
  return std::nullopt;
}

bool set_core_permission_by_name(
    CorePermissionSet& permissions,
    std::string_view name,
    TriState value) noexcept {
  const std::optional<CorePermissionField> field =
      core_permission_field_from_name(name);
  if (!field.has_value()) {
    return false;
  }
  set_core_value(permissions, field.value(), value);
  return true;
}

TriState ResolvedPermissions::custom_value(std::string_view node) const {
  return custom_permission_value(custom, node);
}

bool ResolvedPermissions::has(std::string_view node) const {
  return custom_value(node) == TriState::Allow;
}

GameModeRegistry::Result GameModeRegistry::add_builtin_mode(
    std::string id,
    ResolvedCorePermissions permissions) {
  if (id.empty()) {
    return Result::failure({"built-in game mode id is empty"});
  }
  if (mode_indices_.find(id) != mode_indices_.end()) {
    return Result::failure({"duplicate game mode id: " + id});
  }

  GameModeDefinition definition;
  definition.id = std::move(id);
  definition.permissions = permissions;
  definition.overrides = core_from_resolved(permissions);
  definition.builtin = true;

  mode_indices_[definition.id] = modes_.size();
  modes_.push_back(std::move(definition));
  return Result::success();
}

GameModeRegistry::Result GameModeRegistry::add_custom_mode(
    std::string id,
    std::string base_id,
    CorePermissionSet overrides,
    CustomPermissionMap custom_permissions) {
  if (id.empty()) {
    return Result::failure({"custom game mode id is empty"});
  }
  if (mode_indices_.find(id) != mode_indices_.end()) {
    return Result::failure({"duplicate game mode id: " + id});
  }

  const std::optional<ResolvedCorePermissions> base = permissions_for(base_id);
  if (!base.has_value()) {
    return Result::failure({"custom game mode base is missing: " + base_id});
  }

  GameModeDefinition definition;
  definition.id = std::move(id);
  definition.base_id = std::move(base_id);
  definition.overrides = overrides;
  definition.permissions = apply_core_override(base.value(), overrides);
  definition.custom_permissions = std::move(custom_permissions);
  definition.builtin = false;

  mode_indices_[definition.id] = modes_.size();
  modes_.push_back(std::move(definition));
  return Result::success();
}

const GameModeDefinition* GameModeRegistry::find(std::string_view id) const noexcept {
  const auto found = mode_indices_.find(std::string{id});
  return found == mode_indices_.end() ? nullptr : &modes_[found->second];
}

std::optional<ResolvedCorePermissions> GameModeRegistry::permissions_for(
    std::string_view id) const noexcept {
  const GameModeDefinition* mode = find(id);
  if (mode == nullptr) {
    return std::nullopt;
  }
  return mode->permissions;
}

std::vector<std::string> GameModeRegistry::mode_ids() const {
  std::vector<std::string> ids;
  ids.reserve(modes_.size());
  for (const GameModeDefinition& mode : modes_) {
    ids.push_back(mode.id);
  }
  return ids;
}

std::span<const GameModeDefinition> GameModeRegistry::modes() const noexcept {
  return modes_;
}

PermissionGroupRegistry::Result PermissionGroupRegistry::add_group(PermissionGroup group) {
  if (group.id.empty()) {
    return Result::failure({"permission group id is empty"});
  }
  if (group_indices_.find(group.id) != group_indices_.end()) {
    return Result::failure({"duplicate permission group id: " + group.id});
  }
  group_indices_[group.id] = groups_.size();
  groups_.push_back(std::move(group));
  return Result::success();
}

const PermissionGroup* PermissionGroupRegistry::find(std::string_view id) const noexcept {
  const auto found = group_indices_.find(std::string{id});
  return found == group_indices_.end() ? nullptr : &groups_[found->second];
}

std::span<const PermissionGroup> PermissionGroupRegistry::groups() const noexcept {
  return groups_;
}

PermissionGroupRegistry::LayerResult PermissionGroupRegistry::resolve_groups(
    std::span<const std::string> group_ids) const {
  struct ResolvedGroupLayer {
    i32 priority{};
    usize order{};
    PermissionLayer layer{};
  };

  std::vector<ResolvedGroupLayer> layers;
  layers.reserve(group_ids.size());
  for (usize index = 0; index < group_ids.size(); ++index) {
    std::vector<std::string> stack;
    auto resolved = resolve_group(group_ids[index], stack);
    if (!resolved) {
      return resolved;
    }
    const PermissionGroup* group = find(group_ids[index]);
    if (group == nullptr) {
      return LayerResult::failure({"missing permission group: " + group_ids[index]});
    }
    layers.push_back({group->priority, index, std::move(resolved.value())});
  }

  std::sort(layers.begin(), layers.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.priority != rhs.priority) {
      return lhs.priority < rhs.priority;
    }
    return lhs.order < rhs.order;
  });

  PermissionLayer merged;
  std::optional<i32> previous_priority;
  for (const ResolvedGroupLayer& layer : layers) {
    const bool equal_priority =
        previous_priority.has_value() && previous_priority.value() == layer.priority;
    merge_layer_for_group(merged, layer.layer, equal_priority);
    previous_priority = layer.priority;
  }
  return LayerResult::success(std::move(merged));
}

PermissionGroupRegistry::LayerResult PermissionGroupRegistry::resolve_group(
    std::string_view id,
    std::vector<std::string>& stack) const {
  const PermissionGroup* group = find(id);
  if (group == nullptr) {
    return LayerResult::failure({"missing permission group: " + std::string{id}});
  }

  if (std::find(stack.begin(), stack.end(), id) != stack.end()) {
    return LayerResult::failure({"circular permission group inheritance: " + std::string{id}});
  }

  stack.push_back(group->id);
  PermissionLayer layer;
  for (const std::string& parent_id : group->inherits) {
    auto parent = resolve_group(parent_id, stack);
    if (!parent) {
      return parent;
    }
    merge_layer_for_group(layer, parent.value(), false);
  }
  stack.pop_back();

  PermissionLayer own;
  own.core = group->core;
  own.custom = group->custom;
  merge_layer_for_group(layer, own, false);
  return LayerResult::success(std::move(layer));
}

ResolvedPermissions resolve_permissions(
    const GameModeRegistry& game_modes,
    const PermissionGroupRegistry& groups,
    const PermissionResolveRequest& request) {
  ResolvedPermissions resolved;
  const GameModeDefinition* mode = game_modes.find(request.game_mode_id);
  resolved.core = mode == nullptr ? survival_core_permissions() : mode->permissions;
  if (mode != nullptr) {
    merge_custom_final(resolved.custom, mode->custom_permissions);
  }

  std::vector<PermissionLayer> layers;

  if (request.player_permissions != nullptr) {
    auto group_layer = groups.resolve_groups(request.player_permissions->groups);
    if (group_layer) {
      layers.push_back(std::move(group_layer.value()));
    }

    PermissionLayer direct;
    direct.core = request.player_permissions->overrides;
    direct.custom = request.player_permissions->custom_overrides;
    layers.push_back(std::move(direct));
  }

  for (const PermissionLayer& modifier : request.temporary_modifiers) {
    layers.push_back(modifier);
  }
  if (request.region_permissions != nullptr) {
    layers.push_back(*request.region_permissions);
  }
  for (const PermissionLayer& plugin : request.plugin_overrides) {
    layers.push_back(plugin);
  }

  std::vector<CorePermissionSet> core_layers;
  core_layers.reserve(layers.size());
  for (const PermissionLayer& layer : layers) {
    core_layers.push_back(layer.core);
  }
  resolved.core = apply_core_overrides(resolved.core, core_layers);
  for (const PermissionLayer& layer : layers) {
    merge_custom_final(resolved.custom, layer.custom);
  }
  return resolved;
}

ResolvedCorePermissions apply_core_overrides(
    ResolvedCorePermissions base,
    std::span<const CorePermissionSet> overrides) {
  bool denied[static_cast<usize>(CorePermissionField::IsVisibleToOthers) + 1U]{};
  for (const CorePermissionSet& layer : overrides) {
    for (const CorePermissionField field : CorePermissionFields) {
      const usize index = static_cast<usize>(field);
      const TriState value = core_value(layer, field);
      if (value == TriState::Unset) {
        continue;
      }
      if (value == TriState::Deny) {
        set_core_value(base, field, false);
        denied[index] = true;
      } else if (!denied[index]) {
        set_core_value(base, field, true);
      }
    }
  }
  return base;
}

TriState custom_permission_value(
    const CustomPermissionMap& permissions,
    std::string_view node) {
  const auto exact = permissions.find(std::string{node});
  if (exact != permissions.end()) {
    return exact->second;
  }

  TriState best = TriState::Unset;
  usize best_specificity = 0;
  for (const auto& [pattern, value] : permissions) {
    if (!wildcard_matches(pattern, node)) {
      continue;
    }
    const usize specificity = pattern.size();
    if (specificity > best_specificity ||
        (specificity == best_specificity && value == TriState::Deny)) {
      best = value;
      best_specificity = specificity;
    }
  }
  return best;
}

bool is_valid_custom_permission_node(std::string_view node) noexcept {
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

  const usize wildcard = node.find('*');
  return wildcard == std::string_view::npos ||
         (wildcard + 1U == node.size() && wildcard > 0U && node[wildcard - 1U] == '.');
}

} // namespace arv::player
