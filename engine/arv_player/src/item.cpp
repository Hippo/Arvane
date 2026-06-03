#include "arv/player/item.hpp"

#include "arv/core/time.hpp"
#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::player {

namespace {

constexpr std::string_view MissingTextureAsset = "arvane:texture/missing";

bool item_name_less(const ItemDefinition& lhs, const ItemDefinition& rhs) {
  if (lhs.name == AirItemName) {
    return true;
  }
  if (rhs.name == AirItemName) {
    return false;
  }
  return lhs.name < rhs.name;
}

bool has_texture_source(const ItemDefinition& definition) noexcept {
  return !definition.texture_asset.empty();
}

bool has_model_source(const ItemDefinition& definition) noexcept {
  return !definition.model_asset.empty();
}

bool has_block_source(const ItemDefinition& definition) noexcept {
  return definition.placement_variant.has_value() || definition.source_block.has_value() ||
         definition.generated_from_block;
}

[[nodiscard]] bool data_value_equal(const io::DataValue& lhs, const io::DataValue& rhs) noexcept {
  if (lhs.kind() != rhs.kind()) {
    return false;
  }

  switch (lhs.kind()) {
    case io::DataKind::Null:
      return true;
    case io::DataKind::Bool:
      return lhs.as_bool_or(false) == rhs.as_bool_or(false);
    case io::DataKind::I64:
      return lhs.as_i64_or(0) == rhs.as_i64_or(0);
    case io::DataKind::U64:
      return lhs.as_u64_or(0U) == rhs.as_u64_or(0U);
    case io::DataKind::F64:
      return lhs.as_f64_or(0.0) == rhs.as_f64_or(0.0);
    case io::DataKind::String:
      return lhs.as_string_or({}) == rhs.as_string_or({});
    case io::DataKind::Bytes: {
      const io::DataValue::Bytes* lhs_bytes = lhs.bytes_if();
      const io::DataValue::Bytes* rhs_bytes = rhs.bytes_if();
      return lhs_bytes != nullptr && rhs_bytes != nullptr && *lhs_bytes == *rhs_bytes;
    }
    case io::DataKind::Array: {
      const io::DataValue::Array* lhs_array = lhs.array_if();
      const io::DataValue::Array* rhs_array = rhs.array_if();
      if (lhs_array == nullptr || rhs_array == nullptr ||
          lhs_array->size() != rhs_array->size()) {
        return false;
      }
      for (usize index = 0; index < lhs_array->size(); ++index) {
        if (!data_value_equal((*lhs_array)[index], (*rhs_array)[index])) {
          return false;
        }
      }
      return true;
    }
    case io::DataKind::Object: {
      const io::DataValue::Object* lhs_object = lhs.object_if();
      const io::DataValue::Object* rhs_object = rhs.object_if();
      if (lhs_object == nullptr || rhs_object == nullptr ||
          lhs_object->size() != rhs_object->size()) {
        return false;
      }
      for (const auto& [key, lhs_child] : *lhs_object) {
        const auto found = rhs_object->find(key);
        if (found == rhs_object->end() || !data_value_equal(lhs_child, found->second)) {
          return false;
        }
      }
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool document_is_empty_stack_data(const io::DataDocument& document) noexcept {
  if (document.root.kind() == io::DataKind::Null) {
    return true;
  }
  const io::DataValue::Object* object = document.root.object_if();
  return object != nullptr && object->empty();
}

Expected<ItemRenderType, ItemRegistryError> resolve_render_type(const ItemDefinition& definition) {
  const ItemRenderType render_type = definition.explicit_render_type.value_or(
      has_block_source(definition)
          ? ItemRenderType::Block
          : (has_texture_source(definition)
                 ? ItemRenderType::Texture
                 : (has_model_source(definition) ? ItemRenderType::Model : ItemRenderType::Texture)));

  switch (render_type) {
    case ItemRenderType::Texture:
      return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Texture);
    case ItemRenderType::Model:
      if (definition.model_asset.empty()) {
        return Expected<ItemRenderType, ItemRegistryError>::failure(
            {"item render_type model requires model for " + definition.name});
      }
      return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Model);
    case ItemRenderType::Block:
      if (!definition.placement_variant.has_value()) {
        return Expected<ItemRenderType, ItemRegistryError>::failure(
            {"item render_type block requires places_block or generated block source for " + definition.name});
      }
      return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Block);
  }

  return Expected<ItemRenderType, ItemRegistryError>::failure({"invalid item render type for " + definition.name});
}

std::optional<ItemRegistryError> resolve_places_block(
    const voxel::BlockRegistry& registry,
    ItemDefinition& definition) {
  if (definition.places_block.empty()) {
    return std::nullopt;
  }

  const std::optional<voxel::BlockID> block_id = registry.find_block(definition.places_block);
  if (!block_id.has_value()) {
    return ItemRegistryError{
        "item " + definition.name + " references missing places_block " + definition.places_block};
  }
  const voxel::BlockRecord* block = registry.block(block_id.value());
  if (block == nullptr) {
    return ItemRegistryError{"item " + definition.name + " places_block did not resolve"};
  }

  definition.placement_variant = block->default_variant;
  definition.source_block = block->id;
  return std::nullopt;
}

void merge_explicit_definition(ItemDefinition& target, ItemDefinition source) {
  if (!source.display_name.empty()) {
    target.display_name = std::move(source.display_name);
  }
  if (!source.behavior_id.empty()) {
    target.behavior_id = std::move(source.behavior_id);
  }
  if (!source.category.empty()) {
    target.category = std::move(source.category);
  }
  target.max_stack_size = source.max_stack_size;
  if (source.explicit_render_type.has_value()) {
    target.explicit_render_type = source.explicit_render_type;
  }
  if (!source.texture_asset.empty()) {
    target.texture_asset = std::move(source.texture_asset);
  }
  if (!source.model_asset.empty()) {
    target.model_asset = std::move(source.model_asset);
  }
  if (!source.places_block.empty()) {
    target.places_block = std::move(source.places_block);
    target.placement_variant = source.placement_variant;
    target.source_block = source.source_block;
  }
  if (source.buoyancy_set) {
    target.buoyancy = source.buoyancy;
    target.buoyancy_set = true;
  }
  if (!source.tool_category.empty()) {
    target.tool_category = std::move(source.tool_category);
    target.tool_break_speed_multiplier = source.tool_break_speed_multiplier;
  }
  if (source.attack_data.has_value()) {
    target.attack_data = source.attack_data;
  }
  for (auto& [action, animation] : source.held_animations) {
    target.held_animations.insert_or_assign(std::move(action), std::move(animation));
  }
  target.generated_from_block = target.generated_from_block || source.generated_from_block;
}

} // namespace

