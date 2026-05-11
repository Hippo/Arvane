#include "arv/render/entity_model.hpp"

#include "arv/voxel/block.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::render {
namespace {

using json = nlohmann::json;

[[nodiscard]] usize face_index(EntityModelFace face) noexcept {
  return static_cast<usize>(face);
}

[[nodiscard]] Expected<Vec3f, EntityModelError> read_vec3(
    const json& node,
    const std::string& field) {
  if (!node.is_array() || node.size() != 3U) {
    return Expected<Vec3f, EntityModelError>::failure(
        {field + " must be a 3-element numeric array"});
  }
  for (const json& value : node) {
    if (!value.is_number()) {
      return Expected<Vec3f, EntityModelError>::failure(
          {field + " entries must be numeric"});
    }
  }
  Vec3f result{
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()};
  if (!std::isfinite(result.x) || !std::isfinite(result.y) ||
      !std::isfinite(result.z)) {
    return Expected<Vec3f, EntityModelError>::failure(
        {field + " entries must be finite"});
  }
  return Expected<Vec3f, EntityModelError>::success(result);
}

[[nodiscard]] std::string field_path(std::string_view path, std::string_view field) {
  if (path.empty()) {
    return std::string{field};
  }
  return std::string{path} + "." + std::string{field};
}

[[nodiscard]] std::string indexed_field_path(
    std::string_view path,
    std::string_view field,
    usize index) {
  return field_path(path, field) + "[" + std::to_string(index) + "]";
}

[[nodiscard]] std::optional<EntityModelFace> face_from_name(std::string_view name) noexcept {
  if (name == "east") {
    return EntityModelFace::PositiveX;
  }
  if (name == "west") {
    return EntityModelFace::NegativeX;
  }
  if (name == "up") {
    return EntityModelFace::PositiveY;
  }
  if (name == "down") {
    return EntityModelFace::NegativeY;
  }
  if (name == "south") {
    return EntityModelFace::PositiveZ;
  }
  if (name == "north") {
    return EntityModelFace::NegativeZ;
  }
  return std::nullopt;
}

[[nodiscard]] EntityModelFace face_from_block_face(voxel::BlockFace face) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return EntityModelFace::PositiveX;
    case voxel::BlockFace::NegativeX:
      return EntityModelFace::NegativeX;
    case voxel::BlockFace::PositiveY:
      return EntityModelFace::PositiveY;
    case voxel::BlockFace::NegativeY:
      return EntityModelFace::NegativeY;
    case voxel::BlockFace::PositiveZ:
      return EntityModelFace::PositiveZ;
    case voxel::BlockFace::NegativeZ:
      return EntityModelFace::NegativeZ;
  }
  return EntityModelFace::NegativeX;
}

[[nodiscard]] Vec3f face_color(EntityModelFace face) noexcept {
  switch (face) {
    case EntityModelFace::PositiveY:
      return {1.0F, 1.0F, 1.0F};
    case EntityModelFace::NegativeY:
      return {0.55F, 0.55F, 0.55F};
    case EntityModelFace::PositiveX:
    case EntityModelFace::NegativeX:
      return {0.78F, 0.78F, 0.78F};
    case EntityModelFace::PositiveZ:
    case EntityModelFace::NegativeZ:
      return {0.86F, 0.86F, 0.86F};
  }
  return {1.0F, 1.0F, 1.0F};
}

[[nodiscard]] std::array<Vec3f, 4> face_corners(
    const EntityModelBoxDefinition& box,
    EntityModelFace face) {
  const Vec3f min = box.min;
  const Vec3f max = box.max;
  switch (face) {
    case EntityModelFace::PositiveX:
      return {{{max.x, min.y, max.z}, {max.x, min.y, min.z}, {max.x, max.y, max.z}, {max.x, max.y, min.z}}};
    case EntityModelFace::NegativeX:
      return {{{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, min.z}, {min.x, max.y, max.z}}};
    case EntityModelFace::PositiveY:
      return {{{min.x, max.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z}}};
    case EntityModelFace::NegativeY:
      return {{{max.x, min.y, max.z}, {min.x, min.y, max.z}, {max.x, min.y, min.z}, {min.x, min.y, min.z}}};
    case EntityModelFace::PositiveZ:
      return {{{min.x, min.y, max.z}, {max.x, min.y, max.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z}}};
    case EntityModelFace::NegativeZ:
      return {{{max.x, min.y, min.z}, {min.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z}}};
  }
  return {};
}

