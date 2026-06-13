#include "asset_helpers.hpp"

#include "arv/client/render_asset_upload.hpp"
#include "arv/render/item_model.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arv::desktop {
namespace {

constexpr u32 ItemEntityModelIDBase = 1'000'000U;
constexpr u32 BreakOverlayEntityModelIDBase = 2'000'000U;
constexpr u32 BlockOutlineEntityModelIDBase = 3'000'000U;
constexpr u32 PlayerSkinEntityModelID = 4'000'000U;
constexpr u32 PlayerSlimEntityModelID = 4'000'001U;
constexpr u32 PlayerSlimSkinEntityModelID = 4'000'002U;
constexpr u32 FirstPersonPlayerEntityModelID = 5'000'000U;
constexpr u32 FirstPersonPlayerSkinEntityModelID = 5'000'001U;
constexpr u32 FirstPersonPlayerSlimEntityModelID = 5'000'002U;
constexpr u32 FirstPersonPlayerSlimSkinEntityModelID = 5'000'003U;
constexpr u32 ChunkBoundsEntityModelID = 6'000'000U;
constexpr std::string_view WhiteTextureAsset = "arvane:texture/generated_white";
constexpr std::string_view DefaultPlayerSkinTextureAsset =
    "arvane:texture/default_skin/default";
constexpr std::string_view DefaultPlayerSlimSkinTextureAsset =
    "arvane:texture/default_skin/default_slim";
constexpr std::string_view PlayerEntityModelAsset = "arvane:model/entity/player";
constexpr std::string_view PlayerSlimEntityModelAsset = "arvane:model/entity/player_slim";
constexpr std::string_view DynamicPlayerSkinTextureAsset =
    "arvane:texture/player_skin/dynamic";

[[nodiscard]] render::EntityModelID item_entity_model_id(player::ItemID item_id) noexcept {
  return render::EntityModelID{ItemEntityModelIDBase + item_id.value};
}

[[nodiscard]] render::EntityModelID player_skin_model_id() noexcept {
  return render::EntityModelID{PlayerSkinEntityModelID};
}

[[nodiscard]] render::EntityModelID player_slim_model_id() noexcept {
  return render::EntityModelID{PlayerSlimEntityModelID};
}

[[nodiscard]] render::EntityModelID player_slim_skin_model_id() noexcept {
  return render::EntityModelID{PlayerSlimSkinEntityModelID};
}

[[nodiscard]] render::EntityModelID first_person_player_model_id() noexcept {
  return render::EntityModelID{FirstPersonPlayerEntityModelID};
}

[[nodiscard]] render::EntityModelID first_person_player_skin_model_id() noexcept {
  return render::EntityModelID{FirstPersonPlayerSkinEntityModelID};
}

[[nodiscard]] render::EntityModelID first_person_player_slim_model_id() noexcept {
  return render::EntityModelID{FirstPersonPlayerSlimEntityModelID};
}

[[nodiscard]] render::EntityModelID first_person_player_slim_skin_model_id() noexcept {
  return render::EntityModelID{FirstPersonPlayerSlimSkinEntityModelID};
}

[[nodiscard]] render::EntityModelID chunk_bounds_model_id() noexcept {
  return render::EntityModelID{ChunkBoundsEntityModelID};
}

[[nodiscard]] render::EntityModelID break_overlay_model_id(
    voxel::BlockVariantID variant,
    usize stage) noexcept {
  return render::EntityModelID{
      BreakOverlayEntityModelIDBase +
      variant.value * static_cast<u32>(BreakStageTextureCount) +
      static_cast<u32>(stage)};
}

[[nodiscard]] std::string break_stage_texture_asset(usize stage) {
  return "arvane:texture/break_stage/stage" + std::to_string(stage);
}

[[nodiscard]] render::EntityModelID block_outline_model_id(
    voxel::BlockVariantID variant) noexcept {
  return render::EntityModelID{BlockOutlineEntityModelIDBase + variant.value};
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

[[nodiscard]] std::string asset_leaf_name(std::string_view asset_id) {
  const usize separator = asset_id.find(':');
  std::string_view path = separator == std::string_view::npos
                              ? asset_id
                              : asset_id.substr(separator + 1U);
  const usize slash = path.rfind('/');
  std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1U);
  if (leaf.ends_with(".json")) {
    leaf.remove_suffix(std::string_view{".json"}.size());
  }
  return std::string{leaf};
}

[[nodiscard]] std::string resolve_entity_input_texture(
    std::string_view texture,
    std::string_view default_player_skin_texture = DefaultPlayerSkinTextureAsset) {
  if (texture == "$0") {
    return std::string{default_player_skin_texture};
  }
  return std::string{texture};
}

[[nodiscard]] render::EntityModelDefinition make_first_person_player_definition(
    render::EntityModelDefinition definition) {
  definition.name += "#first_person";
  definition.boxes.erase(
      std::remove_if(
          definition.boxes.begin(),
          definition.boxes.end(),
          [](const render::EntityModelBoxDefinition& box) {
            return box.bone == "head";
          }),
      definition.boxes.end());
  return definition;
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
  return client::texture_upload_from_image(image);
}

render::TextureArrayUploadData texture_array_upload_from_image(const asset::Image& image) {
  return client::texture_array_upload_from_image(image);
}

render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas) {
  return client::texture_array_upload_from_atlas(atlas);
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
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions options) {
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

  return asset::build_texture_atlas(inputs, options);
}

EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models,
    BlockOutlineRenderConfig outline_config,
    asset::TextureAtlasBuildOptions atlas_options) {
  struct PendingModel {
    EntityTypeID type_id{};
    std::optional<player::ItemID> item_id{};
    render::EntityModelID model_id{};
    render::EntityModelDefinition definition{};
    bool first_person_player{};
    bool player_skin_atlas{};
    bool slim_player{};
    bool primary_type_model{};
  };
  struct PendingAnimation {
    EntityTypeID type_id{};
    render::EntityAnimationID animation_id{};
    std::string asset_id{};
    render::EntityAnimationDefinition definition{};
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
  std::vector<PendingAnimation> pending_animations;
  HashMap<EntityTypeID, usize> pending_animation_by_type;
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

    render::EntityModelDefinition definition = std::move(model_definition.value());
    if (definition.name.empty()) {
      definition.name = asset_leaf_name(type.model_asset);
    }
    if (!type.animation_asset.empty()) {
      auto animation_text =
          resources.load_text({type.animation_asset}, asset::AssetKind::Animation);
      if (!animation_text) {
        return EntityRenderAssetsResult::failure(animation_text.error());
      }
      auto animation_definition = render::parse_entity_animation_json(
          animation_text.value(),
          resources
              .resolve_asset_path({type.animation_asset}, asset::AssetKind::Animation)
              .string());
      if (!animation_definition) {
        return EntityRenderAssetsResult::failure({type.animation_asset,
                                                  resources.resolve_asset_path({type.animation_asset}, asset::AssetKind::Animation),
                                                  "parse entity animation",
                                                  animation_definition.error().message});
      }
      const usize animation_index = pending_animations.size();
      pending_animation_by_type[type.id] = animation_index;
      pending_animations.push_back({type.id,
                                    render::EntityAnimationID{type.id.value},
                                    type.animation_asset,
                                    std::move(animation_definition.value())});
    }

    std::vector<usize> player_skin_texture_slots;
    for (usize texture_index = 0; texture_index < definition.texture_assets.size(); ++texture_index) {
      std::string& texture = definition.texture_assets[texture_index];
      if (texture == "$0") {
        player_skin_texture_slots.push_back(texture_index);
      }
      texture = resolve_entity_input_texture(texture);
      add_texture(texture);
    }

    pending_models.push_back({type.id,
                              std::nullopt,
                              render::EntityModelID{type.id.value},
                              definition,
                              false,
                              false,
                              false,
                              true});

    if (type.model_asset != PlayerEntityModelAsset || player_skin_texture_slots.empty()) {
      continue;
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              first_person_player_model_id(),
                              make_first_person_player_definition(definition),
                              true,
                              false,
                              false,
                              false});

    render::EntityModelDefinition dynamic_skin_definition = definition;
    dynamic_skin_definition.name += "#skin_dynamic";
    for (const usize texture_index : player_skin_texture_slots) {
      dynamic_skin_definition.texture_assets[texture_index] =
          std::string{DynamicPlayerSkinTextureAsset};
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              player_skin_model_id(),
                              std::move(dynamic_skin_definition),
                              false,
                              true,
                              false,
                              false});

    render::EntityModelDefinition first_person_dynamic_skin =
        make_first_person_player_definition(definition);
    first_person_dynamic_skin.name += "#skin_dynamic";
    for (const usize texture_index : player_skin_texture_slots) {
      first_person_dynamic_skin.texture_assets[texture_index] =
          std::string{DynamicPlayerSkinTextureAsset};
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              first_person_player_skin_model_id(),
                              std::move(first_person_dynamic_skin),
                              true,
                              true,
                              false,
                              false});

    const asset::AssetID slim_model_asset{std::string{PlayerSlimEntityModelAsset}};
    auto slim_model_text =
        resources.load_text(slim_model_asset, asset::AssetKind::Model);
    if (!slim_model_text) {
      return EntityRenderAssetsResult::failure(slim_model_text.error());
    }
    auto slim_model_definition =
        render::parse_entity_model_json(slim_model_text.value());
    if (!slim_model_definition) {
      return EntityRenderAssetsResult::failure({slim_model_asset.value,
                                                resources.resolve_asset_path(slim_model_asset, asset::AssetKind::Model),
                                                "parse slim player model",
                                                slim_model_definition.error().message});
    }
    render::EntityModelDefinition slim_definition =
        std::move(slim_model_definition.value());
    if (slim_definition.name.empty()) {
      slim_definition.name = asset_leaf_name(PlayerSlimEntityModelAsset);
    }
    std::vector<usize> slim_player_skin_texture_slots;
    for (usize texture_index = 0; texture_index < slim_definition.texture_assets.size(); ++texture_index) {
      std::string& texture = slim_definition.texture_assets[texture_index];
      if (texture == "$0") {
        slim_player_skin_texture_slots.push_back(texture_index);
      }
      texture = resolve_entity_input_texture(texture, DefaultPlayerSlimSkinTextureAsset);
      add_texture(texture);
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              player_slim_model_id(),
                              slim_definition,
                              false,
                              false,
                              true,
                              false});

    pending_models.push_back({type.id,
                              std::nullopt,
                              first_person_player_slim_model_id(),
                              make_first_person_player_definition(slim_definition),
                              true,
                              false,
                              true,
                              false});

    render::EntityModelDefinition slim_dynamic_skin_definition = slim_definition;
    slim_dynamic_skin_definition.name += "#skin_dynamic";
    for (const usize texture_index : slim_player_skin_texture_slots) {
      slim_dynamic_skin_definition.texture_assets[texture_index] =
          std::string{DynamicPlayerSkinTextureAsset};
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              player_slim_skin_model_id(),
                              std::move(slim_dynamic_skin_definition),
                              false,
                              true,
                              true,
                              false});

    render::EntityModelDefinition first_person_slim_dynamic_skin =
        make_first_person_player_definition(slim_definition);
    first_person_slim_dynamic_skin.name += "#skin_dynamic";
    for (const usize texture_index : slim_player_skin_texture_slots) {
      first_person_slim_dynamic_skin.texture_assets[texture_index] =
          std::string{DynamicPlayerSkinTextureAsset};
    }
    pending_models.push_back({type.id,
                              std::nullopt,
                              first_person_player_slim_skin_model_id(),
                              std::move(first_person_slim_dynamic_skin),
                              true,
                              true,
                              true,
                              false});
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
        for (std::string& texture : model_definition.value().texture_assets) {
          texture = resolve_entity_input_texture(texture);
          add_texture(texture);
        }
        if (model_definition.value().name.empty()) {
          model_definition.value().name = asset_leaf_name(model_asset);
        }
        pending_models.push_back({EntityTypeID{},
                                  item_id,
                                  model_id,
                                  std::move(model_definition.value()),
                                  false,
                                  false,
                                  false,
                                  false});
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

  for (usize stage = 0; stage < BreakStageTextureCount; ++stage) {
    add_texture(break_stage_texture_asset(stage));
  }
  add_texture(std::string{WhiteTextureAsset});
  add_texture("arvane:texture/missing");

  std::vector<asset::TextureAtlasInput> inputs;
  HashMap<std::string, asset::Image> source_images;
  inputs.reserve(texture_ids.size());
  for (const asset::AssetID& texture_id : texture_ids) {
    if (texture_id.value == WhiteTextureAsset) {
      asset::Image white;
      white.width = 1;
      white.height = 1;
      white.channels = 4;
      white.pixels = {255, 255, 255, 255};
      inputs.push_back({texture_id, white, false});
      source_images[texture_id.value] = std::move(white);
    } else {
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
  }

  auto atlas_result = asset::build_texture_atlas(inputs, atlas_options);
  if (!atlas_result) {
    return EntityRenderAssetsResult::failure(atlas_result.error());
  }
  EntityRenderAssets assets;
  assets.texture_atlas = std::move(atlas_result.value());

  for (const PendingModel& pending : pending_models) {
    std::vector<render::EntityModelTextureBinding> bindings;
    bindings.reserve(pending.definition.texture_assets.size());
    for (const std::string& texture : pending.definition.texture_assets) {
      if (pending.player_skin_atlas && texture == DynamicPlayerSkinTextureAsset) {
        bindings.push_back({texture,
                            {},
                            {1, 1},
                            1,
                            1,
                            render::TextureAtlasDomain::PlayerSkins,
                            0});
        continue;
      }
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
                          entry->height,
                          render::TextureAtlasDomain::Entities,
                          static_cast<u16>(entry->page)});
    }

    auto model_asset =
        render::build_entity_model_asset(pending.model_id, pending.definition, bindings);
    if (!model_asset) {
      return EntityRenderAssetsResult::failure({pending.definition.name,
                                                {},
                                                "build entity model",
                                                model_asset.error().message});
    }
    render::EntityModelAsset built_model = std::move(model_asset.value());
    if (pending.primary_type_model) {
      const auto pending_animation =
          pending_animation_by_type.find(pending.type_id);
      if (pending_animation != pending_animation_by_type.end()) {
        PendingAnimation& animation =
            pending_animations[pending_animation->second];
        auto animation_asset = render::build_entity_animation_asset(
            animation.animation_id,
            animation.asset_id,
            std::move(animation.definition));
        if (!animation_asset) {
          return EntityRenderAssetsResult::failure({animation.asset_id,
                                                    {},
                                                    "build entity animation",
                                                    animation_asset.error().message});
        }
        if (std::optional<render::EntityModelError> binding_error =
                render::validate_entity_animation_bindings(
                    built_model,
                    animation_asset.value());
            binding_error.has_value()) {
          return EntityRenderAssetsResult::failure({animation.asset_id,
                                                    {},
                                                    "validate entity animation",
                                                    binding_error->message});
        }
        assets.animation_by_type[pending.type_id] = animation.animation_id;
        assets.animations.push_back(std::move(animation_asset.value()));
      }
    }
    if (pending.item_id.has_value()) {
      assets.model_by_item[pending.item_id.value()] = pending.model_id;
    } else if (pending.first_person_player && pending.player_skin_atlas &&
               pending.slim_player) {
      assets.first_person_player_slim_skin_model = pending.model_id;
    } else if (pending.first_person_player && pending.player_skin_atlas) {
      assets.first_person_player_skin_model = pending.model_id;
    } else if (pending.first_person_player && pending.slim_player) {
      assets.first_person_player_slim_model = pending.model_id;
    } else if (pending.first_person_player) {
      assets.first_person_player_model = pending.model_id;
    } else if (pending.player_skin_atlas && pending.slim_player) {
      assets.player_slim_skin_model = pending.model_id;
    } else if (pending.player_skin_atlas) {
      assets.player_skin_model = pending.model_id;
    } else if (pending.slim_player) {
      assets.player_slim_model = pending.model_id;
    } else {
      assets.model_by_type[pending.type_id] = pending.model_id;
    }
    assets.models.push_back(std::move(built_model));
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
    input.atlas_page = static_cast<u16>(entry->page);
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
    input.translucent = variant->render_category == voxel::RenderCategory::Transparent;
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
      input.face_pages[face_index] = static_cast<u16>(entry->page);
      input.face_uvs[face_index] = atlas_face_uvs(*entry, voxel::default_cube_face_uvs(face));
    }
    assets.model_by_item[pending.item_id] = pending.model_id;
    assets.models.push_back(render::build_block_item_model(input));
  }

  assets.break_overlay_model_by_variant.resize(block_registry.variant_count());
  assets.outline_model_by_variant.resize(block_registry.variant_count());
  const asset::TextureAtlasEntry* white_entry =
      assets.texture_atlas.find(std::string{WhiteTextureAsset});
  if (white_entry == nullptr) {
    return EntityRenderAssetsResult::failure({std::string{WhiteTextureAsset},
                                              {},
                                              "build outline models",
                                              "white texture was not present in final atlas"});
  }

  render::BlockOutlineModelInput chunk_bounds_input;
  chunk_bounds_input.id = chunk_bounds_model_id();
  chunk_bounds_input.name = "debug#chunk_bounds";
  chunk_bounds_input.uvs =
      atlas_face_uvs(*white_entry, voxel::default_cube_face_uvs(voxel::BlockFace::PositiveY));
  chunk_bounds_input.atlas_page = static_cast<u16>(white_entry->page);
  chunk_bounds_input.color = {0.08F, 0.75F, 1.0F};
  chunk_bounds_input.thickness_sixteenths = 0.04F;
  chunk_bounds_input.inflate_sixteenths = 0.01F;
  assets.chunk_bounds_model = chunk_bounds_input.id;
  assets.models.push_back(render::build_block_outline_model(chunk_bounds_input));

  for (u32 variant_value = 0; variant_value < block_registry.variant_count(); ++variant_value) {
    const voxel::BlockVariantID variant_id{variant_value};
    const voxel::BlockVariantRecord* variant = block_registry.variant(variant_id);
    if (variant == nullptr || variant->render_category == voxel::RenderCategory::None) {
      continue;
    }

    std::vector<voxel::BlockModelBoxRenderData> model_boxes;
    if (!variant->model.asset.empty()) {
      const voxel::BlockModelDefinition* model_definition = block_model_by_variant[variant_value];
      if (model_definition == nullptr) {
        return EntityRenderAssetsResult::failure({variant->model.asset,
                                                  resources.resolve_asset_path({variant->model.asset}, asset::AssetKind::Model),
                                                  "build break overlay model",
                                                  "block model was not loaded"});
      }
      const std::span<const voxel::BlockModelBoxRenderData> cached_boxes =
          block_render_data.model_boxes(variant_id);
      model_boxes.assign(cached_boxes.begin(), cached_boxes.end());
    }

    for (usize stage = 0; stage < BreakStageTextureCount; ++stage) {
      const std::string texture = break_stage_texture_asset(stage);
      const asset::TextureAtlasEntry* entry = assets.texture_atlas.find(texture);
      if (entry == nullptr) {
        return EntityRenderAssetsResult::failure({texture,
                                                  {},
                                                  "build break overlay model",
                                                  "break stage texture was not present in final atlas"});
      }

      render::BlockBreakOverlayModelInput input;
      input.id = break_overlay_model_id(variant_id, stage);
      input.name = variant->debug_name + "#break_stage_" + std::to_string(stage);
      input.model_boxes =
          std::span<const voxel::BlockModelBoxRenderData>{model_boxes.data(), model_boxes.size()};
      for (const voxel::BlockFace face : voxel::AllBlockFaces) {
        const usize face_index = voxel::face_index(face);
        input.face_valid[face_index] = variant->renders_face[face_index];
        input.face_pages[face_index] = static_cast<u16>(entry->page);
        input.face_uvs[face_index] = atlas_face_uvs(*entry, voxel::default_cube_face_uvs(face));
      }
      assets.break_overlay_model_by_variant[variant_value][stage] = input.id;
      assets.models.push_back(render::build_block_break_overlay_model(input));
    }

    render::BlockOutlineModelInput outline_input;
    outline_input.id = block_outline_model_id(variant_id);
    outline_input.name = variant->debug_name + "#outline";
    outline_input.uvs = atlas_face_uvs(*white_entry, voxel::default_cube_face_uvs(voxel::BlockFace::PositiveY));
    outline_input.atlas_page = static_cast<u16>(white_entry->page);
    outline_input.color = outline_config.color;
    outline_input.thickness_sixteenths = outline_config.thickness;
    outline_input.model_boxes =
        std::span<const voxel::BlockModelBoxRenderData>{model_boxes.data(), model_boxes.size()};
    assets.outline_model_by_variant[variant_value] = outline_input.id;
    assets.models.push_back(render::build_block_outline_model(outline_input));
  }

  return EntityRenderAssetsResult::success(std::move(assets));
}

} // namespace arv::desktop