bool ItemStack::operator==(const ItemStack& other) const {
  return item == other.item && count == other.count && item_stack_data_equal(data, other.data);
}

ItemBehaviorRegistry::Result ItemBehaviorRegistry::register_handler(
    std::string id,
    ItemBehaviorHandler handler) {
  if (id.empty()) {
    return Result::failure({"item behavior handler id is empty"});
  }
  if (contains(id)) {
    return Result::failure({"duplicate item behavior handler id: " + id});
  }
  handlers_.push_back({std::move(id), std::move(handler)});
  return Result::success();
}

const ItemBehaviorHandler* ItemBehaviorRegistry::find(std::string_view id) const noexcept {
  for (const auto& [handler_id, handler] : handlers_) {
    if (handler_id == id) {
      return &handler;
    }
  }
  return nullptr;
}

bool ItemBehaviorRegistry::contains(std::string_view id) const noexcept {
  return find(id) != nullptr;
}

Expected<ItemRenderType, ItemRegistryError> parse_item_render_type(std::string_view value) {
  if (value == "texture") {
    return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Texture);
  }
  if (value == "model") {
    return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Model);
  }
  if (value == "block") {
    return Expected<ItemRenderType, ItemRegistryError>::success(ItemRenderType::Block);
  }
  return Expected<ItemRenderType, ItemRegistryError>::failure(
      {"invalid item render_type: " + std::string{value}});
}