[[nodiscard]] std::array<Vec2f, 4> face_uvs(
    EntityModelFace model_face,
    const EntityModelFaceDefinition& face,
    const EntityModelTextureBinding& binding,
    Vec2i texture_size) {
  const f32 logical_width = static_cast<f32>(std::max(texture_size.x, 1));
  const f32 logical_height = static_cast<f32>(std::max(texture_size.y, 1));
  const f32 source_width = static_cast<f32>(std::max(binding.source_width, 1U));
  const f32 source_height = static_cast<f32>(std::max(binding.source_height, 1U));
  const auto atlas_u = [&](i32 logical_u) {
    return static_cast<f32>(binding.atlas_pixel_min.x) +
           static_cast<f32>(logical_u) / logical_width * source_width;
  };
  const auto atlas_v = [&](i32 logical_v) {
    return static_cast<f32>(binding.atlas_pixel_min.y) +
           static_cast<f32>(logical_v) / logical_height * source_height;
  };

  const f32 u0 = atlas_u(face.uv.x);
  const f32 v0 = atlas_v(face.uv.y);
  const f32 u1 = atlas_u(face.uv.z);
  const f32 v1 = atlas_v(face.uv.w);
  switch (model_face) {
    case EntityModelFace::PositiveX:
    case EntityModelFace::NegativeX:
    case EntityModelFace::PositiveY:
    case EntityModelFace::PositiveZ:
      return {{{u0, v1}, {u1, v1}, {u0, v0}, {u1, v0}}};
    case EntityModelFace::NegativeY:
      return {{{u1, v0}, {u0, v0}, {u1, v1}, {u0, v1}}};
    case EntityModelFace::NegativeZ:
      return {{{u1, v1}, {u0, v1}, {u1, v0}, {u0, v0}}};
  }
  return {{{u0, v1}, {u1, v1}, {u0, v0}, {u1, v0}}};
}

[[nodiscard]] Vec3f blockbench_position_to_engine(Vec3f position) noexcept {
  return {-position.x, position.y, position.z};
}

[[nodiscard]] Vec3f blockbench_rotation_to_engine(Vec3f rotation_degrees) noexcept {
  return {rotation_degrees.x, -rotation_degrees.y, -rotation_degrees.z};
}

[[nodiscard]] EntityModelBoxDefinition blockbench_box_to_engine(
    const EntityModelBoxDefinition& source) {
  EntityModelBoxDefinition box = source;
  const Vec3f inflated_min{
      source.min.x - source.inflate,
      source.min.y - source.inflate,
      source.min.z - source.inflate};
  const Vec3f inflated_max{
      source.max.x + source.inflate,
      source.max.y + source.inflate,
      source.max.z + source.inflate};

  box.min = {-inflated_max.x, inflated_min.y, inflated_min.z};
  box.max = {-inflated_min.x, inflated_max.y, inflated_max.z};
  box.pivot = blockbench_position_to_engine(source.pivot);
  std::swap(box.faces[face_index(EntityModelFace::PositiveX)],
            box.faces[face_index(EntityModelFace::NegativeX)]);
  return box;
}

void append_face(
    EntityModelAsset& asset,
    u32 bone_index,
    u32 flags,
    const EntityModelBoxDefinition& box,
    EntityModelFace face,
    const EntityModelFaceDefinition& face_definition,
    const EntityModelTextureBinding& binding,
    Vec2i texture_size) {
  const u32 base = static_cast<u32>(asset.vertices.size());
  const std::array<Vec3f, 4> corners = face_corners(box, face);
  const std::array<Vec2f, 4> uvs = face_uvs(face, face_definition, binding, texture_size);
  const Vec3f color = face_color(face);
  for (usize index = 0; index < corners.size(); ++index) {
    asset.vertices.push_back({
        corners[index],
        uvs[index],
        color,
        bone_index,
        flags,
        static_cast<u32>(binding.atlas_domain),
        binding.atlas_page});
  }
  asset.indices.insert(
      asset.indices.end(),
      {base + 0U, base + 1U, base + 2U, base + 2U, base + 1U, base + 3U});
}

