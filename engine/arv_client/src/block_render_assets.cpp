#include "arv/client/block_render_assets.hpp"

#include "arv/voxel/fluid.hpp"

#include <optional>
#include <string>
#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] ClientBlockRenderAssetsResult asset_failure(
    std::string category,
    std::string message) {
  return ClientBlockRenderAssetsResult::failure(
      {std::move(category), std::move(message)});
}

void register_default_fluid_render_data(
    render::FluidRenderData& fluid_render_data,
    const voxel::BlockRegistry& block_registry) {
  const std::optional<voxel::BlockVariantID> water_variant =
      block_registry.find_variant("arvane:water");
  if (water_variant.has_value()) {
    fluid_render_data.set_fluid_variant(
        voxel::DefaultWaterFluidTypeID,
        water_variant.value());
  }
}

} // namespace

ClientBlockRenderAssetsResult load_client_block_render_assets(
    asset::PackManager& packs,
    asset::ResourceManager& resources,
    ClientBlockRenderAssetLoadOptions options) {
  ClientBlockRenderAssets loaded;

  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    return asset_failure("blocks", block_definition_text.error().describe());
  }
  auto block_definitions =
      voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    return asset_failure("blocks", block_definitions.error().message);
  }

  voxel::BlockRegistryBuildOptions block_options;
  block_options.behavior_exists = options.block_behavior_exists;
  auto block_registry_result =
      voxel::BlockRegistry::build(std::move(block_definitions.value()), block_options);
  if (!block_registry_result) {
    return asset_failure("blocks", block_registry_result.error().message);
  }
  loaded.block_registry = std::move(block_registry_result.value());

  auto container_type_text =
      packs.load_merged_json_array_text("container_types", "container_types");
  if (!container_type_text) {
    return asset_failure("container_types", container_type_text.error().describe());
  }
  auto container_type_definitions =
      player::parse_container_type_definitions_json(container_type_text.value());
  if (!container_type_definitions) {
    return asset_failure("container_types", container_type_definitions.error().message);
  }
  auto container_type_registry =
      player::ContainerTypeRegistry::build(std::move(container_type_definitions.value()));
  if (!container_type_registry) {
    return asset_failure("container_types", container_type_registry.error().message);
  }
  loaded.container_types = std::move(container_type_registry.value());

  auto container_layout_text =
      packs.load_merged_json_array_text("container_layouts", "container_layouts");
  if (!container_layout_text) {
    return asset_failure("container_layouts", container_layout_text.error().describe());
  }
  auto container_layout_definitions =
      player::parse_container_ui_layout_definitions_json(container_layout_text.value());
  if (!container_layout_definitions) {
    return asset_failure("container_layouts", container_layout_definitions.error().message);
  }
  auto container_layout_registry =
      player::ContainerUiLayoutRegistry::build(std::move(container_layout_definitions.value()));
  if (!container_layout_registry) {
    return asset_failure("container_layouts", container_layout_registry.error().message);
  }
  loaded.container_layouts = std::move(container_layout_registry.value());

  auto block_models_result =
      asset::load_block_model_definitions(resources, loaded.block_registry);
  if (!block_models_result) {
    return asset_failure("assets", block_models_result.error().message);
  }
  loaded.block_models = std::move(block_models_result.value());
  asset::apply_block_model_collision_boxes(
      loaded.block_registry,
      loaded.block_models);

  auto block_atlas_result = asset::build_block_texture_atlas(
      resources,
      loaded.block_registry,
      loaded.block_models,
      options.atlas_options);
  if (!block_atlas_result) {
    return asset_failure("assets", block_atlas_result.error().message);
  }
  loaded.block_atlas = std::move(block_atlas_result.value());
  if (loaded.block_atlas.atlas.pages.empty()) {
    return asset_failure("assets", "block texture atlas did not produce a page");
  }

  register_default_fluid_render_data(
      loaded.fluid_render_data,
      loaded.block_registry);
  return ClientBlockRenderAssetsResult::success(std::move(loaded));
}

} // namespace arv::client