Expected<HeldItemAnimationPlayCondition, ItemRegistryError> parse_held_item_animation_play_condition(
    std::string_view value,
    std::string_view context) {
  if (value == "always") {
    return Expected<HeldItemAnimationPlayCondition, ItemRegistryError>::success(
        HeldItemAnimationPlayCondition::Always);
  }
  if (value == "placement_succeeds" || value == "placement_predicted") {
    return Expected<HeldItemAnimationPlayCondition, ItemRegistryError>::success(
        HeldItemAnimationPlayCondition::PlacementSucceeds);
  }
  return Expected<HeldItemAnimationPlayCondition, ItemRegistryError>::failure(
      {"invalid held animation play_when '" + std::string{value} + "' for " +
       std::string{context}});
}

Expected<HeldItemAnimationBinding, ItemRegistryError> parse_held_item_animation_binding(
    const io::DataValue& animation,
    std::string_view item_name,
    std::string_view action) {
  const std::string context =
      "held_animations." + std::string{action} + " for " + std::string{item_name};
  HeldItemAnimationBinding binding;

  if (animation.kind() == io::DataKind::Null) {
    binding.selection = HeldItemAnimationSelection::Disabled;
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
  }
  if (animation.kind() == io::DataKind::String) {
    binding.selection = HeldItemAnimationSelection::Named;
    binding.animation = std::string{animation.as_string_or({})};
    if (binding.animation.empty()) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + " animation must be non-empty"});
    }
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
  }
  if (animation.kind() != io::DataKind::Object) {
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
        {context + " must be a string, null, or object"});
  }

  if (const io::DataValue* animation_iter = animation.get("animation");
      animation_iter != nullptr) {
    if (animation_iter->kind() == io::DataKind::Null) {
      binding.selection = HeldItemAnimationSelection::Disabled;
    } else if (animation_iter->kind() == io::DataKind::String) {
      binding.selection = HeldItemAnimationSelection::Named;
      binding.animation = std::string{animation_iter->as_string_or({})};
      if (binding.animation.empty()) {
        return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
            {context + ".animation must be non-empty"});
      }
    } else {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + ".animation must be a string or null"});
    }
  }

  if (const io::DataValue* play_when_iter = animation.get("play_when");
      play_when_iter != nullptr) {
    if (play_when_iter->kind() != io::DataKind::String) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + ".play_when must be a string"});
    }
    auto condition = parse_held_item_animation_play_condition(
        play_when_iter->as_string_or({}),
        context);
    if (!condition) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(condition.error());
    }
    binding.play_condition = condition.value();
  }

  return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
}

