#include "arv/client/item_ui_model.hpp"

#include "arv/io/data_document.hpp"
#include "arv/render/block_model_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] ui::UiImageData missing_icon_for(
    const asset::TextureAtlas& item_atlas,
    u32 item_atlas_width,
    u32 item_atlas_height) {
  const asset::TextureAtlasEntry* missing_entry =
      item_atlas.find("arvane:texture/missing");
  return missing_entry == nullptr
             ? ui::UiImageData{}
             : image_data_from_atlas_entry(
                   *missing_entry,
                   item_atlas_width,
                   item_atlas_height,
                   render::UiTextureSource::ItemAtlas);
}

inline constexpr std::array<voxel::BlockFace, 3> UiBlockModelFaces{{
    voxel::BlockFace::PositiveZ,
    voxel::BlockFace::PositiveX,
    voxel::BlockFace::PositiveY,
}};

[[nodiscard]] Vec2f project_block_model_corner(Vec3i corner) noexcept {
  return {
      17.0F + static_cast<f32>(corner.x - corner.z) * (14.0F / 16.0F),
      16.0F + static_cast<f32>(corner.x + corner.z) * (7.0F / 16.0F) -
          static_cast<f32>(corner.y) * (14.0F / 16.0F)};
}

[[nodiscard]] Vec4f block_model_face_tint(voxel::BlockFace face) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveY:
      return {1.0F, 1.0F, 1.0F, 1.0F};
    case voxel::BlockFace::PositiveX:
      return {0.82F, 0.82F, 0.82F, 1.0F};
    case voxel::BlockFace::PositiveZ:
      return {0.70F, 0.70F, 0.70F, 1.0F};
    case voxel::BlockFace::NegativeX:
    case voxel::BlockFace::NegativeY:
    case voxel::BlockFace::NegativeZ:
      break;
  }
  return {1.0F, 1.0F, 1.0F, 1.0F};
}

[[nodiscard]] std::optional<std::string> mason_marker_display_name(
    const player::ItemStack& stack) {
  if (!stack.data.has_value()) {
    return std::nullopt;
  }
  const io::DataValue* value =
      io::find_path(*stack.data, "arv_mason.display_name");
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return std::nullopt;
  }
  const std::string_view display_name = value->as_string_or({});
  if (display_name.empty()) {
    return std::nullopt;
  }
  return std::string{display_name};
}

[[nodiscard]] f32 block_model_face_depth(
    const std::array<Vec3i, 4>& corners) noexcept {
  f32 depth = 0.0F;
  for (const Vec3i corner : corners) {
    depth += static_cast<f32>(corner.x + corner.y + corner.z);
  }
  return depth * 0.25F;
}

void sort_projected_quad(
    std::array<Vec2f, 4>& points,
    std::array<Vec2f, 4>& uvs) {
  Vec2f center{};
  for (const Vec2f point : points) {
    center.x += point.x;
    center.y += point.y;
  }
  center.x *= 0.25F;
  center.y *= 0.25F;

  std::array<usize, 4> order{{0U, 1U, 2U, 3U}};
  std::sort(order.begin(), order.end(), [&](usize lhs, usize rhs) {
    const f32 lhs_angle =
        std::atan2(points[lhs].y - center.y, points[lhs].x - center.x);
    const f32 rhs_angle =
        std::atan2(points[rhs].y - center.y, points[rhs].x - center.x);
    return lhs_angle < rhs_angle;
  });

  std::array<Vec2f, 4> sorted_points{};
  std::array<Vec2f, 4> sorted_uvs{};
  for (usize index = 0; index < order.size(); ++index) {
    sorted_points[index] = points[order[index]];
    sorted_uvs[index] = uvs[order[index]];
  }
  points = sorted_points;
  uvs = sorted_uvs;
}

