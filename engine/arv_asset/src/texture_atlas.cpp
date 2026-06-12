#include "arv/asset/texture_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <sstream>
#include <utility>

namespace arv::asset {
namespace {

struct AtlasFreeRect {
  u32 x{};
  u32 y{};
  u32 width{};
  u32 height{};
};

struct AtlasAllocation {
  u32 x{};
  u32 y{};
  u32 width{};
  u32 height{};
};

struct AtlasPageBuilder {
  TextureAtlasPage page{};
  std::vector<AtlasFreeRect> free_rects{};
};

[[nodiscard]] bool is_power_of_two(u32 value) noexcept {
  return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] u32 mip_safe_max_level_for_padding(u32 padding) noexcept {
  u32 level = 0;
  u32 footprint = 1;
  while (footprint <= padding / 2U) {
    footprint *= 2U;
    ++level;
  }
  return level;
}

[[nodiscard]] u8 image_pixel_channel(const Image& image, u32 x, u32 y, usize channel) noexcept {
  const usize offset = (static_cast<usize>(y) * image.width + x) * image.channels + channel;
  return image.pixels[offset];
}

void write_rgba_pixel(Image& image, u32 x, u32 y, const std::array<u8, 4>& rgba) noexcept {
  const usize offset = (static_cast<usize>(y) * image.width + x) * 4U;
  image.pixels[offset + 0U] = rgba[0];
  image.pixels[offset + 1U] = rgba[1];
  image.pixels[offset + 2U] = rgba[2];
  image.pixels[offset + 3U] = rgba[3];
}

[[nodiscard]] std::array<u8, 4> read_rgba_pixel(const Image& image, u32 x, u32 y) noexcept {
  return {{
      image_pixel_channel(image, x, y, 0U),
      image_pixel_channel(image, x, y, 1U),
      image_pixel_channel(image, x, y, 2U),
      image_pixel_channel(image, x, y, 3U),
  }};
}

void copy_with_edge_padding(
    const Image& source,
    Image& destination,
    u32 dest_x,
    u32 dest_y,
    u32 padding) {
  const u32 padded_width = source.width + padding * 2U;
  const u32 padded_height = source.height + padding * 2U;
  for (u32 y = 0; y < padded_height; ++y) {
    const u32 source_y = std::clamp<i32>(
        static_cast<i32>(y) - static_cast<i32>(padding),
        0,
        static_cast<i32>(source.height) - 1);
    for (u32 x = 0; x < padded_width; ++x) {
      const u32 source_x = std::clamp<i32>(
          static_cast<i32>(x) - static_cast<i32>(padding),
          0,
          static_cast<i32>(source.width) - 1);
      write_rgba_pixel(
          destination,
          dest_x + x,
          dest_y + y,
          read_rgba_pixel(source, source_x, source_y));
    }
  }
}

[[nodiscard]] AtlasPageBuilder make_atlas_page(u32 size) {
  AtlasPageBuilder builder;
  builder.page.image.width = size;
  builder.page.image.height = size;
  builder.page.image.channels = 4;
  builder.page.image.pixels.assign(static_cast<usize>(size) * size * 4U, 0U);
  builder.free_rects.push_back({0, 0, size, size});
  return builder;
}

[[nodiscard]] std::optional<AtlasAllocation> allocate_from_page(
    AtlasPageBuilder& page,
    u32 width,
    u32 height) {
  for (usize index = 0; index < page.free_rects.size(); ++index) {
    const AtlasFreeRect free = page.free_rects[index];
    if (free.width < width || free.height < height) {
      continue;
    }

    page.free_rects.erase(page.free_rects.begin() + static_cast<std::ptrdiff_t>(index));
    if (free.width > width) {
      page.free_rects.push_back({
          free.x + width,
          free.y,
          free.width - width,
          height});
    }
    if (free.height > height) {
      page.free_rects.push_back({
          free.x,
          free.y + height,
          free.width,
          free.height - height});
    }
    return AtlasAllocation{free.x, free.y, width, height};
  }
  return std::nullopt;
}

[[nodiscard]] std::string block_texture_for_face(
    const voxel::BlockVariantRecord& variant,
    voxel::BlockFace face) {
  return variant.texture_assets[voxel::face_index(face)];
}

[[nodiscard]] std::array<Vec2i, 4> atlas_face_uvs(
    const TextureAtlasEntry& entry,
    const std::array<Vec2i, 4>& logical_uvs) {
  std::array<Vec2i, 4> uvs{};
  for (usize i = 0; i < uvs.size(); ++i) {
    const f32 u = static_cast<f32>(logical_uvs[i].x) / 16.0F;
    const f32 v = static_cast<f32>(logical_uvs[i].y) / 16.0F;
    uvs[i] = {
        entry.unpadded_pixel_min.x + static_cast<i32>(u * static_cast<f32>(entry.width)),
        entry.unpadded_pixel_min.y + static_cast<i32>(v * static_cast<f32>(entry.height))};
  }
  return uvs;
}

[[nodiscard]] std::array<Vec2i, 4> atlas_model_face_uvs(
    voxel::BlockFace face,
    const TextureAtlasEntry& entry,
    Vec4i logical_uv,
    Vec2i texture_size,
    f32 rotation) {
  const f32 logical_width = static_cast<f32>(std::max(texture_size.x, 1));
  const f32 logical_height = static_cast<f32>(std::max(texture_size.y, 1));
  const auto atlas_u = [&](i32 logical_u) {
    return entry.unpadded_pixel_min.x +
           static_cast<i32>(
               static_cast<f32>(logical_u) / logical_width *
               static_cast<f32>(entry.width));
  };
  const auto atlas_v = [&](i32 logical_v) {
    return entry.unpadded_pixel_min.y +
           static_cast<i32>(
               static_cast<f32>(logical_v) / logical_height *
               static_cast<f32>(entry.height));
  };

  const i32 u0 = atlas_u(logical_uv.x);
  const i32 v0 = atlas_v(logical_uv.y);
  const i32 u1 = atlas_u(logical_uv.z);
  const i32 v1 = atlas_v(logical_uv.w);
  std::array<Vec2i, 4> uvs{};
  switch (face) {
    case voxel::BlockFace::PositiveX:
    case voxel::BlockFace::NegativeX:
    case voxel::BlockFace::PositiveY:
    case voxel::BlockFace::PositiveZ:
      uvs = {{{u0, v1}, {u1, v1}, {u0, v0}, {u1, v0}}};
      break;
    case voxel::BlockFace::NegativeY:
      uvs = {{{u1, v0}, {u0, v0}, {u1, v1}, {u0, v1}}};
      break;
    case voxel::BlockFace::NegativeZ:
      uvs = {{{u1, v1}, {u0, v1}, {u1, v0}, {u0, v0}}};
      break;
  }
  const i32 turns = static_cast<i32>(std::lround(rotation / 90.0F)) % 4;
  for (i32 turn = 0; turn < (turns + 4) % 4; ++turn) {
    uvs = std::array<Vec2i, 4>{{uvs[2], uvs[0], uvs[3], uvs[1]}};
  }
  return uvs;
}

[[nodiscard]] Vec3i model_coord_to_render_coord(Vec3f value) noexcept {
  return {
      static_cast<i32>(std::lround(value.x)),
      static_cast<i32>(std::lround(value.y)),
      static_cast<i32>(std::lround(value.z))};
}

} // namespace

const TextureAtlasEntry* TextureAtlas::find(std::string_view asset_id) const noexcept {
  const auto found = entry_by_asset_id.find(std::string{asset_id});
  if (found == entry_by_asset_id.end() || found->second >= entries.size()) {
    return nullptr;
  }
  return &entries[found->second];
}

TextureAtlasResult build_texture_atlas(
    std::span<const TextureAtlasInput> inputs,
    TextureAtlasBuildOptions options) {
  if (inputs.empty()) {
    return TextureAtlasResult::failure({{}, {}, "build texture atlas", "atlas has no inputs"});
  }
  if (options.max_page_size == 0 || options.max_pages == 0 ||
      options.padding > options.max_page_size / 4U) {
    return TextureAtlasResult::failure({{}, {}, "build texture atlas", "invalid atlas sizing options"});
  }

  u32 base_width = 0;
  u32 base_height = 0;
  for (const TextureAtlasInput& input : inputs) {
    if (input.image.empty() || input.image.channels != 4) {
      return TextureAtlasResult::failure({input.asset_id.value,
                                          {},
                                          "build texture atlas",
                                          "input texture must be a non-empty RGBA image"});
    }
    if (!is_power_of_two(input.image.width) || !is_power_of_two(input.image.height)) {
      return TextureAtlasResult::failure({input.asset_id.value,
                                          {},
                                          "build texture atlas",
                                          "block texture dimensions must be power-of-two"});
    }
    if (options.require_uniform_non_fallback_size && !input.fallback) {
      if (base_width == 0 && base_height == 0) {
        base_width = input.image.width;
        base_height = input.image.height;
      } else if (input.image.width != base_width || input.image.height != base_height) {
        std::ostringstream message;
        message << "texture size " << input.image.width << "x" << input.image.height
                << " does not match required block texture size "
                << base_width << "x" << base_height;
        return TextureAtlasResult::failure({input.asset_id.value,
                                            {},                                            "build texture atlas",
                                            message.str()});
      }
    }
    if (input.image.width + options.padding * 2U > options.max_page_size ||
        input.image.height + options.padding * 2U > options.max_page_size) {
      return TextureAtlasResult::failure({input.asset_id.value,
                                          {},
                                          "build texture atlas",
                                          "texture plus padding exceeds atlas page size"});
    }
  }

  TextureAtlas atlas;
  atlas.padding = options.padding;
  atlas.mip_safe_max_level = mip_safe_max_level_for_padding(options.padding);
  atlas.entries.resize(inputs.size());

  std::vector<usize> order(inputs.size());
  std::iota(order.begin(), order.end(), 0U);
  std::stable_sort(order.begin(), order.end(), [&](usize lhs, usize rhs) {
    const TextureAtlasInput& lhs_input = inputs[lhs];
    const TextureAtlasInput& rhs_input = inputs[rhs];
    const u32 lhs_width = lhs_input.image.width + options.padding * 2U;
    const u32 lhs_height = lhs_input.image.height + options.padding * 2U;
    const u32 rhs_width = rhs_input.image.width + options.padding * 2U;
    const u32 rhs_height = rhs_input.image.height + options.padding * 2U;
    const u64 lhs_area = static_cast<u64>(lhs_width) * lhs_height;
    const u64 rhs_area = static_cast<u64>(rhs_width) * rhs_height;
    if (lhs_area != rhs_area) {
      return lhs_area > rhs_area;
    }
    return std::max(lhs_width, lhs_height) > std::max(rhs_width, rhs_height);
  });

  std::vector<AtlasPageBuilder> pages;
  pages.reserve(options.max_pages);
  for (const usize input_index : order) {
    const TextureAtlasInput& input = inputs[input_index];
    const u32 padded_width = input.image.width + options.padding * 2U;
    const u32 padded_height = input.image.height + options.padding * 2U;

    std::optional<AtlasAllocation> allocation;
    u32 page_index = 0;
    for (; page_index < pages.size(); ++page_index) {
      allocation = allocate_from_page(pages[page_index], padded_width, padded_height);
      if (allocation.has_value()) {
        break;
      }
    }
    if (!allocation.has_value()) {
      if (pages.size() >= options.max_pages) {
        return TextureAtlasResult::failure({input.asset_id.value,
                                            {},
                                            "build texture atlas",
                                            "atlas inputs exceeded configured page count"});
      }
      pages.push_back(make_atlas_page(options.max_page_size));
      page_index = static_cast<u32>(pages.size() - 1U);
      allocation = allocate_from_page(pages.back(), padded_width, padded_height);
    }
    if (!allocation.has_value()) {
      return TextureAtlasResult::failure({input.asset_id.value,
                                          {},
                                          "build texture atlas",
                                          "atlas page allocator could not place texture"});
    }

    copy_with_edge_padding(
        input.image,
        pages[page_index].page.image,
        allocation->x,
        allocation->y,
        options.padding);

    TextureAtlasEntry entry;
    entry.asset_id = input.asset_id;
    entry.page = page_index;
    entry.texture_index = static_cast<u32>(input_index);
    entry.padded_pixel_min = {
        static_cast<i32>(allocation->x),
        static_cast<i32>(allocation->y)};
    entry.padded_pixel_max = {
        static_cast<i32>(allocation->x + allocation->width),
        static_cast<i32>(allocation->y + allocation->height)};
    entry.unpadded_pixel_min = {
        static_cast<i32>(allocation->x + options.padding),
        static_cast<i32>(allocation->y + options.padding)};
    entry.unpadded_pixel_max = {
        entry.unpadded_pixel_min.x + static_cast<i32>(input.image.width),
        entry.unpadded_pixel_min.y + static_cast<i32>(input.image.height)};
    entry.uv_min = entry.unpadded_pixel_min;
    entry.uv_max = entry.unpadded_pixel_max;
    entry.width = input.image.width;
    entry.height = input.image.height;
    entry.fallback = input.fallback;
    atlas.entry_by_asset_id[entry.asset_id.value] = static_cast<u32>(input_index);
    atlas.entries[input_index] = std::move(entry);
  }

  atlas.pages.reserve(pages.size());
  for (AtlasPageBuilder& page : pages) {
    atlas.pages.push_back(std::move(page.page));
  }
  return TextureAtlasResult::success(std::move(atlas));
}

BlockModelAssetDataResult load_block_model_definitions(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry) {
  std::vector<BlockModelAssetData> block_models;
  HashMap<std::string, voxel::BlockModelDefinition> model_cache;

  const auto cache_key_for = [](const voxel::BlockModelReference& model) {
    std::string key = model.asset;
    for (const std::string& input : model.texture_inputs) {
      key += '\n';
      key += input;
    }
    return key;
  };

  for (u32 variant_value = 0; variant_value < registry.variant_count(); ++variant_value) {
    const auto variant_id = voxel::BlockVariantID{variant_value};
    const auto* variant = registry.variant(variant_id);
    if (variant == nullptr || variant->model.asset.empty()) {
      continue;
    }

    const std::string cache_key = cache_key_for(variant->model);
    auto cached = model_cache.find(cache_key);
    if (cached == model_cache.end()) {
      auto model_text = resources.load_text({variant->model.asset}, AssetKind::Model);
      if (!model_text) {
        return BlockModelAssetDataResult::failure(model_text.error());
      }
      auto model_definition =
          voxel::parse_block_model_json(model_text.value(), variant->model.texture_inputs);
      if (!model_definition) {
        return BlockModelAssetDataResult::failure({variant->model.asset,
                                                   resources.resolve_asset_path({variant->model.asset}, AssetKind::Model),
                                                   "parse block model",
                                                   model_definition.error().message});
      }
      cached = model_cache.emplace(cache_key, std::move(model_definition.value())).first;
    }
    block_models.push_back({variant_id, cached->second});
  }

  return BlockModelAssetDataResult::success(std::move(block_models));
}

void apply_block_model_collision_boxes(
    voxel::BlockRegistry& registry,
    std::span<const BlockModelAssetData> block_models) {
  for (const BlockModelAssetData& block_model : block_models) {
    const voxel::BlockVariantRecord* variant = registry.variant(block_model.variant_id);
    if (variant == nullptr) {
      continue;
    }
    if (!variant->properties.collidable) {
      registry.set_collision_boxes(block_model.variant_id, {});
      continue;
    }
    registry.set_collision_boxes(
        block_model.variant_id,
        voxel::make_block_model_collision_boxes(
            block_model.definition,
            variant->model_transform));
  }
}

BlockModelRenderBoxesResult build_block_model_render_boxes(
    const TextureAtlas& atlas,
    const voxel::BlockModelDefinition& model,
    std::string_view operation) {
  std::vector<voxel::BlockModelBoxRenderData> render_boxes;
  render_boxes.reserve(model.boxes.size());
  for (const voxel::BlockModelBoxDefinition& box : model.boxes) {
    voxel::BlockModelBoxRenderData render_box;
    render_box.min = model_coord_to_render_coord(box.min);
    render_box.max = model_coord_to_render_coord(box.max);
    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      const usize face_slot = voxel::face_index(face);
      if (!box.faces[face_slot].has_value()) {
        continue;
      }
      const voxel::BlockModelFaceDefinition& face_definition =
          box.faces[face_slot].value();
      if (face_definition.texture_index < 0) {
        continue;
      }

      const usize texture_index = static_cast<usize>(face_definition.texture_index);
      std::string texture = model.texture_assets[texture_index];
      if (texture.empty()) {
        texture = "arvane:texture/missing";
      }
      const TextureAtlasEntry* entry = atlas.find(texture);
      if (entry == nullptr) {
        return BlockModelRenderBoxesResult::failure({texture,
                                                     {},
                                                     std::string{operation},
                                                     "texture was not present in final atlas"});
      }

      voxel::BlockModelFaceRenderData face_data;
      face_data.page = entry->page;
      face_data.texture_index = entry->texture_index;
      face_data.uvs = atlas_model_face_uvs(
          face,
          *entry,
          face_definition.uv,
          model.texture_sizes[texture_index],
          face_definition.rotation);
      render_box.faces[face_slot] = face_data;
    }
    render_boxes.push_back(std::move(render_box));
  }

