#include "arv/voxel/block.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::voxel {
namespace {

[[nodiscard]] Expected<RenderCategory, BlockRegistryError> parse_render_category(
    const std::string& value) {
  if (value == "none") {
    return Expected<RenderCategory, BlockRegistryError>::success(RenderCategory::None);
  }
  if (value == "opaque") {
    return Expected<RenderCategory, BlockRegistryError>::success(RenderCategory::Opaque);
  }
  if (value == "transparent") {
    return Expected<RenderCategory, BlockRegistryError>::success(RenderCategory::Transparent);
  }
  return Expected<RenderCategory, BlockRegistryError>::failure({"invalid render_category: " + value});
}

void read_bool_property(const nlohmann::json& object, const char* key, bool& value) {
  const auto found = object.find(key);
  if (found != object.end()) {
    value = found->get<bool>();
  }
}

void set_texture_slot(
    std::array<std::string, BlockFaceCount>& textures,
    BlockFace face,
    const std::string& asset_id) {
  textures[face_index(face)] = asset_id;
}

void set_side_textures(
    std::array<std::string, BlockFaceCount>& textures,
    const std::string& asset_id) {
  set_texture_slot(textures, BlockFace::PositiveX, asset_id);
  set_texture_slot(textures, BlockFace::NegativeX, asset_id);
  set_texture_slot(textures, BlockFace::PositiveZ, asset_id);
  set_texture_slot(textures, BlockFace::NegativeZ, asset_id);
}

[[nodiscard]] std::optional<BlockRegistryError> read_texture_value(
    const nlohmann::json& textures,
    BlockDefinition& definition,
    std::string_view field_name) {
  if (textures.is_string()) {
    definition.texture_assets.fill(textures.get<std::string>());
    return std::nullopt;
  }
  if (textures.is_array()) {
    if (textures.size() != BlockFaceCount) {
      return BlockRegistryError{
          std::string{field_name} + " array must contain exactly 6 entries for " + definition.name};
    }
    definition.texture_assets[face_index(BlockFace::PositiveY)] = textures[0].get<std::string>();
    definition.texture_assets[face_index(BlockFace::NegativeY)] = textures[1].get<std::string>();
    definition.texture_assets[face_index(BlockFace::PositiveZ)] = textures[2].get<std::string>();
    definition.texture_assets[face_index(BlockFace::NegativeZ)] = textures[3].get<std::string>();
    definition.texture_assets[face_index(BlockFace::NegativeX)] = textures[4].get<std::string>();
    definition.texture_assets[face_index(BlockFace::PositiveX)] = textures[5].get<std::string>();
    return std::nullopt;
  }
  if (!textures.is_object()) {
    return BlockRegistryError{std::string{field_name} + " must be a string, array, or object for " + definition.name};
  }

  const auto assign_if_present = [&](std::string_view key, auto assign) {
    const auto found = textures.find(std::string{key});
    if (found != textures.end()) {
      assign(found->get<std::string>());
    }
  };

  assign_if_present("all", [&](const std::string& value) {
    definition.texture_assets.fill(value);
  });
  assign_if_present("side", [&](const std::string& value) {
    set_side_textures(definition.texture_assets, value);
  });
  assign_if_present("top", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::PositiveY, value);
  });
  assign_if_present("up", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::PositiveY, value);
  });
  assign_if_present("bottom", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::NegativeY, value);
  });
  assign_if_present("down", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::NegativeY, value);
  });
  assign_if_present("east", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::PositiveX, value);
  });
  assign_if_present("west", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::NegativeX, value);
  });
  assign_if_present("south", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::PositiveZ, value);
  });
  assign_if_present("north", [&](const std::string& value) {
    set_texture_slot(definition.texture_assets, BlockFace::NegativeZ, value);
  });

  return std::nullopt;
}

[[nodiscard]] std::optional<BlockRegistryError> read_texture_references(
    const nlohmann::json& item,
    BlockDefinition& definition) {
  const auto texture_iter = item.find("texture");
  const auto textures_iter = item.find("textures");
  if (texture_iter == item.end() && textures_iter == item.end()) {
    return std::nullopt;
  }
  if (texture_iter != item.end() && textures_iter != item.end()) {
    return BlockRegistryError{"block defines both texture and textures for " + definition.name};
  }

  return read_texture_value(
      texture_iter != item.end() ? *texture_iter : *textures_iter,
      definition,
      texture_iter != item.end() ? "texture" : "textures");
}

} // namespace

