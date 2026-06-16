#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
inline constexpr std::array<BlockFace, BlockFaceCount> AllBlockFaces{{
    BlockFace::PositiveX,
    BlockFace::NegativeX,
    BlockFace::PositiveY,
    BlockFace::NegativeY,
    BlockFace::PositiveZ,
    BlockFace::NegativeZ,
}};

[[nodiscard]] constexpr Vec3i block_face_offset(BlockFace face) noexcept {
  switch (face) {
    case BlockFace::PositiveX:
      return {1, 0, 0};
    case BlockFace::NegativeX:
      return {-1, 0, 0};
    case BlockFace::PositiveY:
      return {0, 1, 0};
    case BlockFace::NegativeY:
      return {0, -1, 0};
    case BlockFace::PositiveZ:
      return {0, 0, 1};
    case BlockFace::NegativeZ:
      return {0, 0, -1};
  }
  return {};
}

enum class RenderCategory : u8 {
  None,
  Opaque,
  Cutout,
  Transparent,
};

enum class BlockRenderMode : u8 {
  Terrain,
  BlockEntityModel,
};

enum class BlockRenderTint : u8 {
  None,
  Foliage,
};

enum class BlockDropKind : u8 {
  Default,
  None,
  Item,
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
  bool ao_occluder{};
  bool drops_with_wrong_tool{};
  f32 hardness{};
  u8 emission_r{};
  u8 emission_g{};
  u8 emission_b{};
};

struct BlockBlastProperties {
  f32 resistance{1.0F};
  bool destroyable_by_explosions{true};
  bool absorbs_blast{};
  bool flammable{};
  bool drops_items{true};
};

inline constexpr f32 DefaultBlockSlipperiness = 0.6F;

struct BlockPhysicsProperties {
  f32 slipperiness{DefaultBlockSlipperiness};
};

struct BlockDropDefinition {
  BlockDropKind kind{BlockDropKind::Default};
  std::string item_name{};
};

struct StatePropertyDefinition {
  std::string name{};
  std::vector<std::string> values{};
};

struct BlockStateValue {
  std::string name{};
  std::string value{};
};

struct BlockModelReference {
  std::string asset{};
  std::vector<std::string> texture_inputs{};
};

struct BlockDefinition {
  std::string name{};
  std::string display_name{};
  std::string behavior_id{};
  std::vector<std::string> tags{};
  BlockProperties properties{};
  BlockBlastProperties blast{};
  BlockPhysicsProperties physics{};
  BlockDropDefinition drops{};
  RenderCategory render_category{RenderCategory::None};
  BlockRenderMode render_mode{BlockRenderMode::Terrain};
  bool is_item{true};
  f32 item_buoyancy{1.0F};
  bool hide_same_variant_faces{true};
  BlockRenderTint render_tint{BlockRenderTint::None};
  std::array<std::string, BlockFaceCount> texture_assets{};
  std::string particle_texture_asset{};
  std::string container_type{};
  BlockModelReference model{};
  std::vector<StatePropertyDefinition> states{};
};

struct BlockRecord {
  BlockID id{};
  std::string name{};
  std::string display_name{};
  std::string behavior_id{};
  std::vector<std::string> tags{};
  BlockProperties properties{};
  BlockBlastProperties blast{};
  BlockPhysicsProperties physics{};
  BlockDropDefinition drops{};
  std::string container_type{};
  bool is_item{true};
  f32 item_buoyancy{1.0F};
  std::string particle_texture_asset{};
  BlockVariantID default_variant{};
  std::vector<BlockVariantID> variants{};
};

struct BlockModelTransform {
  i8 quarter_turns_y{};
  bool mirror_y{};
};

struct BlockBody {
  std::vector<Aabb3f> boxes{};
};

struct BlockVariantRecord {
  BlockVariantID id{};
  BlockID block_id{};
  std::string debug_name{};
  std::string behavior_id{};
  std::vector<BlockStateValue> state_values{};
  BlockProperties properties{};
  BlockBlastProperties blast{};
  BlockPhysicsProperties physics{};
  RenderCategory render_category{RenderCategory::None};
  BlockRenderMode render_mode{BlockRenderMode::Terrain};
  BlockRenderTint render_tint{BlockRenderTint::None};
  bool hide_same_variant_faces{};
  std::array<std::string, BlockFaceCount> texture_assets{};
  std::string particle_texture_asset{};
  std::string container_type{};
  BlockModelReference model{};
  BlockModelTransform model_transform{};
  u32 block_body_index{};
  std::array<bool, BlockFaceCount> renders_face{};
  std::array<bool, BlockFaceCount> occludes_face{};
};