[[nodiscard]] Vec3f lerp(Vec3f from, Vec3f to, f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha};
}

[[nodiscard]] const EntityAnimationClip* find_clip(
    const EntityAnimationAsset* animation,
    std::string_view name) noexcept {
  if (animation == nullptr || name.empty()) {
    return nullptr;
  }
  for (const EntityAnimationClip& clip : animation->clips) {
    if (clip.name == name) {
      return &clip;
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<Vec3f> keyframe_channel_for_bone(
    const EntityAnimationKeyframe& keyframe,
    std::string_view bone_name,
    EntityAnimationChannel channel) noexcept {
  if (keyframe.bone_name == bone_name && keyframe.channel == channel) {
    return keyframe.value;
  }
  return std::nullopt;
}

[[nodiscard]] Vec3f sample_animation_bone_channel(
    const EntityAnimationClip& clip,
    std::string_view bone_name,
    EntityAnimationChannel channel,
    f32 sample_time) noexcept {
  const bool looping = clip.loop && clip.length_seconds > 0.0F;
  if (looping) {
    sample_time = std::fmod(sample_time, clip.length_seconds);
    if (sample_time < 0.0F) {
      sample_time += clip.length_seconds;
    }
  }
  const EntityAnimationKeyframe* first = nullptr;
  const EntityAnimationKeyframe* last = nullptr;
  const EntityAnimationKeyframe* previous = nullptr;
  const EntityAnimationKeyframe* next = nullptr;
  f32 first_time = 0.0F;
  f32 last_time = 0.0F;
  f32 previous_time = 0.0F;
  f32 next_time = 0.0F;

  for (const EntityAnimationKeyframe& keyframe : clip.keyframes) {
    const std::optional<Vec3f> value =
        keyframe_channel_for_bone(keyframe, bone_name, channel);
    if (!value.has_value()) {
      continue;
    }
    if (first == nullptr) {
      first = &keyframe;
      first_time = keyframe.time_seconds;
    }
    last = &keyframe;
    last_time = keyframe.time_seconds;
    if (keyframe.time_seconds <= sample_time) {
      previous = &keyframe;
      previous_time = keyframe.time_seconds;
    }
    if (keyframe.time_seconds >= sample_time && next == nullptr) {
      next = &keyframe;
      next_time = keyframe.time_seconds;
    }
  }

  if (first == nullptr) {
    return {};
  }
  if (looping) {
    if (previous == nullptr) {
      previous = last;
      previous_time = last_time - clip.length_seconds;
    }
    if (next == nullptr) {
      next = first;
      next_time = first_time + clip.length_seconds;
    }
  }
  if (previous == nullptr) {
    return {};
  }
  const Vec3f from =
      keyframe_channel_for_bone(*previous, bone_name, channel).value_or(Vec3f{});
  if (next == nullptr) {
    return from;
  }
  const Vec3f to =
      keyframe_channel_for_bone(*next, bone_name, channel).value_or(from);
  const f32 span = next_time - previous_time;
  if (span <= 0.0001F) {
    return from;
  }
  const f32 alpha = std::clamp((sample_time - previous_time) / span, 0.0F, 1.0F);
  return lerp(from, to, alpha);
}

[[nodiscard]] Expected<EntityAnimationChannel, EntityModelError> parse_animation_channel(
    const json& node,
    const std::string& path) {
  if (!node.is_string()) {
    return Expected<EntityAnimationChannel, EntityModelError>::failure(
        {path + ".channel must be a string"});
  }
  const std::string channel = node.get<std::string>();
  if (channel == "rotation") {
    return Expected<EntityAnimationChannel, EntityModelError>::success(
        EntityAnimationChannel::Rotation);
  }
  if (channel == "position") {
    return Expected<EntityAnimationChannel, EntityModelError>::success(
        EntityAnimationChannel::Position);
  }
  if (channel == "scale") {
    return Expected<EntityAnimationChannel, EntityModelError>::success(
        EntityAnimationChannel::Scale);
  }
  return Expected<EntityAnimationChannel, EntityModelError>::failure(
      {path + ".channel has unsupported animation channel: " + channel});
}

[[nodiscard]] Expected<EntityAnimationInterpolation, EntityModelError>
parse_animation_interpolation(const json& node, const std::string& path) {
  if (!node.is_string()) {
    return Expected<EntityAnimationInterpolation, EntityModelError>::failure(
        {path + ".interpolation must be a string"});
  }
  const std::string interpolation = node.get<std::string>();
  if (interpolation == "linear") {
    return Expected<EntityAnimationInterpolation, EntityModelError>::success(
        EntityAnimationInterpolation::Linear);
  }
  return Expected<EntityAnimationInterpolation, EntityModelError>::failure(
      {path + ".interpolation has unsupported value: " + interpolation});
}

[[nodiscard]] Expected<bool, EntityModelError> parse_animation_loop(
    const json& node,
    const std::string& path) {
  if (node.is_boolean()) {
    return Expected<bool, EntityModelError>::success(node.get<bool>());
  }
  if (node.is_string()) {
    const std::string loop = node.get<std::string>();
    if (loop.empty() || loop == "none" || loop == "false" || loop == "once" ||
        loop == "hold") {
      return Expected<bool, EntityModelError>::success(false);
    }
    if (loop == "loop" || loop == "true") {
      return Expected<bool, EntityModelError>::success(true);
    }
  }
  return Expected<bool, EntityModelError>::failure(
      {path + ".loop must be a boolean or accepted loop string"});
}

[[nodiscard]] bool is_player_entity_model_name(const std::string& name) noexcept {
  return name == "player" || name.rfind("player#", 0) == 0;
}

} // namespace

std::string normalize_entity_texture_asset(std::string_view texture) {
  if (texture.empty()) {
    return {};
  }
  if (texture.front() == '$') {
    return std::string{texture};
  }
  if (texture.find(':') != std::string_view::npos) {
    return std::string{texture};
  }
  return "arvane:texture/entity/" + std::string{texture};
}

EntityModelDefinitionResult parse_entity_model_json(std::string_view text) {
  auto authored_model = voxel::parse_authored_model_json(text);
  if (!authored_model) {
    return EntityModelDefinitionResult::failure({authored_model.error().message});
  }

  EntityModelDefinition definition;
  definition.name = std::move(authored_model.value().name);
  definition.center_origin = authored_model.value().center_origin;
  definition.texture_assets.reserve(authored_model.value().texture_assets.size());
  for (const std::string& texture : authored_model.value().texture_assets) {
    definition.texture_assets.push_back(normalize_entity_texture_asset(texture));
  }
  definition.texture_sizes = std::move(authored_model.value().texture_sizes);
  definition.boxes.reserve(authored_model.value().boxes.size());
  for (const voxel::AuthoredModelBoxDefinition& source_box : authored_model.value().boxes) {
    EntityModelBoxDefinition box;
    box.bone = source_box.bone;
    box.pivot = source_box.pivot;
    box.min = source_box.min;
    box.max = source_box.max;
    box.inflate = source_box.inflate;
    for (voxel::BlockFace source_face : voxel::AllBlockFaces) {
      const usize source_index = voxel::face_index(source_face);
      if (!source_box.faces[source_index].has_value()) {
        continue;
      }
      const voxel::AuthoredModelFaceDefinition& source_face_definition =
          source_box.faces[source_index].value();
      box.faces[face_index(face_from_block_face(source_face))] = EntityModelFaceDefinition{
          source_face_definition.texture_index,
          source_face_definition.uv};
    }
    definition.boxes.push_back(std::move(box));
  }
  return EntityModelDefinitionResult::success(std::move(definition));
}

EntityAnimationDefinitionResult parse_entity_animation_json(
    std::string_view text,
    std::string_view diagnostic_path) {
  try {
    const json root = json::parse(text.begin(), text.end());
    const std::string root_path =
        diagnostic_path.empty() ? std::string{"animation asset"} : std::string{diagnostic_path};
    if (!root.is_object()) {
      return EntityAnimationDefinitionResult::failure({root_path + " root must be an object"});
    }

    const auto animations = root.find("animations");
    if (animations == root.end() || !animations->is_array()) {
      return EntityAnimationDefinitionResult::failure(
          {field_path(root_path, "animations") + " must be an array"});
    }

    EntityAnimationDefinition definition;
    definition.clips.reserve(animations->size());
    for (usize animation_index = 0; animation_index < animations->size(); ++animation_index) {
      const json& animation_json = animations->at(animation_index);
      const std::string animation_path =
          indexed_field_path(root_path, "animations", animation_index);
      if (!animation_json.is_object()) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + " must be an object"});
      }

      const auto name = animation_json.find("name");
      if (name == animation_json.end() || !name->is_string()) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + ".name must be a string"});
      }
      const auto length = animation_json.find("length");
      if (length == animation_json.end() || !length->is_number()) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + ".length must be numeric"});
      }
      const auto loop = animation_json.find("loop");
      if (loop == animation_json.end()) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + ".loop is required"});
      }
      auto parsed_loop = parse_animation_loop(*loop, animation_path);
      if (!parsed_loop) {
        return EntityAnimationDefinitionResult::failure(parsed_loop.error());
      }
      const auto keyframes = animation_json.find("keyframes");
      if (keyframes == animation_json.end() || !keyframes->is_array()) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + ".keyframes must be an array"});
      }

      EntityAnimationClip clip;
      clip.name = name->get<std::string>();
      clip.length_seconds = length->get<f32>();
      if (!std::isfinite(clip.length_seconds)) {
        return EntityAnimationDefinitionResult::failure(
            {animation_path + ".length must be finite"});
      }
      clip.loop = parsed_loop.value();
      clip.keyframes.reserve(keyframes->size());
      for (usize keyframe_index = 0; keyframe_index < keyframes->size(); ++keyframe_index) {
        const json& keyframe_json = keyframes->at(keyframe_index);
        const std::string keyframe_path =
            indexed_field_path(animation_path, "keyframes", keyframe_index);
        if (!keyframe_json.is_object()) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + " must be an object"});
        }

        const auto time = keyframe_json.find("time");
        if (time == keyframe_json.end() || !time->is_number()) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + ".time must be numeric"});
        }
        const auto bone = keyframe_json.find("bone");
        if (bone == keyframe_json.end() || !bone->is_string()) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + ".bone must be a string"});
        }
        const auto channel = keyframe_json.find("channel");
        if (channel == keyframe_json.end()) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + ".channel is required"});
        }
        auto parsed_channel = parse_animation_channel(*channel, keyframe_path);
        if (!parsed_channel) {
          return EntityAnimationDefinitionResult::failure(parsed_channel.error());
        }
        const auto value = keyframe_json.find("value");
        if (value == keyframe_json.end()) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + ".value is required"});
        }
        auto parsed_value = read_vec3(*value, keyframe_path + ".value");
        if (!parsed_value) {
          return EntityAnimationDefinitionResult::failure(parsed_value.error());
        }

        EntityAnimationKeyframe keyframe;
        keyframe.time_seconds = time->get<f32>();
        if (!std::isfinite(keyframe.time_seconds)) {
          return EntityAnimationDefinitionResult::failure(
              {keyframe_path + ".time must be finite"});
        }
        keyframe.bone_name = bone->get<std::string>();
        keyframe.channel = parsed_channel.value();
        keyframe.value = parsed_value.value();
        if (keyframe.channel == EntityAnimationChannel::Rotation) {
          keyframe.value = blockbench_rotation_to_engine(keyframe.value);
        } else if (keyframe.channel == EntityAnimationChannel::Position) {
          keyframe.value = blockbench_position_to_engine(keyframe.value);
        }
        if (const auto interpolation = keyframe_json.find("interpolation");
            interpolation != keyframe_json.end()) {
          auto parsed_interpolation =
              parse_animation_interpolation(*interpolation, keyframe_path);
          if (!parsed_interpolation) {
            return EntityAnimationDefinitionResult::failure(parsed_interpolation.error());
          }
          keyframe.interpolation = parsed_interpolation.value();
        }
        clip.keyframes.push_back(std::move(keyframe));
      }
      std::sort(
          clip.keyframes.begin(),
          clip.keyframes.end(),
          [](const EntityAnimationKeyframe& lhs, const EntityAnimationKeyframe& rhs) {
            if (lhs.time_seconds != rhs.time_seconds) {
              return lhs.time_seconds < rhs.time_seconds;
            }
            if (lhs.bone_name != rhs.bone_name) {
              return lhs.bone_name < rhs.bone_name;
            }
            return static_cast<u8>(lhs.channel) < static_cast<u8>(rhs.channel);
          });
      definition.clips.push_back(std::move(clip));
    }

    return EntityAnimationDefinitionResult::success(std::move(definition));
  } catch (const json::exception& ex) {
    const std::string root_path =
        diagnostic_path.empty() ? std::string{"animation asset"} : std::string{diagnostic_path};
    return EntityAnimationDefinitionResult::failure(
        {root_path + " failed to parse animation JSON: " + std::string{ex.what()}});
  }
}