[[nodiscard]] ui::UiBlockModelQuadView model_quad_for_ui(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face,
    const voxel::BlockModelFaceRenderData& face_data,
    u32 block_atlas_width,
    u32 block_atlas_height) {
  const std::array<Vec3i, 4> corners =
      render::block_model_box_face_corners(box, face);
  std::array<Vec2f, 4> points{};
  for (usize index = 0; index < corners.size(); ++index) {
    points[index] = project_block_model_corner(corners[index]);
  }
  std::array<Vec2f, 4> uvs =
      normalized_uvs(face_data.uvs, block_atlas_width, block_atlas_height);
  sort_projected_quad(points, uvs);

  ui::UiBlockModelQuadView quad;
  quad.depth = block_model_face_depth(corners);
  quad.image.valid = true;
  quad.image.source = render::UiTextureSource::TerrainAtlas;
  quad.image.page = face_data.page;
  quad.image.uvs = uvs;
  quad.image.custom_points = true;
  quad.image.points = points;
  quad.image.tint = block_model_face_tint(face);
  return quad;
}

[[nodiscard]] ui::UiItemStackView stack_view_for_ui(
    const player::ItemStack& stack,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas,
    ui::UiImageData missing_icon) {
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  const u32 block_atlas_width =
      block_atlas.atlas.pages.empty() ? 1U : block_atlas.atlas.pages.front().image.width;
  const u32 block_atlas_height =
      block_atlas.atlas.pages.empty() ? 1U : block_atlas.atlas.pages.front().image.height;

  ui::UiItemStackView view;
  const player::ItemRecord* item = item_registry.item(stack.item);
  view.item_name = item == nullptr ? std::string{player::MissingItemName} : item->name;
  view.display_name = mason_marker_display_name(stack).value_or(
      item == nullptr ? view.item_name : item->display_name);
  view.count = stack.count;
  view.texture_icon = missing_icon;
  view.render_type = item == nullptr ? player::ItemRenderType::Texture : item->render_type;
  if (item != nullptr && item->render_type == player::ItemRenderType::Texture) {
    const asset::TextureAtlasEntry* entry = item_atlas.find(item->texture_asset);
    if (entry != nullptr) {
      view.texture_icon = image_data_from_atlas_entry(
          *entry,
          item_atlas_width,
          item_atlas_height,
          render::UiTextureSource::ItemAtlas);
    }
  } else if (item != nullptr && item->render_type == player::ItemRenderType::Block &&
             item->render_block_variant.has_value()) {
    const std::span<const voxel::BlockModelBoxRenderData> model_boxes =
        block_atlas.render_data.model_boxes(item->render_block_variant.value());
    if (!model_boxes.empty()) {
      for (const voxel::BlockModelBoxRenderData& box : model_boxes) {
        for (const voxel::BlockFace face : UiBlockModelFaces) {
          const auto& face_data = box.faces[voxel::face_index(face)];
          if (!face_data.has_value()) {
            continue;
          }
          view.block_model_quads.push_back(model_quad_for_ui(
              box,
              face,
              face_data.value(),
              block_atlas_width,
              block_atlas_height));
        }
      }
      std::stable_sort(
          view.block_model_quads.begin(),
          view.block_model_quads.end(),
          [](const ui::UiBlockModelQuadView& lhs,
             const ui::UiBlockModelQuadView& rhs) {
            return lhs.depth < rhs.depth;
          });
      if (!view.block_model_quads.empty()) {
        return view;
      }
    }

    for (const voxel::BlockFace face : voxel::AllBlockFaces) {
      const voxel::BlockFaceRenderData* face_data =
          block_atlas.render_data.face(item->render_block_variant.value(), face);
      if (face_data == nullptr) {
        continue;
      }
      ui::UiImageData face_image;
      face_image.valid = true;
      face_image.source = render::UiTextureSource::TerrainAtlas;
      face_image.page = face_data->page;
      face_image.uvs =
          normalized_uvs(face_data->uvs, block_atlas_width, block_atlas_height);
      view.block_faces[voxel::face_index(face)] = face_image;
    }
  }
  return view;
}

} // namespace

std::array<Vec2f, 4> normalized_uvs(
    const std::array<Vec2i, 4>& pixels,
    u32 width,
    u32 height) {
  const f32 inv_width = width == 0U ? 1.0F : 1.0F / static_cast<f32>(width);
  const f32 inv_height = height == 0U ? 1.0F : 1.0F / static_cast<f32>(height);
  std::array<Vec2f, 4> uvs{};
  for (usize index = 0; index < uvs.size(); ++index) {
    uvs[index] = {
        static_cast<f32>(pixels[index].x) * inv_width,
        static_cast<f32>(pixels[index].y) * inv_height};
  }
  return uvs;
}

