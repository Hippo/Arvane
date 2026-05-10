#include "arv/player/item.hpp"

#include "arv/core/time.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

[[nodiscard]] u32 cooldown_ticks_from_seconds(f32 seconds) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0F) {
    return 0U;
  }
  const f32 ticks = std::ceil(seconds * static_cast<f32>(DefaultBaseTps));
  if (ticks >= static_cast<f32>(std::numeric_limits<u32>::max())) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(ticks);
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
    const nlohmann::json& animation,
    std::string_view item_name,
    std::string_view action) {
  const std::string context =
      "held_animations." + std::string{action} + " for " + std::string{item_name};
  HeldItemAnimationBinding binding;

  if (animation.is_null()) {
    binding.selection = HeldItemAnimationSelection::Disabled;
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
  }
  if (animation.is_string()) {
    binding.selection = HeldItemAnimationSelection::Named;
    binding.animation = animation.get<std::string>();
    if (binding.animation.empty()) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + " animation must be non-empty"});
    }
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
  }
  if (!animation.is_object()) {
    return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
        {context + " must be a string, null, or object"});
  }

  if (const auto animation_iter = animation.find("animation");
      animation_iter != animation.end()) {
    if (animation_iter->is_null()) {
      binding.selection = HeldItemAnimationSelection::Disabled;
    } else if (animation_iter->is_string()) {
      binding.selection = HeldItemAnimationSelection::Named;
      binding.animation = animation_iter->get<std::string>();
      if (binding.animation.empty()) {
        return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
            {context + ".animation must be non-empty"});
      }
    } else {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + ".animation must be a string or null"});
    }
  }

  if (const auto play_when_iter = animation.find("play_when");
      play_when_iter != animation.end()) {
    if (!play_when_iter->is_string()) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(
          {context + ".play_when must be a string"});
    }
    auto condition = parse_held_item_animation_play_condition(
        play_when_iter->get<std::string>(),
        context);
    if (!condition) {
      return Expected<HeldItemAnimationBinding, ItemRegistryError>::failure(condition.error());
    }
    binding.play_condition = condition.value();
  }

  return Expected<HeldItemAnimationBinding, ItemRegistryError>::success(std::move(binding));
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
    if (!std::isfinite(definition.buoyancy)) {
      return Result::failure({"item buoyancy must be finite for " + definition.name});
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
      const auto buoyancy_iter = item.find("buoyancy");
      if (buoyancy_iter != item.end()) {
        if (!buoyancy_iter->is_number()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"buoyancy must be a number for " + definition.name});
        }
        definition.buoyancy = buoyancy_iter->get<f32>();
        definition.buoyancy_set = true;
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
      const auto components_iter = item.find("components");
      if (components_iter != item.end()) {
        if (!components_iter->is_object()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"components must be an object for " + definition.name});
        }
        const auto damage_iter = components_iter->find("damage");
        if (damage_iter != components_iter->end()) {
          if (!damage_iter->is_object()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage must be an object for " + definition.name});
          }
          const auto amount_iter = damage_iter->find("amount");
          const auto range_iter = damage_iter->find("range");
          if (amount_iter == damage_iter->end() || !amount_iter->is_number()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage.amount must be a number for " + definition.name});
          }
          if (range_iter == damage_iter->end() || !range_iter->is_number()) {
            return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                {"components.damage.range must be a number for " + definition.name});
          }
          f32 horizontal_knockback = 0.0F;
          f32 vertical_knockback = 0.0F;
          f32 sprint_knockback_bonus = 0.0F;
          const auto read_knockback_field =
              [&](const nlohmann::json& source,
                  std::string_view field,
                  f32& value) -> std::optional<ItemRegistryError> {
            const auto iter = source.find(std::string{field});
            if (iter == source.end()) {
              return std::nullopt;
            }
            if (!iter->is_number()) {
              return ItemRegistryError{
                  "components.damage." + std::string{field} +
                  " must be a number for " + definition.name};
            }
            value = iter->get<f32>();
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
          const auto knockback_iter = components_iter->find("knockback");
          if (knockback_iter != components_iter->end()) {
            if (!knockback_iter->is_object()) {
              return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
                  {"components.knockback must be an object for " + definition.name});
            }
            const auto read_named_knockback_field =
                [&](std::string_view field,
                    f32& value) -> std::optional<ItemRegistryError> {
              const auto iter = knockback_iter->find(std::string{field});
              if (iter == knockback_iter->end()) {
                return std::nullopt;
              }
              if (!iter->is_number()) {
                return ItemRegistryError{
                    "components.knockback." + std::string{field} +
                    " must be a number for " + definition.name};
              }
              value = iter->get<f32>();
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
              damage_iter->value("cooldown_seconds", 0.0F);
          definition.attack_data = ItemAttackData{
              amount_iter->get<f32>(),
              range_iter->get<f32>(),
              cooldown_ticks_from_seconds(cooldown_seconds),
              horizontal_knockback,
              vertical_knockback,
              sprint_knockback_bonus};
        }
      }
      const auto held_animations_iter = item.find("held_animations");
      if (held_animations_iter != item.end()) {
        if (!held_animations_iter->is_object()) {
          return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
              {"held_animations must be an object for " + definition.name});
        }
        for (const auto& [action, animation] : held_animations_iter->items()) {
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
  } catch (const nlohmann::json::exception& ex) {
    return Expected<std::vector<ItemDefinition>, ItemRegistryError>::failure(
        {"failed to parse item definitions JSON: " + std::string{ex.what()}});
  }
}

} // namespace arv::player
