#include "arv/asset/texture_atlas.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <rectpack2D/finders_interface.h>

namespace arv::asset {
namespace {

class AtlasSubject {
public:
  AtlasSubject(rectpack2D::rect_xywh rect, usize input_index)
      : rect_(rect), input_index_(input_index) {
  }

  [[nodiscard]] rectpack2D::rect_xywh& get_rect() noexcept {
    return rect_;
  }

  [[nodiscard]] const rectpack2D::rect_xywh& get_rect() const noexcept {
    return rect_;
  }

  [[nodiscard]] usize input_index() const noexcept {
    return input_index_;
  }

private:
  rectpack2D::rect_xywh rect_{};
  usize input_index_{};
};

[[nodiscard]] bool is_power_of_two(u32 value) noexcept {
  return value != 0 && (value & (value - 1U)) == 0;
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
  if (options.max_page_size == 0 || options.padding > options.max_page_size / 4U) {
    return TextureAtlasResult::failure({{}, {}, "build texture atlas", "invalid atlas sizing options"});
  }

  u32 base_width = 0;
  u32 base_height = 0;
  for (const TextureAtlasInput& input : inputs) {
    if (input.image.empty() || input.image.channels != 4) {
      return TextureAtlasResult::failure({
          input.asset_id.value,
          {},
          "build texture atlas",
          "input texture must be a non-empty RGBA image"});
    }
    if (!is_power_of_two(input.image.width) || !is_power_of_two(input.image.height)) {
      return TextureAtlasResult::failure({
          input.asset_id.value,
          {},
          "build texture atlas",
          "block texture dimensions must be power-of-two"});
    }
    if (!input.fallback) {
      if (base_width == 0 && base_height == 0) {
        base_width = input.image.width;
        base_height = input.image.height;
      } else if (input.image.width != base_width || input.image.height != base_height) {
        std::ostringstream message;
        message << "texture size " << input.image.width << "x" << input.image.height
                << " does not match required block texture size "
                << base_width << "x" << base_height;
        return TextureAtlasResult::failure({
            input.asset_id.value,
            {},
            "build texture atlas",
            message.str()});
      }
    }
    if (input.image.width + options.padding * 2U > options.max_page_size ||
        input.image.height + options.padding * 2U > options.max_page_size) {
      return TextureAtlasResult::failure({
          input.asset_id.value,
          {},
          "build texture atlas",
          "texture plus padding exceeds atlas page size"});
    }
  }

  constexpr bool AllowFlip = false;
  using Spaces = rectpack2D::empty_spaces<AllowFlip, rectpack2D::default_empty_spaces>;
  using Rect = rectpack2D::output_rect_t<Spaces>;

  std::vector<AtlasSubject> subjects;
  subjects.reserve(inputs.size());
  for (usize i = 0; i < inputs.size(); ++i) {
    const TextureAtlasInput& input = inputs[i];
    subjects.emplace_back(
        Rect{
            0,
            0,
            static_cast<int>(input.image.width + options.padding * 2U),
            static_cast<int>(input.image.height + options.padding * 2U)},
        i);
  }

  auto report_successful = [](Rect&) {
    return rectpack2D::callback_result::CONTINUE_PACKING;
  };
  auto report_unsuccessful = [](Rect&) {
    return rectpack2D::callback_result::ABORT_PACKING;
  };

  const rectpack2D::rect_wh result_size = rectpack2D::find_best_packing_dont_sort<Spaces>(
      subjects,
      rectpack2D::make_finder_input(
          static_cast<int>(options.max_page_size),
          -4,
          report_successful,
          report_unsuccessful,
          rectpack2D::flipping_option::DISABLED));

  if (result_size.w <= 0 || result_size.h <= 0 ||
      result_size.w > static_cast<int>(options.max_page_size) ||
      result_size.h > static_cast<int>(options.max_page_size)) {
    return TextureAtlasResult::failure({{}, {}, "build texture atlas", "rectpack2D could not pack atlas inputs"});
  }

  TextureAtlas atlas;
  atlas.padding = options.padding;
  TextureAtlasPage page;
  page.image.width = static_cast<u32>(result_size.w);
  page.image.height = static_cast<u32>(result_size.h);
  page.image.channels = 4;
  page.image.pixels.assign(static_cast<usize>(page.image.width) * page.image.height * 4U, 0U);

  atlas.entries.resize(inputs.size());
  for (AtlasSubject& subject : subjects) {
    const usize input_index = subject.input_index();
    const TextureAtlasInput& input = inputs[input_index];
    const Rect& rect = subject.get_rect();
    copy_with_edge_padding(
        input.image,
        page.image,
        static_cast<u32>(rect.x),
        static_cast<u32>(rect.y),
        options.padding);

    TextureAtlasEntry entry;
    entry.asset_id = input.asset_id;
    entry.page = 0;
    entry.texture_index = static_cast<u32>(input_index);
    entry.padded_pixel_min = {rect.x, rect.y};
    entry.padded_pixel_max = {rect.x + rect.w, rect.y + rect.h};
    entry.unpadded_pixel_min = {
        rect.x + static_cast<i32>(options.padding),
        rect.y + static_cast<i32>(options.padding)};
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

  atlas.pages.push_back(std::move(page));
  return TextureAtlasResult::success(std::move(atlas));
}

BlockTextureAtlasResult build_block_texture_atlas(
    ResourceManager& resources,
    const voxel::BlockRegistry& registry,
    TextureAtlasBuildOptions options) {
  std::vector<AssetID> texture_ids;
  std::unordered_set<std::string> seen;
  for (u32 variant_value = 0; variant_value < registry.variant_count(); ++variant_value) {
    const auto* variant = registry.variant(voxel::BlockVariantID{variant_value});
    if (variant == nullptr || variant->render_category == voxel::RenderCategory::None) {
      continue;
    }

    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      std::string texture = block_texture_for_face(*variant, face);
      if (texture.empty()) {
        texture = "arvane:texture/missing";
      }
      if (seen.insert(texture).second) {
        texture_ids.push_back({std::move(texture)});
      }
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
      return BlockTextureAtlasResult::failure({
          texture_id.value,
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

    for (voxel::BlockFace face : voxel::AllBlockFaces) {
      std::string texture = block_texture_for_face(*variant, face);
      if (texture.empty()) {
        texture = "arvane:texture/missing";
      }
      const TextureAtlasEntry* entry = result.atlas.find(texture);
      if (entry == nullptr) {
        return BlockTextureAtlasResult::failure({
            texture,
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
