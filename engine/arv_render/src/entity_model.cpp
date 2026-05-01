#include "arv/render/entity_model.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::render {
namespace {

using json = nlohmann::json;

[[nodiscard]] usize face_index(EntityModelFace face) noexcept {
  return static_cast<usize>(face);
}

[[nodiscard]] Expected<Vec3f, EntityModelError> read_vec3(const json& node, std::string_view field) {
  if (!node.is_array() || node.size() != 3U) {
    return Expected<Vec3f, EntityModelError>::failure(
        {std::string{field} + " must be a 3-element array"});
  }
  return Expected<Vec3f, EntityModelError>::success({
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()});
}

[[nodiscard]] Expected<Vec2i, EntityModelError> read_vec2i(const json& node, std::string_view field) {
  if (!node.is_array() || node.size() != 2U) {
    return Expected<Vec2i, EntityModelError>::failure(
        {std::string{field} + " must be a 2-element array"});
  }
  return Expected<Vec2i, EntityModelError>::success({
      node.at(0).get<i32>(),
      node.at(1).get<i32>()});
}

[[nodiscard]] Expected<Vec4i, EntityModelError> read_vec4i(const json& node, std::string_view field) {
  if (!node.is_array() || node.size() != 4U) {
    return Expected<Vec4i, EntityModelError>::failure(
        {std::string{field} + " must be a 4-element array"});
  }
  return Expected<Vec4i, EntityModelError>::success({
      node.at(0).get<i32>(),
      node.at(1).get<i32>(),
      node.at(2).get<i32>(),
      node.at(3).get<i32>()});
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

[[nodiscard]] std::array<Vec3f, 4> face_corners(const EntityModelBoxDefinition& box, EntityModelFace face) {
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

[[nodiscard]] Expected<EntityModelFaceDefinition, EntityModelError> read_face(const json& node) {
  if (!node.is_object()) {
    return Expected<EntityModelFaceDefinition, EntityModelError>::failure(
        {"face entry must be an object"});
  }
  EntityModelFaceDefinition face;
  if (const auto texture = node.find("texture"); texture != node.end()) {
    face.texture_index = texture->get<i32>();
  }
  if (face.texture_index < 0) {
    return Expected<EntityModelFaceDefinition, EntityModelError>::success(face);
  }
  const auto uv = node.find("uv");
  if (uv == node.end()) {
    return Expected<EntityModelFaceDefinition, EntityModelError>::failure(
        {"face entry is missing uv"});
  }
  auto parsed_uv = read_vec4i(*uv, "face uv");
  if (!parsed_uv) {
    return Expected<EntityModelFaceDefinition, EntityModelError>::failure(parsed_uv.error());
  }
  face.uv = parsed_uv.value();
  return Expected<EntityModelFaceDefinition, EntityModelError>::success(face);
}

void append_face(
    EntityModelAsset& asset,
    u32 bone_index,
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
    asset.vertices.push_back({corners[index], uvs[index], color, bone_index});
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

[[nodiscard]] const EntityModelAnimation* find_animation(
    const EntityModelAsset& model,
    std::string_view name) noexcept {
  if (!name.empty()) {
    for (const EntityModelAnimation& animation : model.animations) {
      if (animation.name == name) {
        return &animation;
      }
    }
  }
  return model.animations.empty() ? nullptr : &model.animations.front();
}

[[nodiscard]] std::optional<Vec3f> keyframe_rotation_for_bone(
    const EntityModelAnimationKeyframe& keyframe,
    u32 bone_index) noexcept {
  for (const EntityModelAnimationBoneKey& bone : keyframe.bones) {
    if (bone.bone_index == bone_index) {
      return bone.rotation_euler_degrees;
    }
  }
  return std::nullopt;
}

} // namespace

std::string normalize_entity_texture_asset(std::string_view texture) {
  if (texture.empty()) {
    return {};
  }
  if (texture.find(':') != std::string_view::npos) {
    return std::string{texture};
  }
  return "arvane:texture/entity/" + std::string{texture};
}

EntityModelDefinitionResult parse_entity_model_json(std::string_view text) {
  try {
    const json root = json::parse(text.begin(), text.end());
    if (!root.is_object()) {
      return EntityModelDefinitionResult::failure({"entity model root must be an object"});
    }

    EntityModelDefinition definition;
    if (const auto name = root.find("name"); name != root.end()) {
      definition.name = name->get<std::string>();
    }

    const auto skeleton = root.find("skeleton");
    if (skeleton == root.end() || !skeleton->is_array()) {
      return EntityModelDefinitionResult::failure({"entity model is missing skeleton array"});
    }

    definition.bones.reserve(skeleton->size());
    for (const json& bone_json : *skeleton) {
      if (!bone_json.is_object()) {
        return EntityModelDefinitionResult::failure({"skeleton bone must be an object"});
      }
      EntityModelBoneDefinition bone;
      bone.name = bone_json.at("name").get<std::string>();
      auto pivot = read_vec3(bone_json.at("pivot"), "bone pivot");
      if (!pivot) {
        return EntityModelDefinitionResult::failure(pivot.error());
      }
      bone.pivot = pivot.value();
      if (const auto boxes = bone_json.find("boxes"); boxes != bone_json.end()) {
        if (!boxes->is_array()) {
          return EntityModelDefinitionResult::failure({"bone boxes must be an array"});
        }
        for (const json& box_json : *boxes) {
          EntityModelBoxDefinition box;
          auto min = read_vec3(box_json.at("min"), "box min");
          auto max = read_vec3(box_json.at("max"), "box max");
          if (!min) {
            return EntityModelDefinitionResult::failure(min.error());
          }
          if (!max) {
            return EntityModelDefinitionResult::failure(max.error());
          }
          box.min = min.value();
          box.max = max.value();
          for (std::string_view face_name : {"east", "west", "up", "down", "south", "north"}) {
            const auto face_json = box_json.find(std::string{face_name});
            if (face_json == box_json.end()) {
              continue;
            }
            auto face = read_face(*face_json);
            if (!face) {
              return EntityModelDefinitionResult::failure(face.error());
            }
            box.faces[face_index(face_from_name(face_name).value())] = face.value();
          }
          bone.boxes.push_back(std::move(box));
        }
      }
      definition.bones.push_back(std::move(bone));
    }

    if (const auto textures = root.find("textures"); textures != root.end()) {
      if (!textures->is_array()) {
        return EntityModelDefinitionResult::failure({"textures must be an array"});
      }
      for (const json& texture : *textures) {
        definition.texture_assets.push_back(normalize_entity_texture_asset(texture.get<std::string>()));
      }
    }
    if (definition.texture_assets.empty()) {
      definition.texture_assets.push_back("arvane:texture/missing");
    }

    if (const auto texture_sizes = root.find("texture_sizes"); texture_sizes != root.end()) {
      if (!texture_sizes->is_array()) {
        return EntityModelDefinitionResult::failure({"texture_sizes must be an array"});
      }
      for (const json& size : *texture_sizes) {
        auto parsed_size = read_vec2i(size, "texture size");
        if (!parsed_size) {
          return EntityModelDefinitionResult::failure(parsed_size.error());
        }
        definition.texture_sizes.push_back(parsed_size.value());
      }
    }

    if (const auto animations = root.find("animations"); animations != root.end()) {
      if (!animations->is_array()) {
        return EntityModelDefinitionResult::failure({"animations must be an array"});
      }
      for (const json& animation_json : *animations) {
        EntityModelAnimationDefinition animation;
        animation.name = animation_json.at("name").get<std::string>();
        animation.length_seconds = animation_json.value("length", 0.0F);
        animation.loop = animation_json.value("loop", std::string{}) == "loop";
        if (const auto keyframes = animation_json.find("keyframes"); keyframes != animation_json.end()) {
          if (!keyframes->is_array()) {
            return EntityModelDefinitionResult::failure({"animation keyframes must be an array"});
          }
          for (const json& keyframe_json : *keyframes) {
            EntityModelAnimationKeyframeDefinition keyframe;
            keyframe.time_seconds = keyframe_json.value("time", 0.0F);
            if (const auto bones = keyframe_json.find("bones"); bones != keyframe_json.end()) {
              if (!bones->is_array()) {
                return EntityModelDefinitionResult::failure({"keyframe bones must be an array"});
              }
              for (const json& bone_json : *bones) {
                EntityModelAnimationBoneDefinition bone;
                bone.bone_name = bone_json.at("name").get<std::string>();
                auto rotation = read_vec3(bone_json.at("rotation_euler"), "bone rotation_euler");
                if (!rotation) {
                  return EntityModelDefinitionResult::failure(rotation.error());
                }
                bone.rotation_euler_degrees = rotation.value();
                keyframe.bones.push_back(std::move(bone));
              }
            }
            animation.keyframes.push_back(std::move(keyframe));
          }
        }
        std::sort(
            animation.keyframes.begin(),
            animation.keyframes.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.time_seconds < rhs.time_seconds;
            });
        definition.animations.push_back(std::move(animation));
      }
    }

    return EntityModelDefinitionResult::success(std::move(definition));
  } catch (const json::exception& ex) {
    return EntityModelDefinitionResult::failure(
        {"failed to parse entity model JSON: " + std::string{ex.what()}});
  }
}

EntityModelAssetResult build_entity_model_asset(
    EntityModelID id,
    const EntityModelDefinition& definition,
    std::span<const EntityModelTextureBinding> texture_bindings) {
  EntityModelAsset asset;
  asset.id = id;
  asset.name = definition.name;
  asset.bones.reserve(definition.bones.size());

  HashMap<std::string, u32> bone_indices;
  for (const EntityModelBoneDefinition& bone : definition.bones) {
    const u32 bone_index = static_cast<u32>(asset.bones.size());
    if (!bone_indices.emplace(bone.name, bone_index).second) {
      return EntityModelAssetResult::failure({"duplicate entity model bone: " + bone.name});
    }
    asset.bones.push_back({bone.name, bone.pivot});
  }

  for (u32 bone_index = 0; bone_index < definition.bones.size(); ++bone_index) {
    const EntityModelBoneDefinition& bone = definition.bones[bone_index];
    for (const EntityModelBoxDefinition& box : bone.boxes) {
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
            box,
            static_cast<EntityModelFace>(face_value),
            face,
            binding,
            texture_size);
      }
    }
  }