EntityModelAssetResult build_entity_model_asset(
    EntityModelID id,
    const EntityModelDefinition& definition,
    std::span<const EntityModelTextureBinding> texture_bindings) {
  EntityModelAsset asset;
  asset.id = id;
  asset.name = definition.name;
  asset.center_origin = blockbench_position_to_engine(definition.center_origin);

  HashMap<std::string, u32> bone_indices;
  const auto ensure_bone = [&](const std::string& bone_name, Vec3f pivot)
      -> Expected<u32, EntityModelError> {
    const auto found = bone_indices.find(bone_name);
    if (found != bone_indices.end()) {
      const EntityModelBone& existing = asset.bones[found->second];
      if (!bone_name.empty() && existing.pivot != pivot) {
        return Expected<u32, EntityModelError>::failure(
            {"entity model boxes for bone '" + bone_name + "' use inconsistent pivots"});
      }
      return Expected<u32, EntityModelError>::success(found->second);
    }
    const u32 bone_index = static_cast<u32>(asset.bones.size());
    bone_indices.emplace(bone_name, bone_index);
    asset.bones.push_back({bone_name, pivot});
    return Expected<u32, EntityModelError>::success(bone_index);
  };

  HashMap<std::string, usize> emitted_box_count_by_bone;
  for (const EntityModelBoxDefinition& source_box : definition.boxes) {
    const EntityModelBoxDefinition box = blockbench_box_to_engine(source_box);
    auto bone_index_result = ensure_bone(box.bone, box.pivot);
    if (!bone_index_result) {
      return EntityModelAssetResult::failure(bone_index_result.error());
    }
    const u32 bone_index = bone_index_result.value();
    const usize box_index_for_bone = emitted_box_count_by_bone[source_box.bone]++;
    u32 flags = 0U;
    if (is_player_entity_model_name(definition.name)) {
      flags = (box_index_for_bone % 2U) == 0U
                  ? EntityModelMeshVertexFlagSkinBaseLayer
                  : EntityModelMeshVertexFlagSkinDecorLayer;
    }
    for (usize face_value = 0; face_value < box.faces.size(); ++face_value) {
      if (!box.faces[face_value].has_value()) {
        continue;
      }
      const EntityModelFaceDefinition& face = box.faces[face_value].value();
      if (face.texture_index < 0) {
        continue;
      }
      const auto texture_index = static_cast<usize>(face.texture_index);
      if (texture_index >= texture_bindings.size()) {
        return EntityModelAssetResult::failure(
            {"entity model face references missing texture index"});
      }
      if (!definition.texture_sizes.empty() && texture_index >= definition.texture_sizes.size()) {
        return EntityModelAssetResult::failure(
            {"entity model face references missing texture size"});
      }
      const EntityModelTextureBinding& binding = texture_bindings[texture_index];
      const Vec2i texture_size = definition.texture_sizes.empty()
                                     ? Vec2i{
                                           static_cast<i32>(binding.source_width),
                                           static_cast<i32>(binding.source_height)}
                                     : definition.texture_sizes[texture_index];
      append_face(
          asset,
          bone_index,
          flags,
          box,
          static_cast<EntityModelFace>(face_value),
          face,
          binding,
          texture_size);
    }
  }

  return EntityModelAssetResult::success(std::move(asset));
}