ItemRegistry::Result ItemRegistry::build(
    std::vector<ItemDefinition> definitions,
    ItemRegistryBuildOptions options) {
  if (definitions.empty()) {
    return Result::failure({"item registry is empty; required item arvane:air is missing"});
  }

  HashMap<std::string, usize> definition_names;
  for (usize index = 0; index < definitions.size(); ++index) {
    const ItemDefinition& definition = definitions[index];
    if (definition.name.empty()) {
      return Result::failure({"item definition has empty name"});
    }
    if (definition.max_stack_size == 0U) {
      return Result::failure({"item max_stack_size must be non-zero for " + definition.name});
    }
    if (!std::isfinite(definition.tool_break_speed_multiplier) ||
        definition.tool_break_speed_multiplier < 0.0F) {
      return Result::failure({"item tool break speed multiplier must be finite and >= 0 for " + definition.name});
    }
    if (!std::isfinite(definition.buoyancy)) {
      return Result::failure({"item buoyancy must be finite for " + definition.name});
    }
    if (!definition.behavior_id.empty() && options.behavior_exists &&
        !options.behavior_exists(definition.behavior_id)) {
      return Result::failure(
          {"missing item behavior handler for " + definition.name + ": " +
           definition.behavior_id});
    }
    if (definition.attack_data.has_value()) {
      const ItemAttackData& attack = definition.attack_data.value();
      if (!std::isfinite(attack.damage_amount) || attack.damage_amount < 0.0F) {
        return Result::failure({"item attack damage must be finite and >= 0 for " + definition.name});
      }
      if (!std::isfinite(attack.attack_range) || attack.attack_range <= 0.0F) {
        return Result::failure({"item attack range must be finite and > 0 for " + definition.name});
      }
      if (!std::isfinite(attack.horizontal_knockback) ||
          attack.horizontal_knockback < 0.0F) {
        return Result::failure({"item horizontal knockback must be finite and >= 0 for " + definition.name});
      }
      if (!std::isfinite(attack.vertical_knockback) ||
          attack.vertical_knockback < 0.0F) {
        return Result::failure({"item vertical knockback must be finite and >= 0 for " + definition.name});
      }
      if (!std::isfinite(attack.sprint_knockback_bonus) ||
          attack.sprint_knockback_bonus < 0.0F) {
        return Result::failure({"item sprint knockback bonus must be finite and >= 0 for " + definition.name});
      }
    }
    auto resolved_type = resolve_render_type(definition);
    if (!resolved_type) {
      return Result::failure(resolved_type.error());
    }
    if (!definition_names.emplace(definition.name, index).second) {
      return Result::failure({"duplicate item definition: " + definition.name});
    }
  }

  if (definition_names.find(std::string{AirItemName}) == definition_names.end()) {
    return Result::failure({"required item arvane:air is missing"});
  }

  std::sort(definitions.begin(), definitions.end(), item_name_less);

  ItemRegistry registry;
  registry.items_.reserve(definitions.size());

  u32 next_item_id = 0;
  for (ItemDefinition& definition : definitions) {
    ItemRecord record;
    record.id = ItemID{next_item_id++};
    record.name = std::move(definition.name);
    record.display_name =
        definition.display_name.empty() ? record.name : std::move(definition.display_name);
    record.behavior_id = std::move(definition.behavior_id);
    record.category = std::move(definition.category);
    record.max_stack_size = definition.max_stack_size;
    auto resolved_type = resolve_render_type(definition);
    if (!resolved_type) {
      return Result::failure(resolved_type.error());
    }
    record.render_type = resolved_type.value();
    record.texture_asset = std::move(definition.texture_asset);
    record.model_asset = std::move(definition.model_asset);
    record.placement_variant = definition.placement_variant;
    record.render_block_variant =
        record.render_type == ItemRenderType::Block ? definition.placement_variant : std::nullopt;
    record.source_block = definition.source_block;
    record.buoyancy = definition.buoyancy;
    record.tool_category = std::move(definition.tool_category);
    record.tool_break_speed_multiplier = definition.tool_break_speed_multiplier;
    record.attack_data = definition.attack_data;
    record.held_animations = std::move(definition.held_animations);

    if (record.render_type == ItemRenderType::Texture && record.texture_asset.empty()) {
      record.texture_asset = std::string{MissingTextureAsset};
    }

    if (record.id == AirItemID && record.name != AirItemName) {
      return Result::failure({"item registry failed to preserve arvane:air at ID 0"});
    }
    if (!registry.name_to_id_.emplace(record.name, record.id).second) {
      return Result::failure({"duplicate item name during registry build: " + record.name});
    }
    if (record.name == MissingItemName) {
      registry.missing_item_id_ = record.id;
    }

    registry.items_.push_back(std::move(record));
  }

  if (registry.items_.empty() || registry.items_.front().id != AirItemID ||
      registry.items_.front().name != AirItemName) {
    return Result::failure({"item air invariants were not preserved during registry build"});
  }

  return Result::success(std::move(registry));
}