struct AuthoredModelFaceDefinition {
  i32 texture_index{};
  Vec4i uv{};
  f32 rotation{};
};

struct AuthoredModelBoneDefinition {
  std::optional<std::string> parent{};
  Vec3f pivot{};
  Vec3f rotation{};
};

struct AuthoredModelBoxDefinition {
  std::string bone{};
  Vec3f min{};
  Vec3f max{};
  Vec3f origin{};
  Vec3f rotation{};
  f32 inflate{};
  std::array<std::optional<AuthoredModelFaceDefinition>, BlockFaceCount> faces{};
};

struct AuthoredModelDefinition {
  std::string name{};
  Vec3f center_origin{8.0F, 8.0F, 8.0F};
  std::map<std::string, AuthoredModelBoneDefinition, std::less<>> bones{};
  std::vector<AuthoredModelBoxDefinition> boxes{};
  std::vector<std::string> texture_assets{};
  std::vector<Vec2i> texture_sizes{};
};

using BlockModelFaceDefinition = AuthoredModelFaceDefinition;
using BlockModelBoxDefinition = AuthoredModelBoxDefinition;
using BlockModelDefinition = AuthoredModelDefinition;

struct BlockFaceRenderData {
  u32 page{};
  u32 texture_index{};
  std::array<Vec2i, 4> uvs{};
};

struct BlockModelFaceRenderData {
  u32 page{};
  u32 texture_index{};
  std::array<Vec2i, 4> uvs{};
};

struct BlockModelBoxRenderData {
  Vec3i min{};
  Vec3i max{};
  std::array<std::optional<BlockModelFaceRenderData>, BlockFaceCount> faces{};
};

struct BlockVariantRenderData {
  std::array<BlockFaceRenderData, BlockFaceCount> faces{};
  Vec3i model_center_origin{8, 8, 8};
  std::vector<BlockModelBoxRenderData> model_boxes{};
};

using BlockDefinitionsResult = Expected<std::vector<BlockDefinition>, BlockRegistryError>;
using AuthoredModelDefinitionResult = Expected<AuthoredModelDefinition, BlockRegistryError>;
using BlockModelDefinitionResult = Expected<BlockModelDefinition, BlockRegistryError>;

[[nodiscard]] constexpr usize face_index(BlockFace face) noexcept {
  return static_cast<usize>(face);
}

