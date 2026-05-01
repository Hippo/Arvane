#include "asset_helpers.hpp"

#include <string>
#include <utility>
#include <vector>

namespace arv::desktop {

render::TextureUploadData texture_upload_from_image(const asset::Image& image) {
  return {image.width, image.height, image.pixels};
}

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
    const player::ItemRegistry& item_registry) {
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
      return asset::TextureAtlasResult::failure({texture_id.value,
                                                 {},
                                                 "build item texture atlas",
                                                 "loaded texture handle did not resolve"});
    }
    inputs.emplace_back(texture_id, resource->image, resource->fallback);
  }

  return asset::build_texture_atlas(inputs, {.max_page_size = 1024, .padding = 2});
}

EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry) {
  struct PendingModel {
    EntityTypeID type_id{};
    render::EntityModelID model_id{};
    render::EntityModelDefinition definition{};
  };

  std::vector<PendingModel> pending_models;
  std::vector<asset::AssetID> texture_ids;
  HashSet<std::string> seen_textures;

  for (const entity::EntityTypeRecord& type : entity_registry.records()) {
    if (type.model_asset.empty()) {
      continue;
    }

    auto model_text = resources.load_text({type.model_asset}, asset::AssetKind::Model);
    if (!model_text) {
      return EntityRenderAssetsResult::failure(model_text.error());
    }

    auto model_definition = render::parse_entity_model_json(model_text.value());
    if (!model_definition) {
      return EntityRenderAssetsResult::failure({type.model_asset,
                                                resources.resolve_asset_path({type.model_asset}, asset::AssetKind::Model),
                                                "parse entity model",
                                                model_definition.error().message});
    }

    for (const std::string& texture : model_definition.value().texture_assets) {
      if (!texture.empty() && seen_textures.insert(texture).second) {
        texture_ids.push_back({texture});
      }
    }

    pending_models.push_back({type.id,
                              render::EntityModelID{type.id.value},
                              std::move(model_definition.value())});
  }

  EntityRenderAssets assets;
  if (pending_models.empty()) {
    return EntityRenderAssetsResult::success(std::move(assets));
  }

  if (texture_ids.empty()) {
    texture_ids.push_back({"arvane:texture/missing"});
  }

  std::vector<asset::TextureAtlasInput> inputs;
  inputs.reserve(texture_ids.size());
  for (const asset::AssetID& texture_id : texture_ids) {
    auto handle = texture_id.value == "arvane:texture/missing"
                      ? asset::TextureLoadResult::success(resources.missing_texture())
                      : resources.load_texture(texture_id);
    if (!handle) {
      return EntityRenderAssetsResult::failure(handle.error());
    }
    const asset::TextureResource* resource = resources.texture(handle.value());
    if (resource == nullptr) {
      return EntityRenderAssetsResult::failure({texture_id.value,
                                                {},
                                                "build entity texture atlas",
                                                "loaded texture handle did not resolve"});
    }
    inputs.push_back({texture_id, resource->image, resource->fallback});
  }

  auto atlas_result = asset::build_texture_atlas(inputs, {.max_page_size = 1024, .padding = 2});
  if (!atlas_result) {
    return EntityRenderAssetsResult::failure(atlas_result.error());
  }
  assets.texture_atlas = std::move(atlas_result.value());

  for (const PendingModel& pending : pending_models) {
    std::vector<render::EntityModelTextureBinding> bindings;
    bindings.reserve(pending.definition.texture_assets.size());
    for (const std::string& texture : pending.definition.texture_assets) {
      const asset::TextureAtlasEntry* entry = assets.texture_atlas.find(texture);
      if (entry == nullptr) {
        return EntityRenderAssetsResult::failure({texture,
                                                  {},
                                                  "build entity model",
                                                  "model texture was not present in final atlas"});
      }
      bindings.push_back({texture,
                          entry->unpadded_pixel_min,
                          entry->unpadded_pixel_max,
                          entry->width,
                          entry->height});
    }

    auto model_asset =
        render::build_entity_model_asset(pending.model_id, pending.definition, bindings);
    if (!model_asset) {
      return EntityRenderAssetsResult::failure({pending.definition.name,
                                                {},
                                                "build entity model",
                                                model_asset.error().message});
    }
    assets.model_by_type[pending.type_id] = pending.model_id;
    assets.models.push_back(std::move(model_asset.value()));
  }

  return EntityRenderAssetsResult::success(std::move(assets));
}

} // namespace arv::desktop
