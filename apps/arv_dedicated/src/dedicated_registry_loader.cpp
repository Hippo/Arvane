#include "dedicated_registry_loader.hpp"

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/core/log.hpp"
#include "arv/crafting/crafting.hpp"
#include "arv/crafting/smelting.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/persistence/save.hpp"
#include "arv/text/language.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <utility>

namespace arv::dedicated {
namespace {

[[nodiscard]] std::string hash_text(std::string_view text) {
  return crypto::hash_256_hex(crypto::hash_256(
      std::span<const std::byte>{
          reinterpret_cast<const std::byte*>(text.data()),
          text.size()}));
}

[[nodiscard]] io::DataValue string_array(std::span<const std::string> values) {
  io::DataValue array = io::DataValue::array();
  for (const std::string& value : values) {
    array.push(value);
  }
  return array;
}

[[nodiscard]] io::DataValue light_emission_value(const voxel::BlockProperties& properties) {
  io::DataValue array = io::DataValue::array();
  array.push(static_cast<u64>(properties.emission_r));
  array.push(static_cast<u64>(properties.emission_g));
  array.push(static_cast<u64>(properties.emission_b));
  return array;
}

[[nodiscard]] io::DataValue block_variants_value(const voxel::BlockRegistry& registry) {
  io::DataValue array = io::DataValue::array();
  for (u32 index = 0; index < registry.variant_count(); ++index) {
    const voxel::BlockVariantID variant_id{index};
    const voxel::BlockVariantRecord* variant = registry.variant(variant_id);
    if (variant == nullptr) {
      continue;
    }
    io::DataValue entry = io::DataValue::object();
    entry.set("numeric_id", static_cast<u64>(variant->id.value));
    entry.set("block_numeric_id", static_cast<u64>(variant->block_id.value));
    entry.set("debug_name", variant->debug_name);
    entry.set("solid", variant->properties.solid);
    entry.set("collidable", variant->properties.collidable);
    entry.set("transparent", !variant->properties.opaque);
    entry.set("blocks_light", variant->properties.blocks_light);
    entry.set("replaceable", variant->properties.replaceable);
    entry.set("light_emission", light_emission_value(variant->properties));
    entry.set("particle_texture", variant->particle_texture_asset);
    array.push(std::move(entry));
  }
  return array;
}

[[nodiscard]] io::DataValue blocks_snapshot_value(const voxel::BlockRegistry& registry) {
  io::DataValue array = io::DataValue::array();
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    io::DataValue entry = io::DataValue::object();
    entry.set("numeric_id", static_cast<u64>(block->id.value));
    entry.set("resource_id", block->name);
    entry.set("display_name", block->display_name);
    entry.set("default_variant_numeric_id", static_cast<u64>(block->default_variant.value));
    entry.set("tags", string_array(block->tags));
    entry.set("solid", block->properties.solid);
    entry.set("collidable", block->properties.collidable);
    entry.set("transparent", !block->properties.opaque);
    entry.set("blocks_light", block->properties.blocks_light);
    entry.set("replaceable", block->properties.replaceable);
    entry.set("full_cube", block->properties.full_cube);
    entry.set("light_emission", light_emission_value(block->properties));
    entry.set("particle_texture", block->particle_texture_asset);
    array.push(std::move(entry));
  }
  return array;
}

[[nodiscard]] io::DataValue items_snapshot_value(const player::ItemRegistry& registry) {
  io::DataValue array = io::DataValue::array();
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    io::DataValue entry = io::DataValue::object();
    entry.set("numeric_id", static_cast<u64>(item->id.value));
    entry.set("resource_id", item->name);
    entry.set("display_name", item->display_name);
    entry.set("category", item->category);
    entry.set("tags", string_array(item->tags));
    entry.set("max_stack_size", static_cast<u64>(item->max_stack_size));
    entry.set("texture_asset", item->texture_asset);
    entry.set("model_asset", item->model_asset);
    if (item->render_block_variant.has_value()) {
      entry.set(
          "render_block_variant_numeric_id",
          static_cast<u64>(item->render_block_variant->value));
    }
    array.push(std::move(entry));
  }
  return array;
}