BlockRegistry::Result BlockRegistry::build(std::vector<BlockDefinition> definitions) {
  if (definitions.empty()) {
    return Result::failure({"block registry is empty; required block arvane:air is missing"});
  }

  std::unordered_map<std::string, usize> definition_names;
  for (usize i = 0; i < definitions.size(); ++i) {
    const BlockDefinition& definition = definitions[i];
    if (definition.name.empty()) {
      return Result::failure({"block definition has empty name"});
    }
    if (!definition_names.emplace(definition.name, i).second) {
      return Result::failure({"duplicate block definition: " + definition.name});
    }
    if (auto state_error = validate_states(definition); state_error.has_value()) {
      return Result::failure({state_error.value()});
    }
  }

  const auto air_iter = definition_names.find(std::string{AirBlockName});
  if (air_iter == definition_names.end()) {
    return Result::failure({"required block arvane:air is missing"});
  }

  const BlockDefinition& air_definition = definitions[air_iter->second];
  if (auto error = validate_air_definition(air_definition); error.has_value()) {
    return Result::failure({error.value()});
  }

  std::sort(definitions.begin(), definitions.end(), [](const BlockDefinition& lhs, const BlockDefinition& rhs) {
    if (lhs.name == AirBlockName) {
      return true;
    }
    if (rhs.name == AirBlockName) {
      return false;
    }
    return lhs.name < rhs.name;
  });

  BlockRegistry registry;
  registry.blocks_.reserve(definitions.size());

  u32 next_block_id = 0;
  u32 next_variant_id = 0;
  for (const BlockDefinition& definition : definitions) {
    if (next_block_id == std::numeric_limits<u32>::max()) {
      return Result::failure({"block ID limit exceeded"});
    }

    const BlockID block_id{next_block_id++};
    BlockRecord block;
    block.id = block_id;
    block.name = definition.name;
    block.tags = definition.tags;
    block.properties = definition.properties;
    block.is_item = definition.is_item;

    std::vector<std::string> variant_names = expand_variant_names(definition);
    if (variant_names.empty()) {
      return Result::failure({"block has no variants after expansion: " + definition.name});
    }

    for (std::string& variant_name : variant_names) {
      if (next_variant_id == std::numeric_limits<u32>::max()) {
        return Result::failure({"block variant ID limit exceeded"});
      }

      const BlockVariantID variant_id{next_variant_id++};
      BlockVariantRecord variant;
      variant.id = variant_id;
      variant.block_id = block_id;
      variant.debug_name = std::move(variant_name);
      variant.properties = definition.properties;
      variant.render_category = definition.render_category;
      variant.hide_same_variant_faces = definition.hide_same_variant_faces;
      variant.texture_assets = definition.texture_assets;
      variant.renders_face.fill(definition.render_category != RenderCategory::None && definition.properties.full_cube);
      variant.occludes_face.fill(definition.properties.full_cube && definition.properties.opaque);
      if (definition.properties.collidable && definition.properties.full_cube) {
        variant.collision_boxes.push_back({{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}});
      }

      if (block.variants.empty()) {
        block.default_variant = variant_id;
      }
      block.variants.push_back(variant_id);
      registry.variant_name_to_id_.emplace(variant.debug_name, variant_id);
      registry.variants_.push_back(std::move(variant));
    }

    registry.block_name_to_id_.emplace(block.name, block_id);
    registry.blocks_.push_back(std::move(block));
  }

  if (registry.blocks_.empty() || registry.blocks_.front().id != AirBlockID ||
      registry.variants_.empty() || registry.variants_.front().id != AirBlockVariantID ||
      registry.blocks_.front().name != AirBlockName ||
      registry.variants_.front().debug_name != AirBlockName) {
    return Result::failure({"air block invariants were not preserved during registry build"});
  }

  return Result::success(std::move(registry));
}

const BlockRecord* BlockRegistry::block(BlockID id) const noexcept {
  if (id.value >= blocks_.size()) {
    return nullptr;
  }
  return &blocks_[id.value];
}

const BlockVariantRecord* BlockRegistry::variant(BlockVariantID id) const noexcept {
  if (id.value >= variants_.size()) {
    return nullptr;
  }
  return &variants_[id.value];
}