ui::UiImageData image_data_from_atlas_entry(
    const asset::TextureAtlasEntry& entry,
    u32 page_width,
    u32 page_height,
    render::UiTextureSource source) {
  const std::array<Vec2i, 4> pixels{{
      entry.uv_min,
      {entry.uv_max.x, entry.uv_min.y},
      entry.uv_max,
      {entry.uv_min.x, entry.uv_max.y},
  }};

  ui::UiImageData image;
  image.valid = true;
  image.source = source;
  image.page = entry.page;
  image.uvs = normalized_uvs(pixels, page_width, page_height);
  return image;
}

asset::TextureAtlasResult build_item_texture_atlas(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions options,
    std::span<const asset::AssetID> extra_texture_assets) {
  std::vector<asset::AssetID> texture_ids;
  HashSet<std::string> seen;
  const auto add_texture = [&](std::string texture) {
    if (!texture.empty() && seen.insert(texture).second) {
      texture_ids.emplace_back(std::move(texture));
    }
  };
  add_texture("arvane:texture/missing");
  for (u32 index = 0; index < item_registry.item_count(); ++index) {
    const player::ItemRecord* item = item_registry.item(player::ItemID{index});
    if (item == nullptr || item->render_type != player::ItemRenderType::Texture) {
      continue;
    }
    add_texture(item->texture_asset);
  }
  for (const asset::AssetID& texture : extra_texture_assets) {
    add_texture(texture.value);
  }

  std::vector<asset::TextureAtlasInput> inputs;
  inputs.reserve(texture_ids.size());
  for (const asset::AssetID& texture_id : texture_ids) {
    auto handle = texture_id.value == "arvane:texture/missing"
                      ? asset::TextureLoadResult::success(resources.missing_texture())
                      : resources.load_texture(texture_id);
    if (!handle) {
      return asset::TextureAtlasResult::failure(handle.error());
    }

    const asset::TextureResource* resource = resources.texture(handle.value());
    if (resource == nullptr) {
      return asset::TextureAtlasResult::failure({
          texture_id.value,
          {},
          "build item texture atlas",
          "loaded texture handle did not resolve"});
    }
    inputs.emplace_back(texture_id, resource->image, resource->fallback);
  }

  return asset::build_texture_atlas(inputs, options);
}

std::vector<ui::UiItemStackView> inventory_slots_for_ui(
    const player::Inventory& inventory,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas) {
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  const ui::UiImageData missing_icon =
      missing_icon_for(item_atlas, item_atlas_width, item_atlas_height);

  std::vector<ui::UiItemStackView> slots;
  slots.reserve(inventory.slot_count());
  for (usize index = 0; index < inventory.slot_count(); ++index) {
    ui::UiItemStackView view;
    const std::optional<player::ItemStack>& stack = inventory.slot(index);
    if (stack.has_value()) {
      view = stack_view_for_ui(*stack, item_registry, item_atlas, block_atlas, missing_icon);
    }
    slots.emplace_back(std::move(view));
  }
  return slots;
}

std::optional<ui::UiItemStackView> item_stack_for_ui(
    const std::optional<player::ItemStack>& stack,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas) {
  if (!stack.has_value()) {
    return std::nullopt;
  }
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  return stack_view_for_ui(
      stack.value(),
      item_registry,
      item_atlas,
      block_atlas,
      missing_icon_for(item_atlas, item_atlas_width, item_atlas_height));
}

InventoryUiProjection project_inventory_ui(
    const InventoryUiProjectionInput& input) {
  InventoryUiProjection projection;
  if (!input.has_authoritative_inventory) {
    return projection;
  }

  projection.inventory_slots = inventory_slots_for_ui(
      input.inventory,
      input.item_registry,
      input.item_atlas,
      input.block_atlas);
  if (input.container_open) {
    projection.container_slots = inventory_slots_for_ui(
        input.container_inventory,
        input.item_registry,
        input.item_atlas,
        input.block_atlas);
  }
  projection.carried_stack = item_stack_for_ui(
      input.carried_stack,
      input.item_registry,
      input.item_atlas,
      input.block_atlas);
  return projection;
}

} // namespace arv::client