  return BlockModelRenderBoxesResult::success(std::move(render_boxes));
}

BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    TextureAtlasBuildOptions options) {
  auto block_models = load_block_model_definitions(resources, registry);
  if (!block_models) {
    return BlockTextureAtlasResult::failure(block_models.error());
  }
  return build_block_texture_atlas(resources, registry, block_models.value(), options);
}

BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    std::span<const BlockModelAssetData> block_models,
    TextureAtlasBuildOptions options) {
  std::vector<AssetID> texture_ids;
  HashSet<std::string> seen;
  std::vector<const voxel::BlockModelDefinition*> block_model_by_variant(registry.variant_count(), nullptr);
  for (const BlockModelAssetData& block_model : block_models) {
    if (block_model.variant_id.value < block_model_by_variant.size()) {
      block_model_by_variant[block_model.variant_id.value] = &block_model.definition;
    }
  }

  const auto add_texture = [&](std::string texture) {
    if (texture.empty()) {
      texture = "arvane:texture/missing";
    }
    if (seen.insert(texture).second) {
      texture_ids.push_back({std::move(texture)});
    }
  };

  for (u32 variant_value = 0; variant_value < registry.variant_count(); ++variant_value) {
    const auto* variant = registry.variant(voxel::BlockVariantID{variant_value});
    if (variant == nullptr || variant->render_category == voxel::RenderCategory::None) {
      continue;
    }

    if (!variant->model.asset.empty()) {
      const voxel::BlockModelDefinition* model = block_model_by_variant[variant_value];
      if (model == nullptr) {
        return BlockTextureAtlasResult::failure({variant->model.asset,
                                                 resources.resolve_asset_path({variant->model.asset}, AssetKind::Model),
                                                 "build block texture atlas",
                                                 "block model was not loaded"});
      }
      for (const std::string& texture : model->texture_assets) {
        add_texture(texture);
      }
      continue;
    }

    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      add_texture(block_texture_for_face(*variant, face));
    }
  }

  if (texture_ids.empty()) {
    texture_ids.push_back({"arvane:texture/missing"});
  }

  std::vector<TextureAtlasInput> inputs;
  inputs.reserve(texture_ids.size());
  for (const AssetID& texture_id : texture_ids) {
    auto handle = texture_id.value == "arvane:texture/missing"
                      ? TextureLoadResult::success(resources.missing_texture())
                      : resources.load_texture(texture_id);
    if (!handle) {
      return BlockTextureAtlasResult::failure(handle.error());
    }

    const TextureResource* resource = resources.texture(handle.value());
    if (resource == nullptr) {
      return BlockTextureAtlasResult::failure({texture_id.value,
                                               {},
                                               "build block texture atlas",
                                               "loaded texture handle did not resolve"});
    }
    inputs.push_back({texture_id, resource->image, resource->fallback});
  }

  auto atlas_result = build_texture_atlas(inputs, options);
  if (!atlas_result) {
    return BlockTextureAtlasResult::failure(atlas_result.error());
  }

  BlockTextureAtlas result;
  result.atlas = std::move(atlas_result.value());
  result.render_data = voxel::BlockRenderData::make_empty(registry.variant_count());

  for (u32 variant_value = 0; variant_value < registry.variant_count(); ++variant_value) {
    const auto variant_id = voxel::BlockVariantID{variant_value};
    const auto* variant = registry.variant(variant_id);
    if (variant == nullptr || variant->render_category == voxel::RenderCategory::None) {
      continue;
    }

    if (block_model_by_variant[variant_value] != nullptr) {
      auto render_boxes = build_block_model_render_boxes(
          result.atlas,
          *block_model_by_variant[variant_value],
          "build block model render data");
      if (!render_boxes) {
        return BlockTextureAtlasResult::failure(render_boxes.error());
      }
      const Vec3i center_origin =
          model_coord_to_render_coord(block_model_by_variant[variant_value]->center_origin);
      std::vector<voxel::BlockModelBoxRenderData> boxes =
          std::move(render_boxes.value());
      for (voxel::BlockModelBoxRenderData& box : boxes) {
        box = voxel::transform_model_box(box, center_origin, variant->model_transform);
      }
      result.render_data.set_model_center_origin(
          variant_id,
          center_origin);
      result.render_data.set_model_boxes(variant_id, std::move(boxes));
      continue;
    }

    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      std::string texture = block_texture_for_face(*variant, face);
      if (texture.empty()) {
        texture = "arvane:texture/missing";
      }
      const TextureAtlasEntry* entry = result.atlas.find(texture);
      if (entry == nullptr) {
        return BlockTextureAtlasResult::failure({texture,
                                                 {},
                                                 "build block texture atlas",
                                                 "texture was not present in final atlas"});
      }
      voxel::BlockFaceRenderData face_data;
      face_data.page = entry->page;
      face_data.texture_index = entry->texture_index;
      face_data.uvs = atlas_face_uvs(*entry, voxel::default_cube_face_uvs(face));
      result.render_data.set_face(variant_id, face, face_data);
    }
  }

  return BlockTextureAtlasResult::success(std::move(result));
}

} // namespace arv::asset
