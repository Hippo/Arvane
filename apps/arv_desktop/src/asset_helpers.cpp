#include "asset_helpers.hpp"

#include "arv/render/item_model.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arv::desktop {
namespace {

constexpr u32 ItemEntityModelIDBase = 1'000'000U;

[[nodiscard]] render::EntityModelID item_entity_model_id(player::ItemID item_id) noexcept {
  return render::EntityModelID{ItemEntityModelIDBase + item_id.value};
}

[[nodiscard]] std::string normalize_item_model_asset(std::string_view model) {
  if (model.empty()) {
    return {};
  }
  if (model.find(':') != std::string_view::npos) {
    return std::string{model};
  }
  return "arvane:model/item/" + std::string{model};
}

[[nodiscard]] std::string block_texture_for_face(
    const voxel::BlockVariantRecord& variant,
    voxel::BlockFace face) {
  std::string texture = variant.texture_assets[voxel::face_index(face)];
  return texture.empty() ? "arvane:texture/missing" : texture;
}

[[nodiscard]] std::array<Vec2i, 4> atlas_face_uvs(
    const asset::TextureAtlasEntry& entry,
    const std::array<Vec2i, 4>& logical_uvs) {
  std::array<Vec2i, 4> uvs{};
  for (usize index = 0; index < uvs.size(); ++index) {
    const f32 u = static_cast<f32>(logical_uvs[index].x) / 16.0F;
    const f32 v = static_cast<f32>(logical_uvs[index].y) / 16.0F;
    uvs[index] = {
        entry.unpadded_pixel_min.x + static_cast<i32>(u * static_cast<f32>(entry.width)),
        entry.unpadded_pixel_min.y + static_cast<i32>(v * static_cast<f32>(entry.height))};
  }
  return uvs;
}

} // namespace

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
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    std::span<const asset::BlockModelAssetData> block_models) {
  struct PendingModel {
    EntityTypeID type_id{};
    std::optional<player::ItemID> item_id{};
    render::EntityModelID model_id{};
    render::EntityModelDefinition definition{};
  };
  struct PendingTextureItem {
    player::ItemID item_id{};
    render::EntityModelID model_id{};
    std::string texture_asset{};
  };
  struct PendingBlockItem {
    player::ItemID item_id{};
    render::EntityModelID model_id{};
    voxel::BlockVariantID variant{};
    const voxel::BlockModelDefinition* model_definition{};
  };

  std::vector<asset::BlockModelAssetData> loaded_block_models;
  std::span<const asset::BlockModelAssetData> resolved_block_models = block_models;
  if (resolved_block_models.empty()) {
    auto loaded_models = asset::load_block_model_definitions(resources, block_registry);
    if (!loaded_models) {
      return EntityRenderAssetsResult::failure(loaded_models.error());
    }
    loaded_block_models = std::move(loaded_models.value());
    resolved_block_models =
        std::span<const asset::BlockModelAssetData>{loaded_block_models.data(), loaded_block_models.size()};
  }

  std::vector<const voxel::BlockModelDefinition*> block_model_by_variant(
      block_registry.variant_count(),
      nullptr);
  for (const asset::BlockModelAssetData& block_model : resolved_block_models) {
    if (block_model.variant_id.value < block_model_by_variant.size()) {
      block_model_by_variant[block_model.variant_id.value] = &block_model.definition;
    }
  }

  std::vector<PendingModel> pending_models;
  std::vector<PendingTextureItem> pending_texture_items;
  std::vector<PendingBlockItem> pending_block_items;
  std::vector<asset::AssetID> texture_ids;
  HashSet<std::string> seen_textures;
  const auto add_texture = [&](std::string texture) {
    if (texture.empty()) {
      texture = "arvane:texture/missing";
    }
    if (seen_textures.insert(texture).second) {
      texture_ids.push_back({std::move(texture)});
    }
  };

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
      add_texture(texture);
    }

    pending_models.push_back({type.id,
                              std::nullopt,
                              render::EntityModelID{type.id.value},
                              std::move(model_definition.value())});
  }

  for (u32 item_value = 0; item_value < item_registry.item_count(); ++item_value) {
    const player::ItemID item_id{item_value};
    if (item_registry.is_air(item_id)) {
      continue;
    }
    const player::ItemRecord* item = item_registry.item(item_id);
    if (item == nullptr) {
      continue;
    }

    const render::EntityModelID model_id = item_entity_model_id(item_id);
    switch (item->render_type) {
      case player::ItemRenderType::Texture: {
        std::string texture = item->texture_asset.empty()
                                  ? std::string{"arvane:texture/missing"}
                                  : item->texture_asset;
        add_texture(texture);
        pending_texture_items.push_back({item_id, model_id, std::move(texture)});
        break;
      }
      case player::ItemRenderType::Model: {
        const std::string model_asset = normalize_item_model_asset(item->model_asset);
        auto model_text = resources.load_text({model_asset}, asset::AssetKind::Model);
        if (!model_text) {
          return EntityRenderAssetsResult::failure(model_text.error());
        }
        auto model_definition = render::parse_entity_model_json(model_text.value());
        if (!model_definition) {
          return EntityRenderAssetsResult::failure({model_asset,
                                                    resources.resolve_asset_path({model_asset}, asset::AssetKind::Model),
                                                    "parse item model",
                                                    model_definition.error().message});
        }
        for (const std::string& texture : model_definition.value().texture_assets) {
          add_texture(texture);
        }
        pending_models.push_back({EntityTypeID{},
                                  item_id,
                                  model_id,
                                  std::move(model_definition.value())});
        break;
      }
      case player::ItemRenderType::Block: {
        if (!item->render_block_variant.has_value()) {
          add_texture("arvane:texture/missing");
          pending_texture_items.push_back({item_id, model_id, "arvane:texture/missing"});
          break;
        }
        const voxel::BlockVariantRecord* variant =
            block_registry.variant(item->render_block_variant.value());
        if (variant == nullptr) {
          add_texture("arvane:texture/missing");
          pending_texture_items.push_back({item_id, model_id, "arvane:texture/missing"});
          break;
        }
        const voxel::BlockModelDefinition* model_definition =
            variant->model.asset.empty()
                ? nullptr
                : block_model_by_variant[item->render_block_variant.value().value];
        if (!variant->model.asset.empty() && model_definition == nullptr) {
          return EntityRenderAssetsResult::failure({variant->model.asset,
                                                    resources.resolve_asset_path({variant->model.asset}, asset::AssetKind::Model),
                                                    "build block item model",
                                                    "block model was not loaded"});
        }
        if (model_definition != nullptr) {
          for (const std::string& texture : model_definition->texture_assets) {
            add_texture(texture);
          }
        } else {
          for (const voxel::BlockFace face : voxel::AllBlockFaces) {
            add_texture(block_texture_for_face(*variant, face));
          }
        }
        pending_block_items.push_back({item_id, model_id, item->render_block_variant.value(), model_definition});
        break;
      }
    }
  }

  add_texture("arvane:texture/missing");

  std::vector<asset::TextureAtlasInput> inputs;
  HashMap<std::string, asset::Image> source_images;
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
    source_images[texture_id.value] = resource->image;
  }

  auto atlas_result = asset::build_texture_atlas(
      inputs,
      {.max_page_size = 1024, .padding = 2, .require_uniform_non_fallback_size = false});
  if (!atlas_result) {
    return EntityRenderAssetsResult::failure(atlas_result.error());
  }
  EntityRenderAssets assets;
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
    if (pending.item_id.has_value()) {
      assets.model_by_item[pending.item_id.value()] = pending.model_id;
    } else {
      assets.model_by_type[pending.type_id] = pending.model_id;
    }
    assets.models.push_back(std::move(model_asset.value()));
  }

  for (const PendingTextureItem& pending : pending_texture_items) {
    const asset::TextureAtlasEntry* entry = assets.texture_atlas.find(pending.texture_asset);
    const auto image = source_images.find(pending.texture_asset);
    if (entry == nullptr || image == source_images.end()) {
      return EntityRenderAssetsResult::failure({pending.texture_asset,
                                                {},
                                                "build item model",
                                                "item texture was not present in final atlas"});
    }
    render::ItemTextureMeshInput input;
    input.id = pending.model_id;
    input.name = item_registry.item(pending.item_id) == nullptr
                     ? std::string{player::MissingItemName}
                     : item_registry.item(pending.item_id)->name;
    input.width = image->second.width;
    input.height = image->second.height;
    input.rgba_pixels = std::span<const u8>{image->second.pixels.data(), image->second.pixels.size()};
    input.atlas_pixel_min = entry->unpadded_pixel_min;
    assets.model_by_item[pending.item_id] = pending.model_id;
    assets.models.push_back(render::build_extruded_texture_item_model(input));
  }

  for (const PendingBlockItem& pending : pending_block_items) {
    const voxel::BlockVariantRecord* variant = block_registry.variant(pending.variant);
    if (variant == nullptr) {
      continue;
    }

    render::ItemBlockModelInput input;
    input.id = pending.model_id;
    input.name = item_registry.item(pending.item_id) == nullptr
                     ? std::string{player::MissingItemName}
                     : item_registry.item(pending.item_id)->name;
    if (pending.model_definition != nullptr) {
      auto render_boxes = asset::build_block_model_render_boxes(
          assets.texture_atlas,
          *pending.model_definition,
          "build block item model");
      if (!render_boxes) {
        return EntityRenderAssetsResult::failure(render_boxes.error());
      }
      std::vector<voxel::BlockModelBoxRenderData> model_boxes =
          std::move(render_boxes.value());
      input.model_boxes =
          std::span<const voxel::BlockModelBoxRenderData>{model_boxes.data(), model_boxes.size()};
      assets.model_by_item[pending.item_id] = pending.model_id;
      assets.models.push_back(render::build_block_item_model(input));
      continue;
    }
    for (const voxel::BlockFace face : voxel::AllBlockFaces) {
      const std::string texture = block_texture_for_face(*variant, face);
      const asset::TextureAtlasEntry* entry = assets.texture_atlas.find(texture);
      if (entry == nullptr) {
        return EntityRenderAssetsResult::failure({texture,
                                                  {},
                                                  "build block item model",
                                                  "block item texture was not present in final atlas"});
      }
      const usize face_index = voxel::face_index(face);
      input.face_valid[face_index] = true;
      input.face_uvs[face_index] = atlas_face_uvs(*entry, voxel::default_cube_face_uvs(face));
    }
    assets.model_by_item[pending.item_id] = pending.model_id;
    assets.models.push_back(render::build_block_item_model(input));
  }

  return EntityRenderAssetsResult::success(std::move(assets));
}

} // namespace arv::desktop
