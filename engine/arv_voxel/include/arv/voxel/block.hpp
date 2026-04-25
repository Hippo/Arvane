#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/types.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace arv::voxel {

struct BlockTag {};
struct BlockVariantTag {};

using BlockID = ID<BlockTag, u32>;
using BlockVariantID = ID<BlockVariantTag, u32>;

inline constexpr BlockID AirBlockID{0};
inline constexpr BlockVariantID AirBlockVariantID{0};
inline constexpr std::string_view AirBlockName = "arvane:air";

enum class BlockFace : u8 {
  PositiveX = 0,
  NegativeX = 1,
  PositiveY = 2,
  NegativeY = 3,
  PositiveZ = 4,
  NegativeZ = 5,
};

inline constexpr usize BlockFaceCount = 6;

enum class RenderCategory : u8 {
  None,
  Opaque,
  Transparent,
};

struct BlockRegistryError {
  std::string message{};
};

struct BlockProperties {
  bool solid{};
  bool collidable{};
  bool opaque{};
  bool blocks_light{};
  bool emits_light{};
  bool replaceable{};
  bool fluid{};
  bool full_cube{};
  u8 emission_r{};
  u8 emission_g{};
  u8 emission_b{};
};

struct StatePropertyDefinition {
  std::string name{};
  std::vector<std::string> values{};
};

struct BlockDefinition {
  std::string name{};
  std::vector<std::string> tags{};
  BlockProperties properties{};
  RenderCategory render_category{RenderCategory::None};
  bool hide_same_variant_faces{true};
  std::vector<StatePropertyDefinition> states{};
};

struct BlockRecord {
  BlockID id{};
  std::string name{};
  std::vector<std::string> tags{};
  BlockProperties properties{};
  BlockVariantID default_variant{};
  std::vector<BlockVariantID> variants{};
};

struct BlockVariantRecord {
  BlockVariantID id{};
  BlockID block_id{};
  std::string debug_name{};
  BlockProperties properties{};
  RenderCategory render_category{RenderCategory::None};
  bool hide_same_variant_faces{};
  std::array<bool, BlockFaceCount> renders_face{};
  std::array<bool, BlockFaceCount> occludes_face{};
};

using BlockDefinitionsResult = Expected<std::vector<BlockDefinition>, BlockRegistryError>;

[[nodiscard]] constexpr usize face_index(BlockFace face) noexcept {
  return static_cast<usize>(face);
}

[[nodiscard]] constexpr BlockFace opposite_face(BlockFace face) noexcept {
  switch (face) {
  case BlockFace::PositiveX:
    return BlockFace::NegativeX;
  case BlockFace::NegativeX:
    return BlockFace::PositiveX;
  case BlockFace::PositiveY:
    return BlockFace::NegativeY;
  case BlockFace::NegativeY:
    return BlockFace::PositiveY;
  case BlockFace::PositiveZ:
    return BlockFace::NegativeZ;
  case BlockFace::NegativeZ:
    return BlockFace::PositiveZ;
  }
  return BlockFace::NegativeX;
}

class BlockRegistry {
public:
  using Result = Expected<BlockRegistry, BlockRegistryError>;

