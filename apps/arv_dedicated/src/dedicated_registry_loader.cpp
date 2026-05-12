#include "dedicated_registry_loader.hpp"

#include "arv/asset/resource.hpp"
#include "arv/base/base_module.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/log.hpp"
#include "arv/lang/language.hpp"
#include "arv/persistence/save.hpp"

#include <filesystem>
#include <optional>
#include <utility>

namespace arv::dedicated {
namespace {

[[nodiscard]] std::optional<DedicatedError> apply_block_language(
    const lang::LanguageCatalog& language,
    voxel::BlockRegistry& registry) {
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    const std::string key = lang::block_display_name_key(block->name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return dedicated_error("lang", "missing language key: " + key);
    }
    registry.set_display_name(block_id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DedicatedError> apply_item_language(
    const lang::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry) {
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    const std::string key = lang::item_display_name_key(item->name);
    if (const std::optional<std::string_view> text = language.find(key);
        text.has_value()) {
      registry.set_display_name(item_id, std::string{text.value()});
      continue;
    }
    if (item->source_block.has_value()) {
      if (const voxel::BlockRecord* block = blocks.block(item->source_block.value());
          block != nullptr && !block->display_name.empty()) {
        registry.set_display_name(item_id, block->display_name);
        continue;
      }
    }
    return dedicated_error("lang", "missing language key: " + key);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DedicatedError> apply_entity_language(
    const lang::LanguageCatalog& language,
    entity::EntityRegistry& registry) {
  for (const entity::EntityTypeRecord& type : registry.records()) {
    const std::string key = lang::entity_display_name_key(type.name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return dedicated_error("lang", "missing language key: " + key);
    }
    registry.set_display_name(type.id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] lang::LanguageResult load_pack_language(
    const asset::PackManager& packs,
    std::string locale,
    std::string fallback_locale) {
  const auto paths_for = [&](std::string_view selected_locale) {
    std::vector<std::filesystem::path> paths;
    const std::vector<asset::PackDataFile> files =
        packs.data_files_at("lang/" + std::string{selected_locale} + ".yaml");
    paths.reserve(files.size());
    for (const asset::PackDataFile& file : files) {
      paths.push_back(file.path);
    }
    return paths;
  };

  auto fallback = lang::load_language_files(paths_for(fallback_locale), fallback_locale);
  if (!fallback) {
    return fallback;
  }
  lang::LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    auto selected = lang::load_language_files(paths_for(locale), locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  catalog.set_locale(std::move(locale));
  return lang::LanguageResult::success(std::move(catalog));
}

} // namespace

Expected<DedicatedRegistries, DedicatedError> load_dedicated_registries(
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const world::EntityGoalRegistry& entity_goals) {
  auto language_result = load_pack_language(
      packs,
      config.language.locale,
      config.language.fallback_locale);
  if (!language_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("lang", language_result.error().message));
  }
  lang::LanguageCatalog language = std::move(language_result.value());

  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("blocks", block_definition_text.error().describe()));
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
  registries.language = std::move(language);
  registries.block_registry = std::move(block_registry_result.value());
  if (auto error = apply_block_language(registries.language, registries.block_registry);
      error.has_value()) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(error.value());
  }
  asset::ResourceManager resources(packs);
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

  auto item_definition_text = packs.load_merged_json_array_text("items", "items");
  if (!item_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("items", item_definition_text.error().describe()));
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
  if (auto error = apply_item_language(
          registries.language,
          registries.block_registry,
          registries.item_registry);
      error.has_value()) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(error.value());
  }
  registries.item_registry_hash =
      persistence::hash_item_registry(registries.item_registry);
  core::info(
      "dedicated",
      "loaded item registry: items={} hash={}",
      registries.item_registry.item_count(),
      registries.item_registry_hash);

  auto entity_definition_text = packs.load_merged_json_array_text("entities", "entities");
  if (!entity_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_definition_text.error().describe()));
  }

  auto entity_definitions =
      entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_definitions.error().message));
  }

  auto entity_registry_result =
      base::build_base_entity_registry(
          std::move(entity_definitions.value()),
          &entity_goals);
  if (!entity_registry_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("entities", entity_registry_result.error().message));
  }

  registries.entity_registry = std::move(entity_registry_result.value());
  if (auto error = apply_entity_language(registries.language, registries.entity_registry);
      error.has_value()) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(error.value());
  }
  core::info(
      "dedicated",
      "loaded entity registry: types={}",
      registries.entity_registry.type_count());

  return Expected<DedicatedRegistries, DedicatedError>::success(std::move(registries));
}

} // namespace arv::dedicated