[[nodiscard]] io::DataValue entities_snapshot_value(const entity::EntityRegistry& registry) {
  io::DataValue array = io::DataValue::array();
  for (const entity::EntityTypeRecord& type : registry.records()) {
    io::DataValue entry = io::DataValue::object();
    entry.set("numeric_id", static_cast<u64>(type.id.value));
    entry.set("resource_id", type.name);
    entry.set("display_name", type.display_name);
    entry.set("category", type.category);
    entry.set("model_asset", type.model_asset);
    entry.set("animation_asset", type.animation_asset);
    entry.set("spawnable", type.spawnable);
    entry.set("replicated_by_default", type.replicated_by_default);
    if (type.components.collision.has_value()) {
      io::DataValue collision = io::DataValue::object();
      collision.set("shape", "box");
      io::DataValue box = io::DataValue::array();
      box.push(static_cast<f64>(type.components.collision->box.x));
      box.push(static_cast<f64>(type.components.collision->box.y));
      box.push(static_cast<f64>(type.components.collision->box.z));
      collision.set("box", std::move(box));
      entry.set("collision", std::move(collision));
    }
    array.push(std::move(entry));
  }
  return array;
}

[[nodiscard]] Expected<std::string, DedicatedError> make_client_registry_snapshot_json(
    const voxel::BlockRegistry& blocks,
    const player::ItemRegistry& items,
    const entity::EntityRegistry& entities) {
  io::DataValue root = io::DataValue::object();
  root.set("format_version", static_cast<u64>(1U));
  root.set("blocks", blocks_snapshot_value(blocks));
  root.set("block_variants", block_variants_value(blocks));
  root.set("items", items_snapshot_value(items));
  root.set("entities", entities_snapshot_value(entities));
  auto encoded = io::encode_json_value(root);
  if (!encoded) {
    return Expected<std::string, DedicatedError>::failure(
        dedicated_error("registry", "failed to encode client registry snapshot: " + encoded.error().describe()));
  }
  return Expected<std::string, DedicatedError>::success(std::move(encoded.value()));
}

