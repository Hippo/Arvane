#include "arv/voxel/block.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

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
  if (value == "cutout") {
    return Expected<RenderCategory, BlockRegistryError>::success(RenderCategory::Cutout);
  }
  if (value == "transparent") {
    return Expected<RenderCategory, BlockRegistryError>::success(RenderCategory::Transparent);
  }
  return Expected<RenderCategory, BlockRegistryError>::failure({"invalid render_category: " + value});
}

[[nodiscard]] bool is_integer(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::I64 || value.kind() == io::DataKind::U64;
}

[[nodiscard]] bool is_number(const io::DataValue& value) noexcept {
  return is_integer(value) || value.kind() == io::DataKind::F64;
}

void read_bool_property(const io::DataValue& object, const char* key, bool& value) {
  const io::DataValue* found = object.get(key);
  if (found != nullptr && found->kind() == io::DataKind::Bool) {
    value = found->as_bool_or(value);
  }
}

void read_f32_property(const io::DataValue& object, const char* key, f32& value) {
  const io::DataValue* found = object.get(key);
  if (found != nullptr && is_number(*found)) {
    value = static_cast<f32>(found->as_f64_or(value));
  }
}

void set_texture_slot(
    std::array<std::string, BlockFaceCount>& textures,
    BlockFace face,
    const std::string& asset_id) {
  textures[face_index(face)] = normalize_block_texture_asset(asset_id);
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
    const io::DataValue& textures,
    BlockDefinition& definition,
    std::string_view field_name) {
  if (textures.kind() == io::DataKind::String) {
    definition.texture_assets.fill(
        normalize_block_texture_asset(textures.as_string_or({})));
    return std::nullopt;
  }
  if (const io::DataValue::Array* texture_array = textures.array_if();
      texture_array != nullptr) {
    if (texture_array->size() != BlockFaceCount) {
      return BlockRegistryError{
          std::string{field_name} + " array must contain exactly 6 entries for " + definition.name};
    }
    definition.texture_assets[face_index(BlockFace::PositiveY)] =
        normalize_block_texture_asset((*texture_array)[0].as_string_or({}));
    definition.texture_assets[face_index(BlockFace::NegativeY)] =
        normalize_block_texture_asset((*texture_array)[1].as_string_or({}));
    definition.texture_assets[face_index(BlockFace::PositiveZ)] =
        normalize_block_texture_asset((*texture_array)[2].as_string_or({}));
    definition.texture_assets[face_index(BlockFace::NegativeZ)] =
        normalize_block_texture_asset((*texture_array)[3].as_string_or({}));
    definition.texture_assets[face_index(BlockFace::NegativeX)] =
        normalize_block_texture_asset((*texture_array)[4].as_string_or({}));
    definition.texture_assets[face_index(BlockFace::PositiveX)] =
        normalize_block_texture_asset((*texture_array)[5].as_string_or({}));
    return std::nullopt;
  }
  if (textures.kind() != io::DataKind::Object) {
    return BlockRegistryError{std::string{field_name} + " must be a string, array, or object for " + definition.name};
  }

  const auto assign_if_present = [&](std::string_view key, auto assign) {
    const io::DataValue* found = textures.get(key);
    if (found != nullptr) {
      assign(std::string{found->as_string_or({})});
    }
  };

  assign_if_present("all", [&](const std::string& value) {
    definition.texture_assets.fill(normalize_block_texture_asset(value));
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
    const io::DataValue& item,
    BlockDefinition& definition) {
  const io::DataValue* texture_iter = item.get("texture");
  const io::DataValue* textures_iter = item.get("textures");
  if (texture_iter == nullptr && textures_iter == nullptr) {
    return std::nullopt;
  }
  if (texture_iter != nullptr && textures_iter != nullptr) {
    return BlockRegistryError{"block defines both texture and textures for " + definition.name};
  }

  return read_texture_value(
      texture_iter != nullptr ? *texture_iter : *textures_iter,
      definition,
      texture_iter != nullptr ? "texture" : "textures");
}

[[nodiscard]] std::optional<BlockRegistryError> read_model_reference(
    const io::DataValue& model,
    BlockDefinition& definition) {
  if (model.kind() == io::DataKind::String) {
    definition.model.asset = normalize_block_model_asset(model.as_string_or({}));
    if (definition.model.asset.empty()) {
      return BlockRegistryError{"model must not be empty for " + definition.name};
    }
    return std::nullopt;
  }

  if (model.kind() != io::DataKind::Object) {
    return BlockRegistryError{"model must be a string or object for " + definition.name};
  }

  const io::DataValue* path = model.get("path");
  if (path == nullptr || path->kind() != io::DataKind::String) {
    return BlockRegistryError{"model object requires string path for " + definition.name};
  }
  definition.model.asset = normalize_block_model_asset(path->as_string_or({}));
  if (definition.model.asset.empty()) {
    return BlockRegistryError{"model path must not be empty for " + definition.name};
  }

  if (const io::DataValue* textures = model.get("textures"); textures != nullptr) {
    const io::DataValue::Array* texture_array = textures->array_if();
    if (texture_array == nullptr) {
      return BlockRegistryError{"model.textures must be an array for " + definition.name};
    }
    definition.model.texture_inputs.reserve(texture_array->size());
    for (const io::DataValue& texture : *texture_array) {
      if (texture.kind() != io::DataKind::String) {
        return BlockRegistryError{"model.textures entries must be strings for " + definition.name};
      }
      definition.model.texture_inputs.push_back(
          normalize_block_texture_asset(texture.as_string_or({})));
    }
  }

  return std::nullopt;
}

[[nodiscard]] Expected<Vec3f, BlockRegistryError> read_vec3f(
    const io::DataValue& node,
    std::string_view field) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 3U) {
    return Expected<Vec3f, BlockRegistryError>::failure(
        {std::string{field} + " must be a 3-element array"});
  }
  for (const io::DataValue& value : *array) {
    if (!is_number(value)) {
      return Expected<Vec3f, BlockRegistryError>::failure(
          {std::string{field} + " entries must be numeric"});
    }
  }
  Vec3f result{
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0))};
  if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z)) {
    return Expected<Vec3f, BlockRegistryError>::failure(
        {std::string{field} + " entries must be finite"});
  }
  return Expected<Vec3f, BlockRegistryError>::success(result);
}

