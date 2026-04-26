#include "arv/player/item.hpp"

#include <algorithm>
#include <utility>

namespace arv::player {

namespace {

bool item_name_less(const ItemDefinition& lhs, const ItemDefinition& rhs) {
  if (lhs.name == AirItemName) {
    return true;
  }
  if (rhs.name == AirItemName) {
    return false;
  }
  return lhs.name < rhs.name;
}

} // namespace

ItemRegistry::Result ItemRegistry::build(std::vector<ItemDefinition> definitions) {
  if (definitions.empty()) {
    return Result::failure({"item registry is empty; required item arvane:air is missing"});
  }

  std::unordered_map<std::string, usize> definition_names;
  for (usize index = 0; index < definitions.size(); ++index) {
    const ItemDefinition& definition = definitions[index];
    if (definition.name.empty()) {
      return Result::failure({"item definition has empty name"});
    }
    if (definition.max_stack_size == 0U) {
      return Result::failure({"item max_stack_size must be non-zero for " + definition.name});
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
    record.max_stack_size = definition.max_stack_size;
    record.placement_variant = definition.placement_variant;

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

ItemRegistry::Result ItemRegistry::build_default_block_items(const voxel::BlockRegistry& registry) {
  std::vector<ItemDefinition> definitions;
  definitions.reserve(registry.block_count() + 2U);
  definitions.push_back(make_air_item_definition());
  definitions.push_back(make_missing_item_definition());

  for (u32 block_index = 0; block_index < registry.block_count(); ++block_index) {
    const voxel::BlockRecord* block = registry.block(voxel::BlockID{block_index});
    if (block == nullptr || block->name == voxel::AirBlockName) {
      continue;
    }

    ItemDefinition definition;
    definition.name = block->name;
    definition.display_name = block->name;
    definition.max_stack_size = 64;
    definition.placement_variant = block->default_variant;
    definitions.push_back(std::move(definition));
  }

  return build(std::move(definitions));
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

const ItemRecord* ItemRegistry::missing_item() const noexcept {
  return missing_item_id_.has_value() ? item(missing_item_id_.value()) : nullptr;
}

ItemDefinition make_air_item_definition() {
  ItemDefinition definition;
  definition.name = std::string{AirItemName};
  definition.display_name = std::string{AirItemName};
  definition.max_stack_size = 1;
  return definition;
}

ItemDefinition make_missing_item_definition() {
  ItemDefinition definition;
  definition.name = std::string{MissingItemName};
  definition.display_name = std::string{MissingItemName};
  definition.max_stack_size = 64;
  return definition;
}

} // namespace arv::player