  [[nodiscard]] static Result build(std::vector<BlockDefinition> definitions) {
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
        variant.renders_face.fill(definition.render_category != RenderCategory::None && definition.properties.full_cube);
        variant.occludes_face.fill(definition.properties.full_cube && definition.properties.opaque);

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

  [[nodiscard]] usize block_count() const noexcept {
    return blocks_.size();
  }

  [[nodiscard]] usize variant_count() const noexcept {
    return variants_.size();
  }

  [[nodiscard]] BlockID air_block_id() const noexcept {
    return AirBlockID;
  }

  [[nodiscard]] BlockVariantID air_variant_id() const noexcept {
    return AirBlockVariantID;
  }

  [[nodiscard]] bool is_air(BlockVariantID variant_id) const noexcept {
    return variant_id == AirBlockVariantID;
  }

  [[nodiscard]] const BlockRecord* block(BlockID id) const noexcept {
    if (id.value >= blocks_.size()) {
      return nullptr;
    }
    return &blocks_[id.value];
  }

  [[nodiscard]] const BlockVariantRecord* variant(BlockVariantID id) const noexcept {
    if (id.value >= variants_.size()) {
      return nullptr;
    }
    return &variants_[id.value];
  }

  [[nodiscard]] std::optional<BlockID> find_block(std::string_view name) const {
    const auto found = block_name_to_id_.find(std::string{name});
    if (found == block_name_to_id_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  [[nodiscard]] std::optional<BlockVariantID> find_variant(std::string_view debug_name) const {
    const auto found = variant_name_to_id_.find(std::string{debug_name});
    if (found == variant_name_to_id_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  [[nodiscard]] bool has_tag(BlockID id, std::string_view tag) const {
    const BlockRecord* record = block(id);
    if (record == nullptr) {
      return false;
    }
    return std::find(record->tags.begin(), record->tags.end(), tag) != record->tags.end();
  }

  [[nodiscard]] bool should_emit_face(
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

private:
  [[nodiscard]] static std::optional<std::string> validate_states(const BlockDefinition& definition) {
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

  [[nodiscard]] static std::optional<std::string> validate_air_definition(const BlockDefinition& definition) {
    if (!definition.states.empty()) {
      return "arvane:air must not define runtime states";
    }
    if (definition.render_category != RenderCategory::None) {
      return "arvane:air must not render geometry";
    }
    if (definition.properties.solid || definition.properties.collidable || definition.properties.opaque ||
        definition.properties.blocks_light || definition.properties.emits_light || definition.properties.fluid ||
        definition.properties.full_cube) {
      return "arvane:air violates air behavior invariants";
    }
    return std::nullopt;
  }

  [[nodiscard]] static std::vector<std::string> expand_variant_names(const BlockDefinition& definition) {
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

  std::vector<BlockRecord> blocks_;
  std::vector<BlockVariantRecord> variants_;
  std::unordered_map<std::string, BlockID> block_name_to_id_;
  std::unordered_map<std::string, BlockVariantID> variant_name_to_id_;
};

[[nodiscard]] inline BlockDefinition make_air_definition() {
  BlockDefinition definition;
  definition.name = std::string{AirBlockName};
  definition.properties.replaceable = true;
  definition.render_category = RenderCategory::None;
  return definition;
}

[[nodiscard]] inline BlockDefinition make_full_cube_definition(
    std::string name,
    RenderCategory category = RenderCategory::Opaque) {
  BlockDefinition definition;
  definition.name = std::move(name);
  definition.properties.solid = category == RenderCategory::Opaque;
  definition.properties.collidable = category == RenderCategory::Opaque;
  definition.properties.opaque = category == RenderCategory::Opaque;
  definition.properties.blocks_light = category == RenderCategory::Opaque;
  definition.properties.full_cube = true;
  definition.render_category = category;
  return definition;
}

namespace detail {

[[nodiscard]] inline Expected<RenderCategory, BlockRegistryError> parse_render_category(
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

inline void read_bool_property(const nlohmann::json& object, const char* key, bool& value) {
  const auto found = object.find(key);
  if (found != object.end()) {
    value = found->get<bool>();
  }
}

} // namespace detail

[[nodiscard]] inline BlockDefinitionsResult parse_block_definitions_json(std::string_view text) {
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
        auto parsed_category = detail::parse_render_category(category_iter->get<std::string>());
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
        detail::read_bool_property(*properties_iter, "solid", definition.properties.solid);
        detail::read_bool_property(*properties_iter, "collidable", definition.properties.collidable);
        detail::read_bool_property(*properties_iter, "opaque", definition.properties.opaque);
        detail::read_bool_property(*properties_iter, "blocks_light", definition.properties.blocks_light);
        detail::read_bool_property(*properties_iter, "emits_light", definition.properties.emits_light);
        detail::read_bool_property(*properties_iter, "replaceable", definition.properties.replaceable);
        detail::read_bool_property(*properties_iter, "fluid", definition.properties.fluid);
        detail::read_bool_property(*properties_iter, "full_cube", definition.properties.full_cube);
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
