#include "dedicated_registry_loader.hpp"

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/log.hpp"
#include "arv/persistence/save.hpp"

#include <utility>

namespace arv::dedicated {

Expected<DedicatedRegistries, DedicatedError> load_dedicated_registries(
    const io::EngineConfig& config) {
  auto block_definition_text = io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "dedicated block definitions");
  if (!block_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("blocks", block_definition_text.error().message));
  }

  auto block_definitions =
      voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("blocks", block_definitions.error().message));
  }

  auto block_registry_result =
      voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("blocks", block_registry_result.error().message));
  }

  DedicatedRegistries registries;
  registries.block_registry = std::move(block_registry_result.value());
  asset::ResourceManager resources(config.paths.assets_path());
  auto block_models_result =
      asset::load_block_model_definitions(resources, registries.block_registry);
  if (!block_models_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("assets", block_models_result.error().message));
  }
  asset::apply_block_model_collision_boxes(
      registries.block_registry,
      block_models_result.value());
  registries.block_registry_hash =
      persistence::hash_block_registry(registries.block_registry);
  core::info(
      "dedicated",
      "loaded block registry: blocks={} variants={} hash={}",
      registries.block_registry.block_count(),
      registries.block_registry.variant_count(),
      registries.block_registry_hash);

  auto item_definition_text = io::read_text_file(
      config.paths.config_path() / "items.json",
      "dedicated item definitions");
  if (!item_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("items", item_definition_text.error().message));
  }

  auto item_definitions =
      player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("items", item_definitions.error().message));
  }

  auto item_registry_result =
      player::ItemRegistry::build_from_blocks_and_items(
          registries.block_registry,
          std::move(item_definitions.value()));
  if (!item_registry_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("items", item_registry_result.error().message));
  }

  registries.item_registry = std::move(item_registry_result.value());
  registries.item_registry_hash =
      persistence::hash_item_registry(registries.item_registry);
  core::info(
      "dedicated",
      "loaded item registry: items={} hash={}",
      registries.item_registry.item_count(),
      registries.item_registry_hash);

  auto entity_definition_text = io::read_text_file(
      config.paths.config_path() / "entities.json",
      "dedicated entity definitions");
  if (!entity_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_definition_text.error().message));
  }

  auto entity_definitions =
      entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_definitions.error().message));
  }

  auto entity_registry_result =
      entity::EntityRegistry::build_with_builtins(std::move(entity_definitions.value()));
  if (!entity_registry_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_registry_result.error().message));
  }

  registries.entity_registry = std::move(entity_registry_result.value());
  core::info(
      "dedicated",
      "loaded entity registry: types={}",
      registries.entity_registry.type_count());

  return Expected<DedicatedRegistries, DedicatedError>::success(std::move(registries));
}

} // namespace arv::dedicated