ItemRegistry::Result ItemRegistry::build_default_block_items(
    const voxel::BlockRegistry& registry,
    ItemRegistryBuildOptions options) {
  return build_from_blocks_and_items(registry, {}, std::move(options));
}

ItemRegistry::Result ItemRegistry::build_from_blocks_and_items(
    const voxel::BlockRegistry& registry,
    std::vector<ItemDefinition> explicit_definitions,
    ItemRegistryBuildOptions options) {
  std::vector<ItemDefinition> definitions;
  definitions.reserve(registry.block_count() + 2U);
  definitions.push_back(make_air_item_definition());
  definitions.push_back(make_missing_item_definition());
  HashMap<std::string, usize> definition_indices;
  definition_indices.emplace(definitions[0].name, 0U);
  definition_indices.emplace(definitions[1].name, 1U);

  for (u32 block_index = 0; block_index < registry.block_count(); ++block_index) {
    const voxel::BlockRecord* block = registry.block(voxel::BlockID{block_index});
    if (block == nullptr || block->name == voxel::AirBlockName || !block->is_item) {
      continue;
    }

    ItemDefinition definition;
    definition.name = block->name;
    definition.display_name =
        block->display_name.empty() ? block->name : block->display_name;
    definition.behavior_id = options.default_block_item_behavior_id;
    definition.category = "blocks";
    definition.max_stack_size = 64;
    definition.places_block = block->name;
    definition.generated_from_block = true;
    definition.placement_variant = block->default_variant;
    definition.source_block = block->id;
    definition.buoyancy = block->item_buoyancy;
    definition.buoyancy_set = true;
    definition_indices.emplace(definition.name, definitions.size());
    definitions.push_back(std::move(definition));
  }

  HashSet<std::string> explicit_names;
  for (ItemDefinition& definition : explicit_definitions) {
    if (definition.name.empty()) {
      return Result::failure({"item definition has empty name"});
    }
    if (!explicit_names.insert(definition.name).second) {
      return Result::failure({"duplicate explicit item definition: " + definition.name});
    }
    if (definition.max_stack_size == 0U) {
      return Result::failure({"item max_stack_size must be non-zero for " + definition.name});
    }
    if (!std::isfinite(definition.tool_break_speed_multiplier) ||
        definition.tool_break_speed_multiplier < 0.0F) {
      return Result::failure({"item tool break speed multiplier must be finite and >= 0 for " + definition.name});
    }
    if (!std::isfinite(definition.buoyancy)) {
      return Result::failure({"item buoyancy must be finite for " + definition.name});
    }
    if (const auto block_id = registry.find_block(definition.name);
        block_id.has_value() && definition.name != AirItemName) {
      const voxel::BlockRecord* block = registry.block(block_id.value());
      if (block != nullptr && !block->is_item) {
        return Result::failure(
            {"explicit item conflicts with non-item block: " + definition.name});
      }
    }
    if (auto error = resolve_places_block(registry, definition); error.has_value()) {
      return Result::failure(error.value());
    }

    const auto existing = definition_indices.find(definition.name);
    if (existing == definition_indices.end()) {
      definition_indices.emplace(definition.name, definitions.size());
      definitions.push_back(std::move(definition));
    } else {
      merge_explicit_definition(definitions[existing->second], std::move(definition));
    }
  }

  for (u32 block_index = 0; block_index < registry.block_count(); ++block_index) {
    const voxel::BlockRecord* block = registry.block(voxel::BlockID{block_index});
    if (block == nullptr || block->drops.kind != voxel::BlockDropKind::Item) {
      continue;
    }
    if (definition_indices.find(block->drops.item_name) == definition_indices.end()) {
      return Result::failure(
          {"block " + block->name + " drops missing item " + block->drops.item_name});
    }
  }

  return build(std::move(definitions), std::move(options));
}