  asset.animations.reserve(definition.animations.size());
  for (const EntityModelAnimationDefinition& source_animation : definition.animations) {
    EntityModelAnimation animation;
    animation.name = source_animation.name;
    animation.length_seconds = source_animation.length_seconds;
    animation.loop = source_animation.loop;
    animation.keyframes.reserve(source_animation.keyframes.size());
    for (const EntityModelAnimationKeyframeDefinition& source_keyframe : source_animation.keyframes) {
      EntityModelAnimationKeyframe keyframe;
      keyframe.time_seconds = source_keyframe.time_seconds;
      for (const EntityModelAnimationBoneDefinition& source_bone : source_keyframe.bones) {
        const auto found = bone_indices.find(source_bone.bone_name);
        if (found == bone_indices.end()) {
          return EntityModelAssetResult::failure(
              {"animation references unknown bone: " + source_bone.bone_name});
        }
        keyframe.bones.push_back({found->second, source_bone.rotation_euler_degrees});
      }
      animation.keyframes.push_back(std::move(keyframe));
    }
    asset.animations.push_back(std::move(animation));
  }

  return EntityModelAssetResult::success(std::move(asset));
}

void sample_entity_model_animation(
    const EntityModelAsset& model,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations) {
  for (Vec3f& rotation : output_rotations) {
    rotation = {};
  }
  if (output_rotations.size() < model.bones.size()) {
    return;
  }

  const EntityModelAnimation* animation = find_animation(model, animation_name);
  if (animation == nullptr || animation->keyframes.empty()) {
    return;
  }

  f32 sample_time = time_seconds;
  if (animation->loop && animation->length_seconds > 0.0F) {
    sample_time = std::fmod(std::max(sample_time, 0.0F), animation->length_seconds);
  } else {
    sample_time = std::clamp(sample_time, 0.0F, animation->length_seconds);
  }

  const EntityModelAnimationKeyframe* previous = &animation->keyframes.front();
  const EntityModelAnimationKeyframe* next = &animation->keyframes.back();
  for (const EntityModelAnimationKeyframe& keyframe : animation->keyframes) {
    if (keyframe.time_seconds <= sample_time) {
      previous = &keyframe;
    }
    if (keyframe.time_seconds >= sample_time) {
      next = &keyframe;
      break;
    }
  }

  const f32 span = std::max(next->time_seconds - previous->time_seconds, 0.0001F);
  const f32 alpha = std::clamp((sample_time - previous->time_seconds) / span, 0.0F, 1.0F);
  for (u32 bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
    const Vec3f from = keyframe_rotation_for_bone(*previous, bone_index).value_or(Vec3f{});
    const Vec3f to = keyframe_rotation_for_bone(*next, bone_index).value_or(Vec3f{});
    output_rotations[bone_index] = lerp(from, to, alpha);
  }
}

} // namespace arv::render
