#pragma once

#include "arv/core/expected.hpp"
#include "arv/render/render_types.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::render {

enum class EntityModelFace : u8 {
  PositiveX,
  NegativeX,
  PositiveY,
  NegativeY,
  PositiveZ,
  NegativeZ,
};

struct EntityModelError {
  std::string message{};
};

struct EntityModelFaceDefinition {
  i32 texture_index{};
  Vec4i uv{};
};

struct EntityModelBoxDefinition {
  Vec3f min{};
  Vec3f max{};
  std::array<std::optional<EntityModelFaceDefinition>, 6> faces{};
};

struct EntityModelBoneDefinition {
  std::string name{};
  Vec3f pivot{};
  std::vector<EntityModelBoxDefinition> boxes{};
};

struct EntityModelAnimationBoneDefinition {
  std::string bone_name{};
  Vec3f rotation_euler_degrees{};
};

struct EntityModelAnimationKeyframeDefinition {
  f32 time_seconds{};
  std::vector<EntityModelAnimationBoneDefinition> bones{};
};

struct EntityModelAnimationDefinition {
  std::string name{};
  f32 length_seconds{};
  bool loop{};
  std::vector<EntityModelAnimationKeyframeDefinition> keyframes{};
};

struct EntityModelDefinition {
  std::string name{};
  std::vector<EntityModelBoneDefinition> bones{};
  std::vector<std::string> texture_assets{};
  std::vector<Vec2i> texture_sizes{};
  std::vector<EntityModelAnimationDefinition> animations{};
};

struct EntityModelTextureBinding {
  std::string texture_asset{};
  Vec2i atlas_pixel_min{};
  Vec2i atlas_pixel_max{};
  u32 source_width{};
  u32 source_height{};
};

enum EntityModelMeshVertexFlags : u32 {
  EntityModelMeshVertexFlagSkinBaseLayer = 1U << 0U,
  EntityModelMeshVertexFlagSkinDecorLayer = 1U << 1U,
};

struct EntityModelBone {
  std::string name{};
  Vec3f pivot{};
};

struct EntityModelMeshVertex {
  Vec3f position{};
  Vec2f uv{};
  Vec3f color{1.0F, 1.0F, 1.0F};
  u32 bone_index{};
  u32 flags{};
};

struct EntityModelAnimationBoneKey {
  u32 bone_index{};
  Vec3f rotation_euler_degrees{};
};

struct EntityModelAnimationKeyframe {
  f32 time_seconds{};
  std::vector<EntityModelAnimationBoneKey> bones{};
};

struct EntityModelAnimation {
  std::string name{};
  f32 length_seconds{};
  bool loop{};
  std::vector<EntityModelAnimationKeyframe> keyframes{};
};

struct EntityModelAsset {
  EntityModelID id{};
  std::string name{};
  std::vector<EntityModelBone> bones{};
  std::vector<EntityModelMeshVertex> vertices{};
  std::vector<u32> indices{};
  std::vector<EntityModelAnimation> animations{};
};

using EntityModelDefinitionResult = Expected<EntityModelDefinition, EntityModelError>;
using EntityModelAssetResult = Expected<EntityModelAsset, EntityModelError>;

[[nodiscard]] std::string normalize_entity_texture_asset(std::string_view texture);
[[nodiscard]] EntityModelDefinitionResult parse_entity_model_json(std::string_view text);
[[nodiscard]] EntityModelAssetResult build_entity_model_asset(
    EntityModelID id,
    const EntityModelDefinition& definition,
    std::span<const EntityModelTextureBinding> texture_bindings);
void sample_entity_model_animation(
    const EntityModelAsset& model,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations);

} // namespace arv::render