const ItemRecord* ItemRegistry::item(ItemID id) const noexcept {
  if (id.value >= items_.size()) {
    return nullptr;
  }
  return &items_[id.value];
}

std::optional<ItemID> ItemRegistry::find_item(std::string_view name) const {
  const auto found = name_to_id_.find(std::string{name});
  if (found == name_to_id_.end()) {
    return std::nullopt;
  }
  return found->second;
}

void ItemRegistry::set_display_name(ItemID id, std::string display_name) {
  if (id.value >= items_.size()) {
    return;
  }
  items_[id.value].display_name = std::move(display_name);
}

const ItemRecord* ItemRegistry::missing_item() const noexcept {
  return missing_item_id_.has_value() ? item(missing_item_id_.value()) : nullptr;
}

ItemDefinition make_air_item_definition() {
  ItemDefinition definition;
  definition.name = std::string{AirItemName};
  definition.display_name = std::string{AirItemName};
  definition.max_stack_size = 1;
  definition.explicit_render_type = ItemRenderType::Texture;
  definition.texture_asset = std::string{MissingTextureAsset};
  return definition;
}

ItemDefinition make_missing_item_definition() {
  ItemDefinition definition;
  definition.name = std::string{MissingItemName};
  definition.display_name = std::string{MissingItemName};
  definition.max_stack_size = 64;
  definition.explicit_render_type = ItemRenderType::Texture;
  definition.texture_asset = std::string{MissingTextureAsset};
  return definition;
}

bool item_stack_data_equal(
    const std::optional<io::DataDocument>& lhs,
    const std::optional<io::DataDocument>& rhs) noexcept {
  const bool lhs_empty = !lhs.has_value() || document_is_empty_stack_data(lhs.value());
  const bool rhs_empty = !rhs.has_value() || document_is_empty_stack_data(rhs.value());
  if (lhs_empty || rhs_empty) {
    return lhs_empty == rhs_empty;
  }
  return lhs->schema_version == rhs->schema_version && data_value_equal(lhs->root, rhs->root);
}

bool item_stack_has_data(const ItemStack& stack) noexcept {
  return stack.data.has_value() && !document_is_empty_stack_data(stack.data.value());
}

void normalize_item_stack_data(ItemStack& stack) noexcept {
  if (stack.data.has_value() && document_is_empty_stack_data(stack.data.value())) {
    stack.data.reset();
  }
}

ItemStack item_stack_with_count(const ItemStack& stack, u16 count) {
  ItemStack copy = stack;
  copy.count = count;
  normalize_item_stack_data(copy);
  return copy;
}