std::optional<BlockID> BlockRegistry::find_block(std::string_view name) const {
  const auto found = block_name_to_id_.find(std::string{name});
  if (found == block_name_to_id_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::optional<BlockVariantID> BlockRegistry::find_variant(std::string_view debug_name) const {
  const auto found = variant_name_to_id_.find(std::string{debug_name});
  if (found == variant_name_to_id_.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool BlockRegistry::has_tag(BlockID id, std::string_view tag) const {
  const BlockRecord* record = block(id);
  if (record == nullptr) {
    return false;
  }
  return std::find(record->tags.begin(), record->tags.end(), tag) != record->tags.end();
}

BlockID BlockRegistry::block_id(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record == nullptr ? AirBlockID : record->block_id;
}

std::string_view BlockRegistry::variant_name(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record == nullptr ? AirBlockName : std::string_view{record->debug_name};
}

std::span<const Aabb3f> BlockRegistry::collision_boxes(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  if (record == nullptr) {
    return {};
  }
  return record->collision_boxes;
}

bool BlockRegistry::is_replaceable(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record != nullptr && record->properties.replaceable;
}

bool BlockRegistry::should_emit_face(
    BlockVariantID self_id,
    BlockVariantID neighbor_id,
    BlockFace face) const noexcept {
  const BlockVariantRecord* self = variant(self_id);
  if (self == nullptr || self->render_category == RenderCategory::None ||
      !self->renders_face[face_index(face)]) {
    return false;
  }

  if (self_id == neighbor_id && self->hide_same_variant_faces) {
    return false;
  }

  const BlockVariantRecord* neighbor = variant(neighbor_id);
  if (neighbor == nullptr) {
    return true;
  }

  return !neighbor->occludes_face[face_index(opposite_face(face))];
}

BlockRenderData BlockRenderData::make_empty(usize variant_count) {
  BlockRenderData data;
  data.variants_.resize(variant_count);
  for (BlockVariantRenderData& variant : data.variants_) {
    for (BlockFace face : AllBlockFaces) {
      variant.faces[face_index(face)].uvs = default_cube_face_uvs(face);
    }
  }
  return data;
}

const BlockFaceRenderData* BlockRenderData::face(
    BlockVariantID variant_id,
    BlockFace block_face) const noexcept {
  if (variant_id.value >= variants_.size()) {
    return nullptr;
  }
  return &variants_[variant_id.value].faces[face_index(block_face)];
}

void BlockRenderData::set_face(
    BlockVariantID variant_id,
    BlockFace block_face,
    BlockFaceRenderData data) {
  if (variant_id.value >= variants_.size()) {
    return;
  }
  variants_[variant_id.value].faces[face_index(block_face)] = data;
}

std::optional<std::string> BlockRegistry::validate_states(const BlockDefinition& definition) {
  std::vector<std::string> names;
  names.reserve(definition.states.size());
  for (const StatePropertyDefinition& state : definition.states) {
    if (state.name.empty()) {
      return "block has state property with empty name: " + definition.name;
    }
    if (state.values.empty()) {
      return "block state property has no values: " + definition.name + "." + state.name;
    }
    if (std::find(names.begin(), names.end(), state.name) != names.end()) {
      return "duplicate state property: " + definition.name + "." + state.name;
    }
    names.push_back(state.name);

    std::vector<std::string> values;
    values.reserve(state.values.size());
    for (const std::string& value : state.values) {
      if (value.empty()) {
        return "block state property has empty value: " + definition.name + "." + state.name;
      }
      if (std::find(values.begin(), values.end(), value) != values.end()) {
        return "duplicate state value: " + definition.name + "." + state.name + "=" + value;
      }
      values.push_back(value);
    }
  }
  return std::nullopt;
}

std::optional<std::string> BlockRegistry::validate_air_definition(const BlockDefinition& definition) {
  if (!definition.states.empty()) {
    return "arvane:air must not define runtime states";
  }
  if (definition.render_category != RenderCategory::None) {
    return "arvane:air must not render geometry";
  }
  if (definition.properties.solid || definition.properties.collidable || definition.properties.opaque ||
      definition.properties.blocks_light || definition.properties.emits_light || definition.properties.fluid ||
      definition.properties.full_cube || definition.properties.ao_occluder) {
    return "arvane:air violates air behavior invariants";
  }
  return std::nullopt;
}

std::vector<std::string> BlockRegistry::expand_variant_names(const BlockDefinition& definition) {
  if (definition.states.empty()) {
    return {definition.name};
  }

  std::vector<std::string> names{definition.name + "["};
  for (usize state_index = 0; state_index < definition.states.size(); ++state_index) {
    const StatePropertyDefinition& state = definition.states[state_index];
    std::vector<std::string> expanded;
    expanded.reserve(names.size() * state.values.size());
    for (const std::string& prefix : names) {
      for (const std::string& value : state.values) {
        std::string next = prefix;
        if (state_index > 0) {
          next += ",";
        }
        next += state.name;
        next += "=";
        next += value;
        expanded.push_back(std::move(next));
      }
    }
    names = std::move(expanded);
  }

  for (std::string& name : names) {
    name += "]";
  }
  return names;
}

BlockDefinition make_air_definition() {
  BlockDefinition definition;
  definition.name = std::string{AirBlockName};
  definition.properties.replaceable = true;
  definition.render_category = RenderCategory::None;
  definition.is_item = false;
  return definition;
}

BlockDefinitionsResult parse_block_definitions_json(std::string_view text) {
  try {
    const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end());
    const nlohmann::json* blocks = &root;
    if (root.is_object()) {
      const auto found = root.find("blocks");
      if (found == root.end()) {
        return BlockDefinitionsResult::failure({"block definition JSON object must contain blocks array"});
      }
      blocks = &(*found);
    }
    if (!blocks->is_array()) {
      return BlockDefinitionsResult::failure({"block definitions must be a JSON array"});
    }

    std::vector<BlockDefinition> definitions;
    definitions.reserve(blocks->size());
    for (const nlohmann::json& item : *blocks) {
      if (!item.is_object()) {
        return BlockDefinitionsResult::failure({"block definition entry must be an object"});
      }

      const auto name_iter = item.find("name");
      if (name_iter == item.end() || !name_iter->is_string()) {
        return BlockDefinitionsResult::failure({"block definition is missing string field name"});
      }

      BlockDefinition definition;
      definition.name = name_iter->get<std::string>();

      const auto category_iter = item.find("render_category");
      if (category_iter != item.end()) {
        if (!category_iter->is_string()) {
          return BlockDefinitionsResult::failure({"render_category must be a string for " + definition.name});
        }
        auto parsed_category = parse_render_category(category_iter->get<std::string>());
        if (!parsed_category) {
          return BlockDefinitionsResult::failure(parsed_category.error());
        }
        definition.render_category = parsed_category.value();
      }

      const auto tags_iter = item.find("tags");
      if (tags_iter != item.end()) {
        if (!tags_iter->is_array()) {
          return BlockDefinitionsResult::failure({"tags must be an array for " + definition.name});
        }
        for (const nlohmann::json& tag : *tags_iter) {
          definition.tags.push_back(tag.get<std::string>());
        }
      }

      const auto properties_iter = item.find("properties");
      if (properties_iter != item.end()) {
        if (!properties_iter->is_object()) {
          return BlockDefinitionsResult::failure({"properties must be an object for " + definition.name});
        }
        read_bool_property(*properties_iter, "solid", definition.properties.solid);
        read_bool_property(*properties_iter, "collidable", definition.properties.collidable);
        read_bool_property(*properties_iter, "opaque", definition.properties.opaque);
        read_bool_property(*properties_iter, "blocks_light", definition.properties.blocks_light);
        read_bool_property(*properties_iter, "emits_light", definition.properties.emits_light);
        read_bool_property(*properties_iter, "replaceable", definition.properties.replaceable);
        read_bool_property(*properties_iter, "fluid", definition.properties.fluid);
        read_bool_property(*properties_iter, "full_cube", definition.properties.full_cube);
        read_bool_property(*properties_iter, "ao_occluder", definition.properties.ao_occluder);
        const auto emission_iter = properties_iter->find("emission_rgb");
        if (emission_iter != properties_iter->end()) {
          if (!emission_iter->is_array() || emission_iter->size() != 3U) {
            return BlockDefinitionsResult::failure(
                {"emission_rgb must be an array with 3 entries for " + definition.name});
          }
          definition.properties.emission_r = (*emission_iter)[0].get<u8>();
          definition.properties.emission_g = (*emission_iter)[1].get<u8>();
          definition.properties.emission_b = (*emission_iter)[2].get<u8>();
          definition.properties.emits_light =
              definition.properties.emission_r != 0 || definition.properties.emission_g != 0 ||
              definition.properties.emission_b != 0;
        }
      }

      const auto hide_iter = item.find("hide_same_variant_faces");
      if (hide_iter != item.end()) {
        definition.hide_same_variant_faces = hide_iter->get<bool>();
      }

      const auto is_item_iter = item.find("is_item");
      if (is_item_iter != item.end()) {
        definition.is_item = is_item_iter->get<bool>();
      }

      if (auto texture_error = read_texture_references(item, definition); texture_error.has_value()) {
        return BlockDefinitionsResult::failure(texture_error.value());
      }

      const auto states_iter = item.find("states");
      if (states_iter != item.end()) {
        if (!states_iter->is_object()) {
          return BlockDefinitionsResult::failure({"states must be an object for " + definition.name});
        }
        for (const auto& [state_name, state_values] : states_iter->items()) {
          if (!state_values.is_array()) {
            return BlockDefinitionsResult::failure({"state values must be arrays for " + definition.name});
          }
          StatePropertyDefinition state;
          state.name = state_name;
          for (const nlohmann::json& value : state_values) {
            state.values.push_back(value.get<std::string>());
          }
          definition.states.push_back(std::move(state));
        }
      }

      definitions.push_back(std::move(definition));
    }

    return BlockDefinitionsResult::success(std::move(definitions));
  } catch (const nlohmann::json::exception& ex) {
    return BlockDefinitionsResult::failure({"failed to parse block definitions JSON: " + std::string{ex.what()}});
  }
}

} // namespace arv::voxel