[[nodiscard]] constexpr std::array<Vec2i, 4> default_cube_face_uvs(BlockFace face) noexcept {
  switch (face) {
    case BlockFace::PositiveX:
    case BlockFace::NegativeX:
    case BlockFace::PositiveY:
    case BlockFace::PositiveZ:
      return {{{0, 16}, {16, 16}, {0, 0}, {16, 0}}};
    case BlockFace::NegativeY:
      return {{{16, 0}, {0, 0}, {16, 16}, {0, 16}}};
    case BlockFace::NegativeZ:
      return {{{16, 16}, {0, 16}, {16, 0}, {0, 0}}};
  }
  return {{{0, 16}, {16, 16}, {0, 0}, {16, 0}}};
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

struct BlockBehaviorHandler {};

class BlockBehaviorRegistry {
public:
  using Result = Expected<void, BlockRegistryError>;

  [[nodiscard]] Result register_handler(std::string id, BlockBehaviorHandler handler = {});
  [[nodiscard]] const BlockBehaviorHandler* find(std::string_view id) const noexcept;
  [[nodiscard]] bool contains(std::string_view id) const noexcept;

private:
  std::vector<std::pair<std::string, BlockBehaviorHandler>> handlers_;
};

struct BlockRegistryBuildOptions {
  FunctionRef<bool(std::string_view)> behavior_exists{};
};

class BlockRegistry {
public:
  using Result = Expected<BlockRegistry, BlockRegistryError>;

  [[nodiscard]] static Result build(
      std::vector<BlockDefinition> definitions,
      BlockRegistryBuildOptions options = {});

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

  [[nodiscard]] const BlockRecord* block(BlockID id) const noexcept;

  [[nodiscard]] const BlockVariantRecord* variant(BlockVariantID id) const noexcept;

  [[nodiscard]] std::optional<BlockID> find_block(std::string_view name) const;

  [[nodiscard]] std::optional<BlockVariantID> find_variant(std::string_view debug_name) const;

  [[nodiscard]] bool has_tag(BlockID id, std::string_view tag) const;

  void set_display_name(BlockID id, std::string display_name);

  [[nodiscard]] BlockID block_id(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] std::string_view variant_name(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] const BlockBody* block_body(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] std::span<const Aabb3f> collision_boxes(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] BlockPhysicsProperties block_physics(BlockVariantID variant_id) const noexcept;

  void set_collision_boxes(BlockVariantID variant_id, std::vector<Aabb3f> boxes);

  [[nodiscard]] bool is_replaceable(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] bool should_emit_face(
      BlockVariantID self_id,
      BlockVariantID neighbor_id,
      BlockFace face) const noexcept;

private:
  [[nodiscard]] static std::optional<std::string> validate_states(const BlockDefinition& definition);

  [[nodiscard]] static std::optional<std::string> validate_air_definition(const BlockDefinition& definition);

  std::vector<BlockRecord> blocks_;
  std::vector<BlockVariantRecord> variants_;
  std::vector<BlockBody> block_bodies_;
  HashMap<std::string, BlockID> block_name_to_id_;
  HashMap<std::string, BlockVariantID> variant_name_to_id_;
};

class BlockRenderData {
public:
  [[nodiscard]] static BlockRenderData make_empty(usize variant_count);

  [[nodiscard]] const BlockFaceRenderData* face(
      BlockVariantID variant_id,
      BlockFace block_face) const noexcept;

  void set_face(BlockVariantID variant_id, BlockFace block_face, BlockFaceRenderData data);

  [[nodiscard]] std::span<const BlockModelBoxRenderData> model_boxes(
      BlockVariantID variant_id) const noexcept;

  [[nodiscard]] Vec3i model_center_origin(BlockVariantID variant_id) const noexcept;

  void set_model_boxes(
      BlockVariantID variant_id,
      std::vector<BlockModelBoxRenderData> boxes);

  void set_model_center_origin(BlockVariantID variant_id, Vec3i center_origin);

  [[nodiscard]] usize variant_count() const noexcept {
    return variants_.size();
  }

private:
  std::vector<BlockVariantRenderData> variants_{};
};

[[nodiscard]] bool block_tag_exists(
    const BlockRegistry& registry,
    std::string_view tag);

[[nodiscard]] bool block_variant_has_any_tag(
    const BlockRegistry& registry,
    BlockVariantID variant,
    std::span<const std::string> tags);

[[nodiscard]] bool is_standing_surface(
    const BlockRegistry& registry,
    BlockVariantID variant) noexcept;

[[nodiscard]] BlockDefinition make_air_definition();

[[nodiscard]] std::string normalize_block_model_asset(std::string_view model);

[[nodiscard]] std::string normalize_block_texture_asset(std::string_view texture);

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
  definition.properties.ao_occluder = category == RenderCategory::Opaque;
  definition.render_category = category;
  return definition;
}

[[nodiscard]] BlockDefinitionsResult parse_block_definitions_json(std::string_view text);

[[nodiscard]] AuthoredModelDefinitionResult parse_authored_model_json(std::string_view text);

[[nodiscard]] BlockModelDefinitionResult parse_block_model_json(
    std::string_view text,
    std::span<const std::string> texture_inputs = {});

[[nodiscard]] BlockFace transform_block_face(
    BlockFace face,
    BlockModelTransform transform) noexcept;

[[nodiscard]] Vec3f block_model_coord_to_block_coord(Vec3f point) noexcept;
[[nodiscard]] Vec3i block_model_coord_to_block_coord(Vec3i point) noexcept;

[[nodiscard]] Vec3i transform_model_point(
    Vec3i point,
    Vec3i center_origin,
    BlockModelTransform transform) noexcept;

[[nodiscard]] BlockModelBoxRenderData transform_model_box(
    const BlockModelBoxRenderData& box,
    Vec3i center_origin,
    BlockModelTransform transform) noexcept;

[[nodiscard]] std::vector<Aabb3f> make_block_model_collision_boxes(
    const BlockModelDefinition& model,
    BlockModelTransform transform = {});
[[nodiscard]] std::vector<Aabb3f> make_block_attached_model_collision_boxes(
    const AuthoredModelDefinition& model,
    BlockModelTransform transform = {});

} // namespace arv::voxel