EntityAnimationAssetResult build_entity_animation_asset(
    EntityAnimationID id,
    std::string name,
    EntityAnimationDefinition definition) {
  EntityAnimationAsset asset;
  asset.id = id;
  asset.name = std::move(name);
  asset.clips = std::move(definition.clips);
  return EntityAnimationAssetResult::success(std::move(asset));
}

std::optional<EntityModelError> validate_entity_animation_bindings(
    const EntityModelAsset& model,
    const EntityAnimationAsset& animation) {
  HashSet<std::string> model_bones;
  for (const EntityModelBone& bone : model.bones) {
    model_bones.insert(bone.name);
  }
  for (const EntityAnimationClip& clip : animation.clips) {
    for (const EntityAnimationKeyframe& keyframe : clip.keyframes) {
      if (model_bones.find(keyframe.bone_name) == model_bones.end()) {
        return EntityModelError{
            "animation " + animation.name + " clip " + clip.name +
            " references unknown model bone: " + keyframe.bone_name};
      }
    }
  }
  return std::nullopt;
}

void sample_entity_model_animation(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations) {
  for (Vec3f& rotation : output_rotations) {
    rotation = {};
  }
  if (output_rotations.size() < model.bones.size()) {
    return;
  }

  const EntityAnimationClip* clip = find_clip(animation, animation_name);
  if (clip == nullptr || clip->keyframes.empty()) {
    return;
  }

  f32 sample_time = time_seconds;
  if (clip->loop && clip->length_seconds > 0.0F) {
    sample_time = std::fmod(std::max(sample_time, 0.0F), clip->length_seconds);
  } else {
    sample_time = std::clamp(sample_time, 0.0F, clip->length_seconds);
  }

  for (usize bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
    output_rotations[bone_index] = sample_animation_bone_channel(
        *clip,
        model.bones[bone_index].name,
        EntityAnimationChannel::Rotation,
        sample_time);
  }
}