[[nodiscard]] Expected<Vec2i, BlockRegistryError> read_vec2i(
    const io::DataValue& node,
    std::string_view field) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 2U) {
    return Expected<Vec2i, BlockRegistryError>::failure(
        {std::string{field} + " must be a 2-element array"});
  }
  for (const io::DataValue& value : *array) {
    if (!is_number(value)) {
      return Expected<Vec2i, BlockRegistryError>::failure(
          {std::string{field} + " entries must be numeric"});
    }
  }
  return Expected<Vec2i, BlockRegistryError>::success({
      static_cast<i32>((*array)[0].as_i64_or(0)),
      static_cast<i32>((*array)[1].as_i64_or(0))});
}

[[nodiscard]] Expected<Vec4i, BlockRegistryError> read_vec4i(
    const io::DataValue& node,
    std::string_view field) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 4U) {
    return Expected<Vec4i, BlockRegistryError>::failure(
        {std::string{field} + " must be a 4-element array"});
  }
  for (const io::DataValue& value : *array) {
    if (!is_number(value)) {
      return Expected<Vec4i, BlockRegistryError>::failure(
          {std::string{field} + " entries must be numeric"});
    }
  }
  return Expected<Vec4i, BlockRegistryError>::success({
      static_cast<i32>((*array)[0].as_i64_or(0)),
      static_cast<i32>((*array)[1].as_i64_or(0)),
      static_cast<i32>((*array)[2].as_i64_or(0)),
      static_cast<i32>((*array)[3].as_i64_or(0))});
}

[[nodiscard]] std::optional<BlockFace> block_face_from_name(std::string_view name) noexcept {
  if (name == "east") {
    return BlockFace::PositiveX;
  }
  if (name == "west") {
    return BlockFace::NegativeX;
  }
  if (name == "up") {
    return BlockFace::PositiveY;
  }
  if (name == "down") {
    return BlockFace::NegativeY;
  }
  if (name == "south") {
    return BlockFace::PositiveZ;
  }
  if (name == "north") {
    return BlockFace::NegativeZ;
  }
  return std::nullopt;
}

[[nodiscard]] bool is_block_model_input_reference(std::string_view texture) noexcept {
  if (texture.size() < 2U || texture.front() != '$') {
    return false;
  }
  return std::all_of(texture.begin() + 1, texture.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
  });
}

[[nodiscard]] Expected<std::string, BlockRegistryError> resolve_block_model_texture(
    std::string_view texture,
    std::span<const std::string> texture_inputs) {
  if (!is_block_model_input_reference(texture)) {
    if (!texture.empty() && texture.front() == '$') {
      return Expected<std::string, BlockRegistryError>::failure(
          {"invalid block model texture input reference: " + std::string{texture}});
    }
    return Expected<std::string, BlockRegistryError>::success(
        normalize_block_texture_asset(texture));
  }

  usize input_index = 0;
  for (const char c : texture.substr(1)) {
    input_index = input_index * 10U + static_cast<usize>(c - '0');
  }
  if (input_index >= texture_inputs.size()) {
    return Expected<std::string, BlockRegistryError>::failure(
        {"model texture input " + std::string{texture} + " is out of range"});
  }
  return Expected<std::string, BlockRegistryError>::success(texture_inputs[input_index]);
}

[[nodiscard]] Expected<AuthoredModelFaceDefinition, BlockRegistryError> read_authored_model_face(
    const io::DataValue& node,
    usize texture_count) {
  if (node.kind() != io::DataKind::Object) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
        {"model face entry must be an object"});
  }

  AuthoredModelFaceDefinition face;
  const io::DataValue* texture = node.get("texture");
  if (texture == nullptr) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
        {"model face entry is missing texture"});
  }
  if (!is_integer(*texture)) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
        {"model face texture must be an integer"});
  }
  face.texture_index = static_cast<i32>(texture->as_i64_or(0));
  if (face.texture_index < 0) {
    if (face.texture_index != -1) {
      return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
          {"model face texture must be -1 or a non-negative texture index"});
    }
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::success(face);
  }
  if (static_cast<usize>(face.texture_index) >= texture_count) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
        {"model face references missing texture index"});
  }

  const io::DataValue* uv = node.get("uv");
  if (uv == nullptr) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(
        {"model face entry is missing uv"});
  }
  auto parsed_uv = read_vec4i(*uv, "model face uv");
  if (!parsed_uv) {
    return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::failure(parsed_uv.error());
  }
  face.uv = parsed_uv.value();
  return Expected<AuthoredModelFaceDefinition, BlockRegistryError>::success(face);
}