[[nodiscard]] std::optional<DedicatedError> apply_block_language(
    const arv::text::LanguageCatalog& language,
    voxel::BlockRegistry& registry) {
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockID block_id{index};
    const voxel::BlockRecord* block = registry.block(block_id);
    if (block == nullptr) {
      continue;
    }
    const std::string key = arv::text::block_display_name_key(block->name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return dedicated_error("lang", "missing language key: " + key);
    }
    registry.set_display_name(block_id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DedicatedError> apply_item_language(
    const arv::text::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry) {
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemID item_id{index};
    const player::ItemRecord* item = registry.item(item_id);
    if (item == nullptr) {
      continue;
    }
    const std::string key = arv::text::item_display_name_key(item->name);
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
    const arv::text::LanguageCatalog& language,
    entity::EntityRegistry& registry) {
  for (const entity::EntityTypeRecord& type : registry.records()) {
    const std::string key = arv::text::entity_display_name_key(type.name);
    const std::optional<std::string_view> text = language.find(key);
    if (!text.has_value()) {
      return dedicated_error("lang", "missing language key: " + key);
    }
    registry.set_display_name(type.id, std::string{text.value()});
  }
  return std::nullopt;
}

[[nodiscard]] arv::text::LanguageResult load_pack_language(
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

  auto fallback = arv::text::load_language_files(paths_for(fallback_locale), fallback_locale);
  if (!fallback) {
    return fallback;
  }
  arv::text::LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    auto selected = arv::text::load_language_files(paths_for(locale), locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  if (auto messages = packs.load_config_text("arvane:messages", ".yaml");
      messages) {
    auto config_messages = arv::text::parse_language_yaml(
        messages.value(),
        catalog.locale());
    if (!config_messages) {
      return config_messages;
    }
    catalog.merge_from(std::move(config_messages.value()));
  }
  catalog.set_locale(std::move(locale));
  return arv::text::LanguageResult::success(std::move(catalog));
}

} // namespace

Expected<DedicatedRegistries, DedicatedError> load_dedicated_registries(
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const voxel::BlockBehaviorRegistry& block_behaviors,
    const player::ItemBehaviorRegistry& item_behaviors,
    const world::EntityGoalRegistry& entity_goals) {
  auto language_result = load_pack_language(
      packs,
      config.language.locale,
      config.language.fallback_locale);
  if (!language_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("lang", language_result.error().message));
  }
  arv::text::LanguageCatalog language = std::move(language_result.value());

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

  const auto block_behavior_exists = [&block_behaviors](std::string_view id) {
    return block_behaviors.contains(id);
  };
  auto block_registry_result =
      voxel::BlockRegistry::build(
          std::move(block_definitions.value()),
          {.behavior_exists = FunctionRef<bool(std::string_view)>{block_behavior_exists}});
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

  auto container_type_text = packs.load_merged_json_array_text("container_types", "container_types");
  if (!container_type_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_types", container_type_text.error().describe()));
  }
  auto container_type_definitions =
      player::parse_container_type_definitions_json(container_type_text.value());
  if (!container_type_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_types", container_type_definitions.error().message));
  }
  auto container_type_registry =
      player::ContainerTypeRegistry::build(std::move(container_type_definitions.value()));
  if (!container_type_registry) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_types", container_type_registry.error().message));
  }
  registries.container_types = std::move(container_type_registry.value());

  auto container_layout_text =
      packs.load_merged_json_array_text("container_layouts", "container_layouts");
  if (!container_layout_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_layouts", container_layout_text.error().describe()));
  }
  auto container_layout_definitions =
      player::parse_container_ui_layout_definitions_json(container_layout_text.value());
  if (!container_layout_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_layouts", container_layout_definitions.error().message));
  }
  auto container_layout_registry =
      player::ContainerUiLayoutRegistry::build(std::move(container_layout_definitions.value()));
  if (!container_layout_registry) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("container_layouts", container_layout_registry.error().message));
  }
  registries.container_layouts = std::move(container_layout_registry.value());

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

  const auto item_behavior_exists = [&item_behaviors](std::string_view id) {
    return item_behaviors.contains(id);
  };
  auto item_registry_result =
      player::ItemRegistry::build_from_blocks_and_items(
          registries.block_registry,
          std::move(item_definitions.value()),
          {.behavior_exists = FunctionRef<bool(std::string_view)>{item_behavior_exists},
           .default_block_item_behavior_id = std::string{base::ids::BlockItemBehaviorID}});
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
  auto crafting_result =
      crafting::load_crafting_registry_from_packs(packs, registries.item_registry);
  if (!crafting_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("crafting", crafting_result.error().message));
  }
  for (const crafting::CraftingLoadDiagnostic& diagnostic :
       crafting_result.value().diagnostics) {
    core::warn("dedicated", diagnostic.describe());
  }
  registries.crafting_registry = std::move(crafting_result.value().registry);
  auto smelting_result =
      crafting::load_smelting_rule_registry_from_packs(packs, registries.item_registry);
  if (!smelting_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("smelting", smelting_result.error().message));
  }
  registries.smelting_registry = std::move(smelting_result.value());
  core::info(
      "dedicated",
      "loaded item registry: items={} hash={} recipes={} smelting_rules={}",
      registries.item_registry.item_count(),
      registries.item_registry_hash,
      registries.crafting_registry.recipes().size(),
      registries.smelting_registry.rules().size());

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

  auto vehicle_definition_text = packs.load_merged_json_array_text("vehicles", "vehicles");
  if (!vehicle_definition_text) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("vehicles", vehicle_definition_text.error().describe()));
  }
  auto vehicle_definitions =
      entity::parse_vehicle_definitions_json(vehicle_definition_text.value());
  if (!vehicle_definitions) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("vehicles", vehicle_definitions.error().message));
  }
  auto vehicle_registry_result =
      entity::VehicleDefinitionRegistry::build(std::move(vehicle_definitions.value()));
  if (!vehicle_registry_result) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(
        dedicated_error("vehicles", vehicle_registry_result.error().message));
  }
  registries.vehicle_registry = std::move(vehicle_registry_result.value());
  core::info(
      "dedicated",
      "loaded vehicle registry: definitions={}",
      registries.vehicle_registry.definition_count());

  auto snapshot_json = make_client_registry_snapshot_json(
      registries.block_registry,
      registries.item_registry,
      registries.entity_registry);
  if (!snapshot_json) {
    return Expected<DedicatedRegistries, DedicatedError>::failure(snapshot_json.error());
  }
  registries.client_registry_snapshot_json = std::move(snapshot_json.value());
  registries.client_registry_snapshot_hash =
      hash_text(registries.client_registry_snapshot_json);
  core::info(
      "dedicated",
      "built client registry snapshot: hash={} bytes={}",
      registries.client_registry_snapshot_hash,
      registries.client_registry_snapshot_json.size());

  return Expected<DedicatedRegistries, DedicatedError>::success(std::move(registries));
}

} // namespace arv::dedicated