void sample_entity_model_animation(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations,
    std::span<Vec3f> output_positions) {
  for (Vec3f& rotation : output_rotations) {
    rotation = {};
  }
  for (Vec3f& position : output_positions) {
    position = {};
  }
  if (output_rotations.size() < model.bones.size() ||
      output_positions.size() < model.bones.size()) {
    return;
  }

  const EntityAnimationClip* clip = find_clip(animation, animation_name);
  if (clip == nullptr || clip->keyframes.empty()) {
    return;
  }

  f32 sample_time = time_seconds;
  if (clip->loop && clip->length_seconds > 0.0F) {
    sample_time = std::fmod(std::max(sample_time, 0.0F), clip->length_seconds);
  } else {
    sample_time = std::clamp(sample_time, 0.0F, clip->length_seconds);
  }

  for (usize bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
    output_rotations[bone_index] = sample_animation_bone_channel(
        *clip,
        model.bones[bone_index].name,
        EntityAnimationChannel::Rotation,
        sample_time);
    output_positions[bone_index] = sample_animation_bone_channel(
        *clip,
        model.bones[bone_index].name,
        EntityAnimationChannel::Position,
        sample_time);
  }
}

void sample_entity_model_animations(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::span<const EntityAnimationPlayback> animations,
    std::span<Vec3f> output_rotations,
    std::span<Vec3f> output_positions) {
  for (Vec3f& rotation : output_rotations) {
    rotation = {};
  }
  for (Vec3f& position : output_positions) {
    position = {};
  }
  if (output_rotations.size() < model.bones.size() ||
      output_positions.size() < model.bones.size()) {
    return;
  }

  for (const EntityAnimationPlayback& playback : animations) {
    const EntityAnimationClip* clip = find_clip(animation, playback.name);
    if (clip == nullptr || clip->keyframes.empty()) {
      continue;
    }

    f32 sample_time = playback.time_seconds;
    if (clip->loop && clip->length_seconds > 0.0F) {
      sample_time = std::fmod(std::max(sample_time, 0.0F), clip->length_seconds);
    } else {
      sample_time = std::clamp(sample_time, 0.0F, clip->length_seconds);
    }

    for (usize bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
      output_rotations[bone_index] = output_rotations[bone_index] +
          sample_animation_bone_channel(
              *clip,
              model.bones[bone_index].name,
              EntityAnimationChannel::Rotation,
              sample_time);
      output_positions[bone_index] = output_positions[bone_index] +
          sample_animation_bone_channel(
              *clip,
              model.bones[bone_index].name,
              EntityAnimationChannel::Position,
              sample_time);
    }
  }
}

} // namespace arv::render
