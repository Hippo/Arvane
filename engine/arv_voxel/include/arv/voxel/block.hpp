#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
  bool ao_occluder{};
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
  std::array<std::string, BlockFaceCount> texture_assets{};
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
  std::array<std::string, BlockFaceCount> texture_assets{};
  std::array<bool, BlockFaceCount> renders_face{};
  std::array<bool, BlockFaceCount> occludes_face{};
  std::vector<Aabb3f> collision_boxes{};
};

struct BlockFaceRenderData {
  u32 page{};
  u32 texture_index{};
  std::array<Vec2i, 4> uvs{};
};

struct BlockVariantRenderData {
  std::array<BlockFaceRenderData, BlockFaceCount> faces{};
};

using BlockDefinitionsResult = Expected<std::vector<BlockDefinition>, BlockRegistryError>;

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

class BlockRegistry {
public:
  using Result = Expected<BlockRegistry, BlockRegistryError>;

  [[nodiscard]] static Result build(std::vector<BlockDefinition> definitions);

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

  [[nodiscard]] BlockID block_id(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] std::string_view variant_name(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] std::span<const Aabb3f> collision_boxes(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] bool is_replaceable(BlockVariantID variant_id) const noexcept;

  [[nodiscard]] bool should_emit_face(
      BlockVariantID self_id,
      BlockVariantID neighbor_id,
      BlockFace face) const noexcept;

private:
  [[nodiscard]] static std::optional<std::string> validate_states(const BlockDefinition& definition);

  [[nodiscard]] static std::optional<std::string> validate_air_definition(const BlockDefinition& definition);

  [[nodiscard]] static std::vector<std::string> expand_variant_names(const BlockDefinition& definition);

  std::vector<BlockRecord> blocks_;
  std::vector<BlockVariantRecord> variants_;
  std::unordered_map<std::string, BlockID> block_name_to_id_;
  std::unordered_map<std::string, BlockVariantID> variant_name_to_id_;
};

class BlockRenderData {
public:
  [[nodiscard]] static BlockRenderData make_empty(usize variant_count);

  [[nodiscard]] const BlockFaceRenderData* face(
      BlockVariantID variant_id,
      BlockFace block_face) const noexcept;

  void set_face(BlockVariantID variant_id, BlockFace block_face, BlockFaceRenderData data);

  [[nodiscard]] usize variant_count() const noexcept {
    return variants_.size();
  }

private:
  std::vector<BlockVariantRenderData> variants_{};
};

[[nodiscard]] BlockDefinition make_air_definition();

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

} // namespace arv::voxel