[[nodiscard]] std::optional<BlockRegistryError> validate_block_model_box(
    const BlockModelBoxDefinition& box) {
  const auto finite = [](f32 value) noexcept {
    return std::isfinite(value);
  };
  const auto in_range = [](f32 value) noexcept {
    return value >= 0.0F && value <= 16.0F;
  };
  if (!finite(box.min.x) || !finite(box.min.y) || !finite(box.min.z) ||
      !finite(box.max.x) || !finite(box.max.y) || !finite(box.max.z) ||
      !finite(box.inflate)) {
    return BlockRegistryError{"block model box coordinates and inflate must be finite"};
  }
  if (!in_range(box.min.x) || !in_range(box.min.y) || !in_range(box.min.z) ||
      !in_range(box.max.x) || !in_range(box.max.y) || !in_range(box.max.z)) {
    return BlockRegistryError{"block model box coordinates must be in the range 0..16"};
  }
  if (box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z) {
    return BlockRegistryError{"block model box min must be less than or equal to max"};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BlockRegistryError> reject_removed_model_fields(
    const io::DataValue& object,
    std::string_view context) {
  for (std::string_view field : {
           "skeleton",
           "bones",
           "animations",
           "animation",
           "keyframes",
           "geometry",
           "cubes",
       }) {
    if (object.get(field) != nullptr) {
      return BlockRegistryError{
          std::string{context} +
          " contains removed entity model field '" +
          std::string{field} +
          "'; models are geometry-only boxes and animations are separate assets"};
    }
  }
  return std::nullopt;
}

struct ExpandedVariantDefinition {
  std::string debug_name{};
  std::vector<BlockStateValue> state_values{};
};

[[nodiscard]] std::optional<std::string_view> state_value(
    std::span<const BlockStateValue> state_values,
    std::string_view name) noexcept {
  for (const BlockStateValue& state : state_values) {
    if (state.name == name) {
      return std::string_view{state.value};
    }
  }
  return std::nullopt;
}

[[nodiscard]] BlockModelTransform model_transform_for_state_values(
    std::span<const BlockStateValue> state_values) noexcept {
  BlockModelTransform transform;
  if (const std::optional<std::string_view> facing = state_value(state_values, "facing");
      facing.has_value()) {
    if (*facing == "east") {
      transform.quarter_turns_y = 1;
    } else if (*facing == "south") {
      transform.quarter_turns_y = 2;
    } else if (*facing == "west") {
      transform.quarter_turns_y = 3;
    } else {
      transform.quarter_turns_y = 0;
    }
  }
  if (const std::optional<std::string_view> half = state_value(state_values, "half");
      half.has_value()) {
    transform.mirror_y = *half == "top";
  }
  return transform;
}

[[nodiscard]] std::vector<ExpandedVariantDefinition> expand_variants(
    const BlockDefinition& definition) {
  if (definition.states.empty()) {
    return {{definition.name, {}}};
  }

  std::vector<ExpandedVariantDefinition> variants;
  variants.push_back({definition.name + "[", {}});
  for (usize state_index = 0; state_index < definition.states.size(); ++state_index) {
    const StatePropertyDefinition& state = definition.states[state_index];
    std::vector<ExpandedVariantDefinition> expanded;
    expanded.reserve(variants.size() * state.values.size());
    for (const ExpandedVariantDefinition& prefix : variants) {
      for (const std::string& value : state.values) {
        ExpandedVariantDefinition next;
        next.debug_name = prefix.debug_name;
        if (state_index > 0) {
          next.debug_name += ",";
        }
        next.debug_name += state.name;
        next.debug_name += "=";
        next.debug_name += value;
        next.state_values = prefix.state_values;
        next.state_values.push_back({state.name, value});
        expanded.push_back(std::move(next));
      }
    }
    variants = std::move(expanded);
  }

  for (ExpandedVariantDefinition& variant : variants) {
    variant.debug_name += "]";
  }
  return variants;
}

[[nodiscard]] Expected<std::string, BlockRegistryError> read_state_value_string(
    const io::DataValue& value,
    std::string_view field) {
  if (value.kind() == io::DataKind::String) {
    return Expected<std::string, BlockRegistryError>::success(
        std::string{value.as_string_or({})});
  }
  if (value.kind() == io::DataKind::Bool) {
    return Expected<std::string, BlockRegistryError>::success(
        value.as_bool_or(false) ? "true" : "false");
  }
  if (value.kind() == io::DataKind::I64) {
    return Expected<std::string, BlockRegistryError>::success(
        std::to_string(value.as_i64_or(0)));
  }
  if (value.kind() == io::DataKind::U64) {
    return Expected<std::string, BlockRegistryError>::success(
        std::to_string(value.as_u64_or(0)));
  }
  return Expected<std::string, BlockRegistryError>::failure(
      {std::string{field} + " values must be strings, booleans, or integers"});
}

[[nodiscard]] Vec3f derive_center_origin(
    std::span<const AuthoredModelBoxDefinition> boxes) noexcept {
  if (boxes.empty()) {
    return {8.0F, 8.0F, 8.0F};
  }

  Vec3f min = boxes.front().min;
  Vec3f max = boxes.front().max;
  for (const AuthoredModelBoxDefinition& box : boxes) {
    min.x = std::min(min.x, box.min.x);
    min.y = std::min(min.y, box.min.y);
    min.z = std::min(min.z, box.min.z);
    max.x = std::max(max.x, box.max.x);
    max.y = std::max(max.y, box.max.y);
    max.z = std::max(max.z, box.max.z);
  }

  return {
      (min.x + max.x) * 0.5F,
      (min.y + max.y) * 0.5F,
      (min.z + max.z) * 0.5F};
}

[[nodiscard]] Vec3f transform_model_point(
    Vec3f point,
    Vec3f center_origin,
    BlockModelTransform transform) noexcept {
  if (transform.mirror_y) {
    point.y = center_origin.y * 2.0F - point.y;
  }

  Vec3f local = point - center_origin;
  switch ((transform.quarter_turns_y % 4 + 4) % 4) {
    case 1:
      local = {-local.z, local.y, local.x};
      break;
    case 2:
      local = {-local.x, local.y, -local.z};
      break;
    case 3:
      local = {local.z, local.y, -local.x};
      break;
    default:
      break;
  }
  return center_origin + local;
}

} // namespace

BlockBehaviorRegistry::Result BlockBehaviorRegistry::register_handler(
    std::string id,
    BlockBehaviorHandler handler) {
  if (id.empty()) {
    return Result::failure({"block behavior handler id is empty"});
  }
  if (contains(id)) {
    return Result::failure({"duplicate block behavior handler id: " + id});
  }
  handlers_.push_back({std::move(id), std::move(handler)});
  return Result::success();
}

const BlockBehaviorHandler* BlockBehaviorRegistry::find(std::string_view id) const noexcept {
  for (const auto& [handler_id, handler] : handlers_) {
    if (handler_id == id) {
      return &handler;
    }
  }
  return nullptr;
}

bool BlockBehaviorRegistry::contains(std::string_view id) const noexcept {
  return find(id) != nullptr;
}

BlockRegistry::Result BlockRegistry::build(
    std::vector<BlockDefinition> definitions,
    BlockRegistryBuildOptions options) {
  if (definitions.empty()) {
    return Result::failure({"block registry is empty; required block arvane:air is missing"});
  }

  HashMap<std::string, usize> definition_names;
  for (usize i = 0; i < definitions.size(); ++i) {
    const BlockDefinition& definition = definitions[i];
    if (definition.name.empty()) {
      return Result::failure({"block definition has empty name"});
    }
    if (!definition_names.emplace(definition.name, i).second) {
      return Result::failure({"duplicate block definition: " + definition.name});
    }
    if (definition.drops.kind == BlockDropKind::Item && definition.drops.item_name.empty()) {
      return Result::failure({"block drops item must be non-empty for " + definition.name});
    }
    if (!std::isfinite(definition.item_buoyancy)) {
      return Result::failure({"block item buoyancy must be finite for " + definition.name});
    }
    if (!std::isfinite(definition.physics.slipperiness)) {
      return Result::failure({"block slipperiness must be finite for " + definition.name});
    }
    if (!definition.behavior_id.empty() && options.behavior_exists &&
        !options.behavior_exists(definition.behavior_id)) {
      return Result::failure(
          {"missing block behavior handler for " + definition.name + ": " +
           definition.behavior_id});
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
    block.display_name =
        definition.display_name.empty() ? definition.name : definition.display_name;
    block.behavior_id = definition.behavior_id;
    block.tags = definition.tags;
    block.properties = definition.properties;
    block.physics = definition.physics;
    block.drops = definition.drops;
    block.is_item = definition.is_item;
    block.item_buoyancy = definition.item_buoyancy;

    std::vector<ExpandedVariantDefinition> expanded_variants = expand_variants(definition);
    if (expanded_variants.empty()) {
      return Result::failure({"block has no variants after expansion: " + definition.name});
    }

    for (ExpandedVariantDefinition& expanded_variant : expanded_variants) {
      if (next_variant_id == std::numeric_limits<u32>::max()) {
        return Result::failure({"block variant ID limit exceeded"});
      }

      const BlockVariantID variant_id{next_variant_id++};
      BlockBody body;
      BlockVariantRecord variant;
      variant.id = variant_id;
      variant.block_id = block_id;
      variant.debug_name = std::move(expanded_variant.debug_name);
      variant.behavior_id = definition.behavior_id;
      variant.state_values = std::move(expanded_variant.state_values);
      variant.properties = definition.properties;
      variant.physics = definition.physics;
      variant.render_category = definition.render_category;
      variant.hide_same_variant_faces = definition.hide_same_variant_faces;
      variant.texture_assets = definition.texture_assets;
      variant.model = definition.model;
      variant.model_transform = model_transform_for_state_values(variant.state_values);
      variant.renders_face.fill(definition.render_category != RenderCategory::None && definition.properties.full_cube);
      variant.occludes_face.fill(definition.properties.full_cube && definition.properties.opaque);
      if (definition.properties.collidable && definition.properties.full_cube) {
        body.boxes.push_back({{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}});
      }
      variant.block_body_index = static_cast<u32>(registry.block_bodies_.size());
      registry.block_bodies_.push_back(std::move(body));

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

void BlockRegistry::set_display_name(BlockID id, std::string display_name) {
  if (id.value >= blocks_.size()) {
    return;
  }
  blocks_[id.value].display_name = std::move(display_name);
}

BlockID BlockRegistry::block_id(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record == nullptr ? AirBlockID : record->block_id;
}

std::string_view BlockRegistry::variant_name(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record == nullptr ? AirBlockName : std::string_view{record->debug_name};
}

const BlockBody* BlockRegistry::block_body(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  if (record == nullptr || record->block_body_index >= block_bodies_.size()) {
    return nullptr;
  }
  return &block_bodies_[record->block_body_index];
}

std::span<const Aabb3f> BlockRegistry::collision_boxes(BlockVariantID variant_id) const noexcept {
  const BlockBody* body = block_body(variant_id);
  if (body == nullptr) {
    return {};
  }
  return body->boxes;
}

BlockPhysicsProperties BlockRegistry::block_physics(BlockVariantID variant_id) const noexcept {
  const BlockVariantRecord* record = variant(variant_id);
  return record == nullptr ? BlockPhysicsProperties{} : record->physics;
}

void BlockRegistry::set_collision_boxes(
    BlockVariantID variant_id,
    std::vector<Aabb3f> boxes) {
  const BlockVariantRecord* record = variant(variant_id);
  if (record == nullptr || record->block_body_index >= block_bodies_.size()) {
    return;
  }
  block_bodies_[record->block_body_index].boxes = std::move(boxes);
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

std::span<const BlockModelBoxRenderData> BlockRenderData::model_boxes(
    BlockVariantID variant_id) const noexcept {
  if (variant_id.value >= variants_.size()) {
    return {};
  }
  return variants_[variant_id.value].model_boxes;
}

Vec3i BlockRenderData::model_center_origin(BlockVariantID variant_id) const noexcept {
  if (variant_id.value >= variants_.size()) {
    return {8, 8, 8};
  }
  return variants_[variant_id.value].model_center_origin;
}

void BlockRenderData::set_model_boxes(
    BlockVariantID variant_id,
    std::vector<BlockModelBoxRenderData> boxes) {
  if (variant_id.value >= variants_.size()) {
    return;
  }
  variants_[variant_id.value].model_boxes = std::move(boxes);
}

void BlockRenderData::set_model_center_origin(
    BlockVariantID variant_id,
    Vec3i center_origin) {
  if (variant_id.value >= variants_.size()) {
    return;
  }
  variants_[variant_id.value].model_center_origin = center_origin;
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

BlockDefinition make_air_definition() {
  BlockDefinition definition;
  definition.name = std::string{AirBlockName};
  definition.properties.replaceable = true;
  definition.render_category = RenderCategory::None;
  definition.is_item = false;
  return definition;
}

std::string normalize_block_model_asset(std::string_view model) {
  if (model.empty()) {
    return {};
  }
  if (model.find(':') != std::string_view::npos) {
    return std::string{model};
  }
  return "arvane:model/block/" + std::string{model};
}

std::string normalize_block_texture_asset(std::string_view texture) {
  if (texture.empty()) {
    return {};
  }
  if (texture.find(':') != std::string_view::npos) {
    return std::string{texture};
  }
  return "arvane:texture/block/" + std::string{texture};
}

BlockDefinitionsResult parse_block_definitions_json(std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return BlockDefinitionsResult::failure(
        {"failed to parse block definitions JSON: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  const io::DataValue* blocks = &root;
  if (root.kind() == io::DataKind::Object) {
    const io::DataValue* found = root.get("blocks");
    if (found == nullptr) {
      return BlockDefinitionsResult::failure({"block definition JSON object must contain blocks array"});
    }
    blocks = found;
  }
  const io::DataValue::Array* block_array = blocks->array_if();
  if (block_array == nullptr) {
    return BlockDefinitionsResult::failure({"block definitions must be a JSON array"});
  }

  std::vector<BlockDefinition> definitions;
  definitions.reserve(block_array->size());
  for (const io::DataValue& item : *block_array) {
      if (item.kind() != io::DataKind::Object) {
        return BlockDefinitionsResult::failure({"block definition entry must be an object"});
      }

      const io::DataValue* name_iter = item.get("name");
      if (name_iter == nullptr || name_iter->kind() != io::DataKind::String) {
        return BlockDefinitionsResult::failure({"block definition is missing string field name"});
      }

      BlockDefinition definition;
      definition.name = std::string{name_iter->as_string_or({})};

      const io::DataValue* display_name_iter = item.get("display_name");
      if (display_name_iter != nullptr) {
        if (display_name_iter->kind() != io::DataKind::String) {
          return BlockDefinitionsResult::failure(
              {"display_name must be a string for " + definition.name});
        }
        definition.display_name = std::string{display_name_iter->as_string_or({})};
      }

      const io::DataValue* behavior_iter = item.get("behavior");
      if (behavior_iter != nullptr) {
        if (behavior_iter->kind() != io::DataKind::String) {
          return BlockDefinitionsResult::failure({"behavior must be a string for " + definition.name});
        }
        definition.behavior_id = std::string{behavior_iter->as_string_or({})};
        if (definition.behavior_id.empty()) {
          return BlockDefinitionsResult::failure({"behavior must be non-empty for " + definition.name});
        }
      }

      const io::DataValue* category_iter = item.get("render_category");
      if (category_iter != nullptr) {
        if (category_iter->kind() != io::DataKind::String) {
          return BlockDefinitionsResult::failure({"render_category must be a string for " + definition.name});
        }
        auto parsed_category = parse_render_category(std::string{category_iter->as_string_or({})});
        if (!parsed_category) {
          return BlockDefinitionsResult::failure(parsed_category.error());
        }
        definition.render_category = parsed_category.value();
      }

      const io::DataValue* tags_iter = item.get("tags");
      if (tags_iter != nullptr) {
        const io::DataValue::Array* tags = tags_iter->array_if();
        if (tags == nullptr) {
          return BlockDefinitionsResult::failure({"tags must be an array for " + definition.name});
        }
        for (const io::DataValue& tag : *tags) {
          definition.tags.emplace_back(tag.as_string_or({}));
        }
      }

      const io::DataValue* properties_iter = item.get("properties");
      if (properties_iter != nullptr) {
        if (properties_iter->kind() != io::DataKind::Object) {
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
        read_bool_property(
            *properties_iter,
            "drops_with_wrong_tool",
            definition.properties.drops_with_wrong_tool);
        read_f32_property(*properties_iter, "hardness", definition.properties.hardness);
        read_f32_property(*properties_iter, "slipperiness", definition.physics.slipperiness);
        const io::DataValue* emission_iter = properties_iter->get("emission_rgb");
        if (emission_iter != nullptr) {
          const io::DataValue::Array* emission = emission_iter->array_if();
          if (emission == nullptr || emission->size() != 3U) {
            return BlockDefinitionsResult::failure(
                {"emission_rgb must be an array with 3 entries for " + definition.name});
          }
          definition.properties.emission_r = static_cast<u8>((*emission)[0].as_u64_or(0));
          definition.properties.emission_g = static_cast<u8>((*emission)[1].as_u64_or(0));
          definition.properties.emission_b = static_cast<u8>((*emission)[2].as_u64_or(0));
          definition.properties.emits_light =
              definition.properties.emission_r != 0 || definition.properties.emission_g != 0 ||
              definition.properties.emission_b != 0;
        }
      }

      const io::DataValue* hide_iter = item.get("hide_same_variant_faces");
      if (hide_iter != nullptr) {
        definition.hide_same_variant_faces =
            hide_iter->as_bool_or(definition.hide_same_variant_faces);
      }

      const io::DataValue* is_item_iter = item.get("is_item");
      if (is_item_iter != nullptr) {
        definition.is_item = is_item_iter->as_bool_or(definition.is_item);
      }

      const io::DataValue* buoyancy_iter = item.get("buoyancy");
      if (buoyancy_iter != nullptr) {
        if (!is_number(*buoyancy_iter)) {
          return BlockDefinitionsResult::failure(
              {"buoyancy must be a number for " + definition.name});
        }
        definition.item_buoyancy = static_cast<f32>(buoyancy_iter->as_f64_or(0.0));
      }

      const io::DataValue* drops_iter = item.get("drops");
      if (drops_iter != nullptr) {
        if (drops_iter->kind() == io::DataKind::Null) {
          definition.drops.kind = BlockDropKind::None;
          definition.drops.item_name.clear();
        } else if (drops_iter->kind() == io::DataKind::String) {
          definition.drops.kind = BlockDropKind::Item;
          definition.drops.item_name = std::string{drops_iter->as_string_or({})};
          if (definition.drops.item_name.empty()) {
            return BlockDefinitionsResult::failure(
                {"drops item must be non-empty for " + definition.name});
          }
        } else {
          return BlockDefinitionsResult::failure(
              {"drops must be a string or null for " + definition.name});
        }
      }

      const bool has_model = item.get("model") != nullptr;
      const bool has_texture = item.get("texture") != nullptr || item.get("textures") != nullptr;
      if (has_model && has_texture) {
        return BlockDefinitionsResult::failure(
            {"block defines both model and texture references for " + definition.name});
      }
      if (has_model) {
        if (auto model_error = read_model_reference(*item.get("model"), definition);
            model_error.has_value()) {
          return BlockDefinitionsResult::failure(model_error.value());
        }
      } else if (auto texture_error = read_texture_references(item, definition);
                 texture_error.has_value()) {
        return BlockDefinitionsResult::failure(texture_error.value());
      }

      const io::DataValue* states_iter = item.get("states");
      if (states_iter != nullptr) {
        const io::DataValue::Object* states = states_iter->object_if();
        if (states == nullptr) {
          return BlockDefinitionsResult::failure({"states must be an object for " + definition.name});
        }
        for (const auto& [state_name, state_values] : *states) {
          const io::DataValue::Array* values = state_values.array_if();
          if (values == nullptr) {
            return BlockDefinitionsResult::failure({"state values must be arrays for " + definition.name});
          }
          StatePropertyDefinition state;
          state.name = state_name;
          for (const io::DataValue& value : *values) {
            auto parsed_value = read_state_value_string(
                value,
                "state " + definition.name + "." + state.name);
            if (!parsed_value) {
              return BlockDefinitionsResult::failure(parsed_value.error());
            }
            state.values.push_back(std::move(parsed_value.value()));
          }
          definition.states.push_back(std::move(state));
        }
      }

      definitions.push_back(std::move(definition));
  }

  return BlockDefinitionsResult::success(std::move(definitions));
}

AuthoredModelDefinitionResult parse_authored_model_json(std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return AuthoredModelDefinitionResult::failure(
        {"failed to parse model JSON: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  if (root.kind() != io::DataKind::Object) {
    return AuthoredModelDefinitionResult::failure({"model root must be an object"});
  }
  if (auto legacy_error = reject_removed_model_fields(root, "model root");
      legacy_error.has_value()) {
    return AuthoredModelDefinitionResult::failure(legacy_error.value());
  }
  if (root.get("pivot") != nullptr) {
    return AuthoredModelDefinitionResult::failure(
        {"model root contains removed entity model field 'pivot'; pivots belong to boxes"});
  }

  AuthoredModelDefinition definition;
  if (const io::DataValue* name = root.get("name"); name != nullptr) {
    if (name->kind() != io::DataKind::String) {
      return AuthoredModelDefinitionResult::failure({"model name must be a string"});
    }
    definition.name = std::string{name->as_string_or({})};
  }
  bool has_center_origin = false;
  if (const io::DataValue* center_origin = root.get("center_origin");
      center_origin != nullptr) {
    auto parsed_center_origin = read_vec3f(*center_origin, "model center_origin");
    if (!parsed_center_origin) {
      return AuthoredModelDefinitionResult::failure(parsed_center_origin.error());
    }
    definition.center_origin = parsed_center_origin.value();
    has_center_origin = true;
  }

  const io::DataValue* textures = root.get("textures");
  const io::DataValue::Array* texture_array =
      textures == nullptr ? nullptr : textures->array_if();
  if (texture_array == nullptr) {
    return AuthoredModelDefinitionResult::failure({"model requires textures array"});
  }
  definition.texture_assets.reserve(texture_array->size());
  for (const io::DataValue& texture : *texture_array) {
    if (texture.kind() != io::DataKind::String) {
      return AuthoredModelDefinitionResult::failure({"model texture entries must be strings"});
    }
    definition.texture_assets.emplace_back(texture.as_string_or({}));
  }

  const io::DataValue* texture_sizes = root.get("texture_sizes");
  const io::DataValue::Array* texture_size_array =
      texture_sizes == nullptr ? nullptr : texture_sizes->array_if();
  if (texture_size_array == nullptr) {
    return AuthoredModelDefinitionResult::failure({"model requires texture_sizes array"});
  }
  definition.texture_sizes.reserve(texture_size_array->size());
  for (const io::DataValue& size : *texture_size_array) {
    auto parsed_size = read_vec2i(size, "model texture size");
    if (!parsed_size) {
      return AuthoredModelDefinitionResult::failure(parsed_size.error());
    }
    if (parsed_size.value().x <= 0 || parsed_size.value().y <= 0) {
      return AuthoredModelDefinitionResult::failure({"model texture sizes must be positive"});
    }
    definition.texture_sizes.push_back(parsed_size.value());
  }
  if (definition.texture_assets.size() != definition.texture_sizes.size()) {
    return AuthoredModelDefinitionResult::failure(
        {"model textures and texture_sizes must have the same length"});
  }

  const io::DataValue* boxes = root.get("boxes");
  const io::DataValue::Array* box_array =
      boxes == nullptr ? nullptr : boxes->array_if();
  if (box_array == nullptr) {
    return AuthoredModelDefinitionResult::failure({"model requires boxes array"});
  }
  definition.boxes.reserve(box_array->size());
  for (const io::DataValue& box_json : *box_array) {
    if (box_json.kind() != io::DataKind::Object) {
      return AuthoredModelDefinitionResult::failure({"model box must be an object"});
    }
    if (auto legacy_error = reject_removed_model_fields(box_json, "model box");
        legacy_error.has_value()) {
      return AuthoredModelDefinitionResult::failure(legacy_error.value());
    }

    const io::DataValue* bone = box_json.get("bone");
    if (bone == nullptr || bone->kind() != io::DataKind::String) {
      return AuthoredModelDefinitionResult::failure(
          {"model box bone must be a string"});
    }
    const io::DataValue* pivot_json = box_json.get("pivot");
    if (pivot_json == nullptr) {
      return AuthoredModelDefinitionResult::failure({"model box is missing pivot"});
    }

    const io::DataValue* min_json = box_json.get("min");
    if (min_json == nullptr) {
      return AuthoredModelDefinitionResult::failure({"model box is missing min"});
    }
    const io::DataValue* max_json = box_json.get("max");
    if (max_json == nullptr) {
      return AuthoredModelDefinitionResult::failure({"model box is missing max"});
    }

    AuthoredModelBoxDefinition box;
    box.bone = std::string{bone->as_string_or({})};
    auto pivot = read_vec3f(*pivot_json, "model box pivot");
    auto min = read_vec3f(*min_json, "model box min");
    auto max = read_vec3f(*max_json, "model box max");
    if (!pivot) {
      return AuthoredModelDefinitionResult::failure(pivot.error());
    }
    if (!min) {
      return AuthoredModelDefinitionResult::failure(min.error());
    }
    if (!max) {
      return AuthoredModelDefinitionResult::failure(max.error());
    }
    box.pivot = pivot.value();
    box.min = min.value();
    box.max = max.value();
    if (const io::DataValue* inflate = box_json.get("inflate"); inflate != nullptr) {
      if (!is_number(*inflate)) {
        return AuthoredModelDefinitionResult::failure({"model box inflate must be numeric"});
      }
      box.inflate = static_cast<f32>(inflate->as_f64_or(0.0));
      if (!std::isfinite(box.inflate)) {
        return AuthoredModelDefinitionResult::failure({"model box inflate must be finite"});
      }
    }
    if (box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z) {
      return AuthoredModelDefinitionResult::failure(
          {"model box min must be less than or equal to max"});
    }

    for (std::string_view face_name : {"east", "west", "up", "down", "south", "north"}) {
      const io::DataValue* face_json = box_json.get(face_name);
      if (face_json == nullptr) {
        continue;
      }
      auto face = read_authored_model_face(*face_json, definition.texture_assets.size());
      if (!face) {
        return AuthoredModelDefinitionResult::failure(face.error());
      }
      box.faces[face_index(block_face_from_name(face_name).value())] = face.value();
    }
    definition.boxes.push_back(std::move(box));
  }
  if (!has_center_origin) {
    definition.center_origin = derive_center_origin(definition.boxes);
  }

  return AuthoredModelDefinitionResult::success(std::move(definition));
}

BlockModelDefinitionResult parse_block_model_json(
    std::string_view text,
    std::span<const std::string> texture_inputs) {
  auto authored_model = parse_authored_model_json(text);
  if (!authored_model) {
    return BlockModelDefinitionResult::failure(authored_model.error());
  }

  BlockModelDefinition definition = std::move(authored_model.value());
  for (std::string& texture : definition.texture_assets) {
    auto resolved_texture = resolve_block_model_texture(texture, texture_inputs);
    if (!resolved_texture) {
      return BlockModelDefinitionResult::failure(resolved_texture.error());
    }
    texture = std::move(resolved_texture.value());
  }
  for (const BlockModelBoxDefinition& box : definition.boxes) {
    if (auto box_error = validate_block_model_box(box); box_error.has_value()) {
      return BlockModelDefinitionResult::failure(box_error.value());
    }
  }
  return BlockModelDefinitionResult::success(std::move(definition));
}

BlockFace transform_block_face(
    BlockFace face,
    BlockModelTransform transform) noexcept {
  if (transform.mirror_y) {
    if (face == BlockFace::PositiveY) {
      face = BlockFace::NegativeY;
    } else if (face == BlockFace::NegativeY) {
      face = BlockFace::PositiveY;
    }
  }

  const i8 turns = static_cast<i8>((transform.quarter_turns_y % 4 + 4) % 4);
  for (i8 turn = 0; turn < turns; ++turn) {
    switch (face) {
      case BlockFace::PositiveX:
        face = BlockFace::PositiveZ;
        break;
      case BlockFace::PositiveZ:
        face = BlockFace::NegativeX;
        break;
      case BlockFace::NegativeX:
        face = BlockFace::NegativeZ;
        break;
      case BlockFace::NegativeZ:
        face = BlockFace::PositiveX;
        break;
      case BlockFace::PositiveY:
      case BlockFace::NegativeY:
        break;
    }
  }
  return face;
}

Vec3i transform_model_point(
    Vec3i point,
    Vec3i center_origin,
    BlockModelTransform transform) noexcept {
  if (transform.mirror_y) {
    point.y = center_origin.y * 2 - point.y;
  }

  Vec3i local = point - center_origin;
  switch ((transform.quarter_turns_y % 4 + 4) % 4) {
    case 1:
      local = {-local.z, local.y, local.x};
      break;
    case 2:
      local = {-local.x, local.y, -local.z};
      break;
    case 3:
      local = {local.z, local.y, -local.x};
      break;
    default:
      break;
  }
  return center_origin + local;
}

BlockModelBoxRenderData transform_model_box(
    const BlockModelBoxRenderData& box,
    Vec3i center_origin,
    BlockModelTransform transform) noexcept {
  if (transform.quarter_turns_y == 0 && !transform.mirror_y) {
    return box;
  }

  const std::array<Vec3i, 8> corners{{
      {box.min.x, box.min.y, box.min.z},
      {box.max.x, box.min.y, box.min.z},
      {box.min.x, box.max.y, box.min.z},
      {box.max.x, box.max.y, box.min.z},
      {box.min.x, box.min.y, box.max.z},
      {box.max.x, box.min.y, box.max.z},
      {box.min.x, box.max.y, box.max.z},
      {box.max.x, box.max.y, box.max.z},
  }};

  BlockModelBoxRenderData transformed;
  transformed.min = transform_model_point(corners.front(), center_origin, transform);
  transformed.max = transformed.min;
  for (const Vec3i corner : corners) {
    const Vec3i point = transform_model_point(corner, center_origin, transform);
    transformed.min.x = std::min(transformed.min.x, point.x);
    transformed.min.y = std::min(transformed.min.y, point.y);
    transformed.min.z = std::min(transformed.min.z, point.z);
    transformed.max.x = std::max(transformed.max.x, point.x);
    transformed.max.y = std::max(transformed.max.y, point.y);
    transformed.max.z = std::max(transformed.max.z, point.z);
  }

  const auto face_corners = [](const BlockModelBoxRenderData& model_box, BlockFace face) {
    const Vec3i min = model_box.min;
    const Vec3i max = model_box.max;
    switch (face) {
      case BlockFace::PositiveX:
        return std::array<Vec3i, 4>{{{max.x, min.y, max.z}, {max.x, min.y, min.z}, {max.x, max.y, max.z}, {max.x, max.y, min.z}}};
      case BlockFace::NegativeX:
        return std::array<Vec3i, 4>{{{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, min.z}, {min.x, max.y, max.z}}};
      case BlockFace::PositiveY:
        return std::array<Vec3i, 4>{{{min.x, max.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z}}};
      case BlockFace::NegativeY:
        return std::array<Vec3i, 4>{{{max.x, min.y, max.z}, {min.x, min.y, max.z}, {max.x, min.y, min.z}, {min.x, min.y, min.z}}};
      case BlockFace::PositiveZ:
        return std::array<Vec3i, 4>{{{min.x, min.y, max.z}, {max.x, min.y, max.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z}}};
      case BlockFace::NegativeZ:
        return std::array<Vec3i, 4>{{{max.x, min.y, min.z}, {min.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z}}};
    }
    return std::array<Vec3i, 4>{};
  };

  for (BlockFace source_face : AllBlockFaces) {
    const usize source_slot = face_index(source_face);
    if (!box.faces[source_slot].has_value()) {
      continue;
    }

    BlockModelFaceRenderData face_data = box.faces[source_slot].value();
    const BlockFace target_face = transform_block_face(source_face, transform);
    const std::array<Vec3i, 4> source_corners = face_corners(box, source_face);
    const std::array<Vec3i, 4> target_corners = face_corners(transformed, target_face);
    for (usize target_index = 0; target_index < target_corners.size(); ++target_index) {
      for (usize source_index = 0; source_index < source_corners.size(); ++source_index) {
        if (transform_model_point(source_corners[source_index], center_origin, transform) ==
            target_corners[target_index]) {
          face_data.uvs[target_index] = box.faces[source_slot]->uvs[source_index];
          break;
        }
      }
    }
    transformed.faces[face_index(target_face)] = face_data;
  }
  return transformed;
}

std::vector<Aabb3f> make_block_model_collision_boxes(
    const BlockModelDefinition& model,
    BlockModelTransform transform) {
  std::vector<Aabb3f> boxes;
  boxes.reserve(model.boxes.size());
  const Vec3f center_origin{
      model.center_origin.x / 16.0F,
      model.center_origin.y / 16.0F,
      model.center_origin.z / 16.0F};
  for (const BlockModelBoxDefinition& box : model.boxes) {
    if (box.min.x >= box.max.x || box.min.y >= box.max.y || box.min.z >= box.max.z) {
      continue;
    }
    const Vec3f min{
        static_cast<f32>(box.min.x) / 16.0F,
        static_cast<f32>(box.min.y) / 16.0F,
        static_cast<f32>(box.min.z) / 16.0F,
    };
    const Vec3f max{
        static_cast<f32>(box.max.x) / 16.0F,
        static_cast<f32>(box.max.y) / 16.0F,
        static_cast<f32>(box.max.z) / 16.0F,
    };

    const std::array<Vec3f, 8> corners{{
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {min.x, max.y, min.z},
        {max.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {min.x, max.y, max.z},
        {max.x, max.y, max.z},
    }};

    Vec3f transformed_min = transform_model_point(corners.front(), center_origin, transform);
    Vec3f transformed_max = transformed_min;
    for (const Vec3f corner : corners) {
      const Vec3f transformed = transform_model_point(corner, center_origin, transform);
      transformed_min.x = std::min(transformed_min.x, transformed.x);
      transformed_min.y = std::min(transformed_min.y, transformed.y);
      transformed_min.z = std::min(transformed_min.z, transformed.z);
      transformed_max.x = std::max(transformed_max.x, transformed.x);
      transformed_max.y = std::max(transformed_max.y, transformed.y);
      transformed_max.z = std::max(transformed_max.z, transformed.z);
    }
    boxes.push_back({transformed_min, transformed_max});
  }
  return boxes;
}

} // namespace arv::voxel
