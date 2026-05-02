#include "arv/player/item.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <nlohmann/json.hpp>

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
  if (!source.tool_category.empty()) {
    target.tool_category = std::move(source.tool_category);
    target.tool_break_speed_multiplier = source.tool_break_speed_multiplier;
  }
  target.generated_from_block = target.generated_from_block || source.generated_from_block;
}

} // namespace

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

ItemRegistry::Result ItemRegistry::build(std::vector<ItemDefinition> definitions) {
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
    record.tool_category = std::move(definition.tool_category);
    record.tool_break_speed_multiplier = definition.tool_break_speed_multiplier;

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

ItemRegistry::Result ItemRegistry::build_default_block_items(const voxel::BlockRegistry& registry) {
  return build_from_blocks_and_items(registry, {});
}

ItemRegistry::Result ItemRegistry::build_from_blocks_and_items(
    const voxel::BlockRegistry& registry,
    std::vector<ItemDefinition> explicit_definitions) {
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
    definition.display_name = block->name;
    definition.category = "blocks";
    definition.max_stack_size = 64;
    definition.places_block = block->name;
    definition.generated_from_block = true;
    definition.placement_variant = block->default_variant;
    definition.source_block = block->id;
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

Expected<std::vector<ItemDefinition>, ItemRegistryError> parse_item_definitions_json(
    std::string_view text) {
  try {
    const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end());
    const nlohmann::json* items = &root;
    if (root.is_object()) {
      const auto found = root.find("items");
      if (found == root.end()) {
        return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
            {"item definition JSON object must contain items array"});
      }
      items = &(*found);
    }
    if (!items->is_array()) {
      return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
          {"item definitions must be a JSON array"});
    }

    std::vector<ItemDefinition> definitions;
    definitions.reserve(items->size());
    for (const nlohmann::json& item : *items) {
      if (!item.is_object()) {
        return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
            {"item definition entry must be an object"});
      }

      const auto name_iter = item.find("name");
      if (name_iter == item.end() || !name_iter->is_string()) {
        return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
            {"item definition is missing string field name"});
      }

      ItemDefinition definition;
      definition.name = name_iter->get<std::string>();

      const auto display_iter = item.find("display_name");
      if (display_iter != item.end()) {
        definition.display_name = display_iter->get<std::string>();
      }
      const auto category_iter = item.find("category");
      if (category_iter != item.end()) {
        definition.category = category_iter->get<std::string>();
      }
      const auto stack_iter = item.find("max_stack_size");
      if (stack_iter != item.end()) {
        definition.max_stack_size = stack_iter->get<u16>();
      }
      const auto render_type_iter = item.find("render_type");
      if (render_type_iter != item.end()) {
        if (!render_type_iter->is_string()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"render_type must be a string for " + definition.name});
        }
        auto render_type = parse_item_render_type(render_type_iter->get<std::string>());
        if (!render_type) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(render_type.error());
        }
        definition.explicit_render_type = render_type.value();
      }
      const auto texture_iter = item.find("texture");
      if (texture_iter != item.end()) {
        definition.texture_asset = texture_iter->get<std::string>();
      }
      const auto model_iter = item.find("model");
      if (model_iter != item.end()) {
        definition.model_asset = model_iter->get<std::string>();
      }
      const auto places_block_iter = item.find("places_block");
      if (places_block_iter != item.end()) {
        definition.places_block = places_block_iter->get<std::string>();
      }
      const auto tool_iter = item.find("tool");
      if (tool_iter != item.end()) {
        if (!tool_iter->is_object()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"tool must be an object for " + definition.name});
        }
        const auto category_iter = tool_iter->find("category");
        if (category_iter != tool_iter->end()) {
          if (!category_iter->is_string()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"tool.category must be a string for " + definition.name});
          }
          definition.tool_category = category_iter->get<std::string>();
        }
        const auto speed_iter = tool_iter->find("break_speed_multiplier");
        if (speed_iter != tool_iter->end()) {
          definition.tool_break_speed_multiplier = speed_iter->get<f32>();
        }
      }

      definitions.push_back(std::move(definition));
    }

    return Expected<std::vector<ItemDefinition>, ItemRegistryError>::success(std::move(definitions));
  } catch (const nlohmann::json::exception& ex) {
    return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
        {"failed to parse item definitions JSON: " + std::string{ex.what()}});
  }
}

} // namespace arv::player
