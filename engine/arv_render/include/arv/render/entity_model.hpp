#pragma once

#include "arv/core/expected.hpp"
#include "arv/render/render_types.hpp"

#include <array>
#include <map>
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
  f32 rotation{};
};

struct EntityModelBoneDefinition {
  std::optional<std::string> parent{};
  Vec3f pivot{};
  Vec3f rotation{};
};

struct EntityModelBoxDefinition {
  std::string bone{};
  Vec3f min{};
  Vec3f max{};
  Vec3f origin{};
  Vec3f rotation{};
  f32 inflate{};
  std::array<std::optional<EntityModelFaceDefinition>, 6> faces{};
};

struct EntityModelDefinition {
  std::string name{};
  Vec3f center_origin{};
  std::map<std::string, EntityModelBoneDefinition, std::less<>> bones{};
  std::vector<EntityModelBoxDefinition> boxes{};
  std::vector<std::string> texture_assets{};
  std::vector<Vec2i> texture_sizes{};
};

struct EntityModelTextureBinding {
  std::string texture_asset{};
  Vec2i atlas_pixel_min{};
  Vec2i atlas_pixel_max{};
  u32 source_width{};
  u32 source_height{};
  TextureAtlasDomain atlas_domain{TextureAtlasDomain::Entities};
  u16 atlas_page{};
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
  u32 texture_domain{static_cast<u32>(TextureAtlasDomain::Entities)};
  u32 atlas_page{};
};

enum class EntityAnimationChannel : u8 {
  Rotation,
  Position,
  Scale,
};

enum class EntityAnimationInterpolation : u8 {
  Linear,
};

struct EntityAnimationKeyframe {
  f32 time_seconds{};
  std::string bone_name{};
  EntityAnimationChannel channel{EntityAnimationChannel::Rotation};
  Vec3f value{};
  EntityAnimationInterpolation interpolation{EntityAnimationInterpolation::Linear};
};

struct EntityAnimationClip {
  std::string name{};
  f32 length_seconds{};
  bool loop{};
  std::vector<EntityAnimationKeyframe> keyframes{};
};

struct EntityAnimationDefinition {
  std::vector<EntityAnimationClip> clips{};
};

struct EntityAnimationAsset {
  EntityAnimationID id{};
  std::string name{};
  std::vector<EntityAnimationClip> clips{};
};

struct EntityModelAsset {
  EntityModelID id{};
  std::string name{};
  Vec3f center_origin{};
  std::vector<EntityModelBone> bones{};
  std::vector<EntityModelMeshVertex> vertices{};
  std::vector<u32> indices{};
};

using EntityModelDefinitionResult = Expected<EntityModelDefinition, EntityModelError>;
using EntityModelAssetResult = Expected<EntityModelAsset, EntityModelError>;
using EntityAnimationDefinitionResult = Expected<EntityAnimationDefinition, EntityModelError>;
using EntityAnimationAssetResult = Expected<EntityAnimationAsset, EntityModelError>;

[[nodiscard]] std::string normalize_entity_texture_asset(std::string_view texture);
[[nodiscard]] EntityModelDefinitionResult parse_entity_model_json(std::string_view text);
[[nodiscard]] EntityAnimationDefinitionResult parse_entity_animation_json(
    std::string_view text,
    std::string_view diagnostic_path = {});
[[nodiscard]] EntityModelAssetResult build_entity_model_asset(
    EntityModelID id,
    const EntityModelDefinition& definition,
    std::span<const EntityModelTextureBinding> texture_bindings);
[[nodiscard]] EntityAnimationAssetResult build_entity_animation_asset(
    EntityAnimationID id,
    std::string name,
    EntityAnimationDefinition definition);
[[nodiscard]] std::optional<EntityModelError> validate_entity_animation_bindings(
    const EntityModelAsset& model,
    const EntityAnimationAsset& animation);
void sample_entity_model_animation(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations);
void sample_entity_model_animation(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::string_view animation_name,
    f32 time_seconds,
    std::span<Vec3f> output_rotations,
    std::span<Vec3f> output_positions);
void sample_entity_model_animations(
    const EntityModelAsset& model,
    const EntityAnimationAsset* animation,
    std::span<const EntityAnimationPlayback> animations,
    std::span<Vec3f> output_rotations,
    std::span<Vec3f> output_positions);

} // namespace arv::render