Expected<std::vector<ItemDefinition>, ItemRegistryError> parse_item_definitions_json(
    std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
        {"failed to parse item definitions JSON: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  const io::DataValue* items = &root;
  if (root.kind() == io::DataKind::Object) {
    const io::DataValue* found = root.get("items");
    if (found == nullptr) {
      return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
          {"item definition JSON object must contain items array"});
    }
    items = found;
  }
  const io::DataValue::Array* item_array = items->array_if();
  if (item_array == nullptr) {
    return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
        {"item definitions must be a JSON array"});
  }

  std::vector<ItemDefinition> definitions;
  definitions.reserve(item_array->size());
  for (const io::DataValue& item : *item_array) {
      if (item.kind() != io::DataKind::Object) {
        return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
            {"item definition entry must be an object"});
      }

      const io::DataValue* name_iter = item.get("name");
      if (name_iter == nullptr || name_iter->kind() != io::DataKind::String) {
        return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
            {"item definition is missing string field name"});
      }

      ItemDefinition definition;
      definition.name = std::string{name_iter->as_string_or({})};

      const io::DataValue* display_iter = item.get("display_name");
      if (display_iter != nullptr) {
        definition.display_name = std::string{display_iter->as_string_or({})};
      }
      const io::DataValue* behavior_iter = item.get("behavior");
      if (behavior_iter != nullptr) {
        if (behavior_iter->kind() != io::DataKind::String) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"behavior must be a string for " + definition.name});
        }
        definition.behavior_id = std::string{behavior_iter->as_string_or({})};
        if (definition.behavior_id.empty()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"behavior must be non-empty for " + definition.name});
        }
      }
      const io::DataValue* category_iter = item.get("category");
      if (category_iter != nullptr) {
        definition.category = std::string{category_iter->as_string_or({})};
      }
      const io::DataValue* stack_iter = item.get("max_stack_size");
      if (stack_iter != nullptr) {
        definition.max_stack_size = static_cast<u16>(stack_iter->as_u64_or(definition.max_stack_size));
      }
      const io::DataValue* render_type_iter = item.get("render_type");
      if (render_type_iter != nullptr) {
        if (render_type_iter->kind() != io::DataKind::String) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"render_type must be a string for " + definition.name});
        }
        auto render_type = parse_item_render_type(render_type_iter->as_string_or({}));
        if (!render_type) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(render_type.error());
        }
        definition.explicit_render_type = render_type.value();
      }
      const io::DataValue* texture_iter = item.get("texture");
      if (texture_iter != nullptr) {
        definition.texture_asset = std::string{texture_iter->as_string_or({})};
      }
      const io::DataValue* model_iter = item.get("model");
      if (model_iter != nullptr) {
        definition.model_asset = std::string{model_iter->as_string_or({})};
      }
      const io::DataValue* places_block_iter = item.get("places_block");
      if (places_block_iter != nullptr) {
        definition.places_block = std::string{places_block_iter->as_string_or({})};
      }
      const io::DataValue* buoyancy_iter = item.get("buoyancy");
      if (buoyancy_iter != nullptr) {
        if (!io::is_number_value(*buoyancy_iter)) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"buoyancy must be a number for " + definition.name});
        }
        definition.buoyancy = static_cast<f32>(buoyancy_iter->as_f64_or(definition.buoyancy));
        definition.buoyancy_set = true;
      }
      const io::DataValue* tool_iter = item.get("tool");
      if (tool_iter != nullptr) {
        if (tool_iter->kind() != io::DataKind::Object) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"tool must be an object for " + definition.name});
        }
        const io::DataValue* category_iter = tool_iter->get("category");
        if (category_iter != nullptr) {
          if (category_iter->kind() != io::DataKind::String) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"tool.category must be a string for " + definition.name});
          }
          definition.tool_category = std::string{category_iter->as_string_or({})};
        }
        const io::DataValue* speed_iter = tool_iter->get("break_speed_multiplier");
        if (speed_iter != nullptr) {
          definition.tool_break_speed_multiplier =
              static_cast<f32>(speed_iter->as_f64_or(definition.tool_break_speed_multiplier));
        }
      }
      const io::DataValue* components_iter = item.get("components");
      if (components_iter != nullptr) {
        if (components_iter->kind() != io::DataKind::Object) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"components must be an object for " + definition.name});
        }
        const io::DataValue* damage_iter = components_iter->get("damage");
        if (damage_iter != nullptr) {
          if (damage_iter->kind() != io::DataKind::Object) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage must be an object for " + definition.name});
          }
          const io::DataValue* amount_iter = damage_iter->get("amount");
          const io::DataValue* range_iter = damage_iter->get("range");
          if (amount_iter == nullptr || !io::is_number_value(*amount_iter)) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage.amount must be a number for " + definition.name});
          }
          if (range_iter == nullptr || !io::is_number_value(*range_iter)) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage.range must be a number for " + definition.name});
          }
          f32 horizontal_knockback = 0.0F;
          f32 vertical_knockback = 0.0F;
          f32 sprint_knockback_bonus = 0.0F;
          const auto read_knockback_field =
              [&](const io::DataValue& source,
                  std::string_view field,
                  f32& value) -> std::optional<ItemRegistryError> {
            const io::DataValue* iter = source.get(field);
            if (iter == nullptr) {
              return std::nullopt;
            }
            if (!io::is_number_value(*iter)) {
              return ItemRegistryError{
                  "components.damage." + std::string{field} +
                  " must be a number for " + definition.name};
            }
            value = static_cast<f32>(iter->as_f64_or(value));
            return std::nullopt;
          };
          if (const std::optional<ItemRegistryError> error =
                  read_knockback_field(
                      *damage_iter,
                      "horizontal_knockback",
                      horizontal_knockback);
              error.has_value()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                error.value());
          }
          if (const std::optional<ItemRegistryError> error =
                  read_knockback_field(
                      *damage_iter,
                      "vertical_knockback",
                      vertical_knockback);
              error.has_value()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                error.value());
          }
          if (const std::optional<ItemRegistryError> error =
                  read_knockback_field(
                      *damage_iter,
                      "sprint_knockback_bonus",
                      sprint_knockback_bonus);
              error.has_value()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                error.value());
          }
          const io::DataValue* knockback_iter = components_iter->get("knockback");
          if (knockback_iter != nullptr) {
            if (knockback_iter->kind() != io::DataKind::Object) {
              return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                  {"components.knockback must be an object for " + definition.name});
            }
            const auto read_named_knockback_field =
                [&](std::string_view field,
                    f32& value) -> std::optional<ItemRegistryError> {
              const io::DataValue* iter = knockback_iter->get(field);
              if (iter == nullptr) {
                return std::nullopt;
              }
              if (!io::is_number_value(*iter)) {
                return ItemRegistryError{
                    "components.knockback." + std::string{field} +
                    " must be a number for " + definition.name};
              }
              value = static_cast<f32>(iter->as_f64_or(value));
              return std::nullopt;
            };
            if (const std::optional<ItemRegistryError> error =
                    read_named_knockback_field(
                        "horizontal",
                        horizontal_knockback);
                error.has_value()) {
              return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                  error.value());
            }
            if (const std::optional<ItemRegistryError> error =
                    read_named_knockback_field(
                        "vertical",
                        vertical_knockback);
                error.has_value()) {
              return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                  error.value());
            }
            if (const std::optional<ItemRegistryError> error =
                    read_named_knockback_field(
                        "sprint_bonus",
                        sprint_knockback_bonus);
                error.has_value()) {
              return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                  error.value());
            }
          }
          const f32 cooldown_seconds =
              damage_iter->get("cooldown_seconds") == nullptr
                  ? 0.0F
                  : static_cast<f32>(damage_iter->get("cooldown_seconds")->as_f64_or(0.0));
          definition.attack_data = ItemAttackData{
              static_cast<f32>(amount_iter->as_f64_or(0.0)),
              static_cast<f32>(range_iter->as_f64_or(0.0)),
              seconds_to_ticks_ceil(cooldown_seconds),
              horizontal_knockback,
              vertical_knockback,
              sprint_knockback_bonus};
        }
      }
      const io::DataValue* held_animations_iter = item.get("held_animations");
      if (held_animations_iter != nullptr) {
        const io::DataValue::Object* held_animations = held_animations_iter->object_if();
        if (held_animations == nullptr) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"held_animations must be an object for " + definition.name});
        }
        for (const auto& [action, animation] : *held_animations) {
          if (action.empty()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"held_animations action key must be non-empty for " + definition.name});
          }
          auto binding = parse_held_item_animation_binding(
              animation,
              definition.name,
              action);
          if (!binding) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                binding.error());
          }
          definition.held_animations.insert_or_assign(action, std::move(binding.value()));
        }
      }

      definitions.push_back(std::move(definition));
  }

  return Expected<std::vector<ItemDefinition>, ItemRegistryError>::success(std::move(definitions));
}

} // namespace arv::player
