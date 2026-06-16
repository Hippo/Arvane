#include "arv/world/natural_spawning.hpp"

#include "arv/voxel/light.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_time.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace arv::world {
namespace {

[[nodiscard]] bool has_distance_to_player(
    BlockPos candidate,
    const std::vector<Vec3f>& player_positions,
    u32 min_distance,
    u32 max_distance) noexcept {
  if (player_positions.empty()) {
    return false;
  }
  const Vec3f pos = block_floor_center(candidate);
  const f32 min_sq = static_cast<f32>(min_distance) * static_cast<f32>(min_distance);
  const f32 max_sq = static_cast<f32>(max_distance) * static_cast<f32>(max_distance);
  bool within_max = false;
  for (const Vec3f player_pos : player_positions) {
    const f32 distance_sq = horizontal_distance_squared(pos, player_pos);
    if (distance_sq < min_sq) {
      return false;
    }
    within_max = within_max || distance_sq <= max_sq;
  }
  return within_max;
}

[[nodiscard]] f32 nearest_player_distance_sq(
    Vec3f pos,
    const std::vector<Vec3f>& player_positions) noexcept {
  f32 nearest = std::numeric_limits<f32>::infinity();
  for (const Vec3f player_pos : player_positions) {
    nearest = std::min(nearest, horizontal_distance_squared(pos, player_pos));
  }
  return nearest;
}

[[nodiscard]] SpawnConditions convert_conditions(
    const worldgen::BiomeSpawnConditionConfig& raw) {
  SpawnConditions conditions;
  conditions.min_y = raw.min_y;
  conditions.max_y = raw.max_y;
  conditions.min_light = raw.min_light;
  conditions.max_light = raw.max_light;
  conditions.requires_surface = raw.requires_surface;
  conditions.requires_sky_access = raw.requires_sky_access;
  conditions.requires_water = raw.requires_water;
  conditions.requires_air_body = raw.requires_air_body;
  conditions.requires_air_head = raw.requires_air_head;
  conditions.min_distance_from_player_blocks = raw.min_distance_from_player_blocks;
  conditions.max_distance_from_player_blocks = raw.max_distance_from_player_blocks;
  conditions.time = raw.time;
  conditions.block_tags_below.reserve(raw.block_tags_below.size());
  for (const worldgen::ResourceRef& tag : raw.block_tags_below) {
    conditions.block_tags_below.push_back(tag.id);
  }
  conditions.block_tags_inside.reserve(raw.block_tags_inside.size());
  for (const worldgen::ResourceRef& tag : raw.block_tags_inside) {
    conditions.block_tags_inside.push_back(tag.id);
  }
  conditions.block_tags_head.reserve(raw.block_tags_head.size());
  for (const worldgen::ResourceRef& tag : raw.block_tags_head) {
    conditions.block_tags_head.push_back(tag.id);
  }
  return conditions;
}

[[nodiscard]] u64 elapsed_microseconds(
    std::chrono::steady_clock::time_point started,
    std::chrono::steady_clock::time_point finished) noexcept {
  return static_cast<u64>(
      std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count());
}

[[nodiscard]] bool contains_chunk_section(
    const std::vector<ChunkSectionCoord>& chunks,
    ChunkSectionCoord coord) noexcept {
  return std::find(chunks.begin(), chunks.end(), coord) != chunks.end();
}

[[nodiscard]] bool loaded_candidate_section_is_near_player(
    BlockPos candidate,
    const std::vector<Vec3f>& player_positions,
    u32 max_distance) noexcept {
  return has_distance_to_player(candidate, player_positions, 0U, max_distance);
}

[[nodiscard]] bool candidate_section_is_simulated(
    const WorldRuntime& world,
    BlockPos candidate,
    const SpawnEvaluationContext& context) noexcept {
  const ChunkSectionCoord chunk = block_to_chunk(candidate);
  if (world.find_chunk(chunk) == nullptr) {
    return false;
  }
  if (world.is_chunk_active(chunk)) {
    return true;
  }
  return context.allow_loaded_candidate_sections_near_players &&
         loaded_candidate_section_is_near_player(
             candidate,
             context.player_positions,
             context.loaded_candidate_section_max_distance_blocks);
}

[[nodiscard]] bool planned_candidate_section_is_simulated(
    const NaturalSpawnJobRequest& request,
    BlockPos candidate,
    const std::vector<Vec3f>& player_positions) noexcept {
  const ChunkSectionCoord section = block_to_chunk(candidate);
  if (!contains_chunk_section(request.loaded_chunks, section)) {
    return false;
  }
  return contains_chunk_section(request.active_chunks, section) ||
         loaded_candidate_section_is_near_player(
             candidate,
             player_positions,
             request.config.max_distance_from_player_blocks);
}

[[nodiscard]] bool spawn_body_cell_is_clear(
    const voxel::BlockRegistry& block_registry,
    voxel::BlockVariantID block) noexcept {
  if (block_registry.is_air(block)) {
    return true;
  }
  const voxel::BlockVariantRecord* record = block_registry.variant(block);
  return record != nullptr &&
         record->properties.replaceable &&
         !record->properties.fluid &&
         !record->properties.collidable;
}

[[nodiscard]] std::vector<Vec3f> player_positions(
    const std::vector<NaturalSpawnPlayerSnapshot>& players) {
  std::vector<Vec3f> positions;
  positions.reserve(players.size());
  for (const NaturalSpawnPlayerSnapshot& player : players) {
    positions.push_back(player.position);
  }
  return positions;
}

[[nodiscard]] bool has_world_budget(
    const SpawnLoadSnapshot& load,
    SpawnCategory category,
    usize player_count,
    const NaturalSpawningConfig& config) noexcept {
  const SpawnCategoryCapConfig& caps = config.caps[spawn_category_index(category)];
  const u32 per_player_cap =
      caps.per_player * static_cast<u32>(std::max<usize>(player_count, 1U));
  const u32 category_count = load.natural_by_category[spawn_category_index(category)];
  return category_count < per_player_cap && category_count < caps.per_world_soft;
}

[[nodiscard]] u32 natural_entity_count(const SpawnLoadSnapshot& load) noexcept {
  u32 count = 0U;
  for (const u32 category_count : load.natural_by_category) {
    count += category_count;
  }
  return count;
}

[[nodiscard]] u32 remaining_natural_entity_budget(
    const SpawnLoadSnapshot& load,
    const NaturalSpawningConfig& config) noexcept {
  const u32 count = natural_entity_count(load);
  return count >= config.max_natural_entities ? 0U : config.max_natural_entities - count;
}

[[nodiscard]] bool has_natural_entity_budget(
    const SpawnLoadSnapshot& load,
    const NaturalSpawningConfig& config) noexcept {
  return remaining_natural_entity_budget(load, config) > 0U;
}

[[nodiscard]] u32 scaled_local_max(
    const NaturalSpawningConfig& config,
    SpawnCategory category,
    f32 density_cap_scale) noexcept {
  const SpawnCategoryCapConfig& caps = config.caps[spawn_category_index(category)];
  if (caps.local_max == 0U || !std::isfinite(density_cap_scale) ||
      density_cap_scale <= 0.0F) {
    return 0U;
  }
  const double scaled =
      std::floor(static_cast<double>(caps.local_max) * static_cast<double>(density_cap_scale));
  if (scaled <= 1.0) {
    return 1U;
  }
  constexpr double MaxU32 = static_cast<double>(std::numeric_limits<u32>::max());
  if (scaled >= MaxU32) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(scaled);
}

[[nodiscard]] bool has_local_budget(
    const std::vector<NaturalSpawnEntitySnapshot>& natural_entities,
    BlockPos pos,
    SpawnCategory category,
    EntityTypeID type,
    const NaturalSpawningConfig& config,
    f32 density_cap_scale = 1.0F) noexcept {
  (void)type;
  const u32 local_max = scaled_local_max(config, category, density_cap_scale);
  if (local_max == 0U) {
    return false;
  }
  const f32 radius = static_cast<f32>(config.local_density_radius_blocks);
  const f32 radius_sq = radius * radius;
  const Vec3f center = block_floor_center(pos);
  u32 count = 0;
  for (const NaturalSpawnEntitySnapshot& entity : natural_entities) {
    if (entity.category != category ||
        horizontal_distance_squared(entity.position, center) > radius_sq) {
      continue;
    }
    ++count;
    if (count >= local_max) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool time_conditions_pass(
    const SpawnConditions& conditions,
    const WorldClock& clock) noexcept {
  if (!conditions.time.has_value()) {
    return true;
  }
  const f32 daylight = build_celestial_state(clock).daylight_strength;
  if (conditions.time.value() == TimePredicate::Day) {
    return daylight >= 0.5F;
  }
  return daylight < 0.5F;
}

[[nodiscard]] BlockPos pick_planned_candidate_near_player(
    const NaturalSpawnPlayerSnapshot& player,
    SpawnCategory category,
    const worldgen::TerrainProfile& profile,
    ChunkCoord column,
    std::mt19937_64& rng) {
  (void)player;
  const i32 local_x = std::uniform_int_distribution<i32>(0, ChunkSize - 1)(rng);
  const i32 local_z = std::uniform_int_distribution<i32>(0, ChunkSize - 1)(rng);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  i32 y = profile.surface_y_by_column[column_index] + 1;
  if (category == SpawnCategory::Water) {
    y = profile.surface_y_by_column[column_index];
  }
  return {{column.x * ChunkSize + local_x,
           y,
           column.z * ChunkSize + local_z}};
}

[[nodiscard]] BlockPos pick_planned_group_member(BlockPos root, std::mt19937_64& rng) {
  std::uniform_int_distribution<i32> offset(-4, 4);
  return {{root.value.x + offset(rng),
           root.value.y,
           root.value.z + offset(rng)}};
}

void merge_planning_counters(
    NaturalSpawnCounters& target,
    const NaturalSpawnCounters& source) noexcept {
  target.attempts_total += source.attempts_total;
  target.attempts_last_interval += source.attempts_last_interval;
  target.rejected_chunk_not_loaded += source.rejected_chunk_not_loaded;
  target.rejected_chunk_not_simulated += source.rejected_chunk_not_simulated;
  target.rejected_no_biome_table += source.rejected_no_biome_table;
  target.rejected_category_cap += source.rejected_category_cap;
  target.rejected_world_cap += source.rejected_world_cap;
  target.rejected_local_density += source.rejected_local_density;
  target.rejected_conditions += source.rejected_conditions;
  target.rejected_group_failed += source.rejected_group_failed;
  target.rejected_no_generation_context += source.rejected_no_generation_context;
  target.spawn_intents_generated += source.spawn_intents_generated;
  target.despawn_intents_generated += source.despawn_intents_generated;
}

void record_despawn_counter(
    NaturalSpawnCounters& counters,
    SpawnCategory category) noexcept {
  switch (category) {
    case SpawnCategory::Hostile:
      ++counters.despawned_natural_hostile;
      break;
    case SpawnCategory::Ambient:
      ++counters.despawned_natural_ambient;
      break;
    case SpawnCategory::Water:
      ++counters.despawned_natural_water;
      break;
    default:
      ++counters.despawned_natural_other;
      break;
  }
}

} // namespace

std::string_view spawn_category_name(SpawnCategory category) noexcept {
  switch (category) {
    case SpawnCategory::Passive:
      return "passive";
    case SpawnCategory::Hostile:
      return "hostile";
    case SpawnCategory::Ambient:
      return "ambient";
    case SpawnCategory::Water:
      return "water";
    case SpawnCategory::Underground:
      return "underground";
    case SpawnCategory::Special:
      return "special";
    case SpawnCategory::Count:
      break;
  }
  return "unknown";
}

std::optional<SpawnCategory> spawn_category_from_string(std::string_view name) noexcept {
  for (const SpawnCategory category : AllSpawnCategories) {
    if (spawn_category_name(category) == name) {
      return category;
    }
  }
  return std::nullopt;
}

std::optional<TimePredicate> time_predicate_from_string(std::string_view name) noexcept {
  if (name == "day") {
    return TimePredicate::Day;
  }
  if (name == "night") {
    return TimePredicate::Night;
  }
  return std::nullopt;
}

std::string_view time_predicate_name(TimePredicate predicate) noexcept {
  switch (predicate) {
    case TimePredicate::Day:
      return "day";
    case TimePredicate::Night:
      return "night";
  }
  return "unknown";
}

std::string SpawnDiagnostic::describe() const {
  std::string message = biome_id.empty() ? std::string{"spawn table"} : biome_id;
  if (!field.empty()) {
    message += "." + field;
  }
  message += ": " + this->message;
  return message;
}

bool BiomeSpawnTable::empty() const noexcept {
  return entries.empty() || total_weight == 0U;
}

const SpawnEntry* BiomeSpawnTable::weighted_pick(u32 roll) const noexcept {
  if (empty()) {
    return nullptr;
  }
  u32 cursor = roll % total_weight;
  for (const SpawnEntry& entry : entries) {
    if (cursor < entry.weight) {
      return &entry;
    }
    cursor -= entry.weight;
  }
  return &entries.back();
}

SpawnRuleRegistry SpawnRuleRegistry::build_from_dimension(
    const worldgen::ResolvedDimension& dimension,
    const entity::EntityRegistry& entity_registry,
    const voxel::BlockRegistry& block_registry) {
  SpawnRuleRegistry registry;
  registry.tables_.reserve(dimension.biomes.size());

  for (const worldgen::ResolvedBiome& biome : dimension.biomes) {
    BiomeTables tables;
    tables.biome_id = biome.raw.id;
    for (const SpawnCategory category : AllSpawnCategories) {
      const usize category_index = spawn_category_index(category);
      const worldgen::BiomeSpawnCategoryConfig& config =
          biome.raw.spawn_categories[category_index];
      if (config.specified) {
        tables.categories[category_index].rate_scale = config.rate_scale;
        tables.categories[category_index].density_cap_scale = config.density_cap_scale;
      }
    }
    for (const worldgen::BiomeSpawnEntryConfig& raw_entry : biome.raw.spawns) {
      const usize category = spawn_category_index(raw_entry.category);
      const std::optional<EntityTypeID> type_id =
          entity_registry.find_type(raw_entry.entity.id);
      if (!type_id.has_value()) {
        registry.diagnostics_.push_back(
            {biome.raw.id, "spawns." + std::string{spawn_category_name(raw_entry.category)}, "unknown entity type: " + raw_entry.entity.id});
        continue;
      }
      const entity::EntityTypeRecord* type = entity_registry.type(type_id.value());
      if (type == nullptr || !type->spawnable) {
        registry.diagnostics_.push_back(
            {biome.raw.id, "spawns." + std::string{spawn_category_name(raw_entry.category)}, "entity type is not spawnable: " + raw_entry.entity.id});
        continue;
      }

      bool tags_valid = true;
      auto validate_tags = [&](std::string_view field, const std::vector<worldgen::ResourceRef>& tags) {
        for (const worldgen::ResourceRef& tag : tags) {
          if (!voxel::block_tag_exists(block_registry, tag.id)) {
            tags_valid = false;
            registry.diagnostics_.push_back(
                {biome.raw.id, std::string{"spawns.conditions."} + std::string{field}, "unknown block tag: " + tag.id});
          }
        }
      };
      validate_tags("block_tags_below", raw_entry.conditions.block_tags_below);
      validate_tags("block_tags_inside", raw_entry.conditions.block_tags_inside);
      validate_tags("block_tags_head", raw_entry.conditions.block_tags_head);
      if (!tags_valid) {
        continue;
      }

      SpawnEntry entry;
      entry.entity_type = type_id.value();
      entry.entity_type_name = type->name;
      entry.category = raw_entry.category;
      entry.weight = raw_entry.weight;
      entry.min_group = raw_entry.min_group;
      entry.max_group = raw_entry.max_group;
      entry.conditions = convert_conditions(raw_entry.conditions);
      entry.persistent_by_default = type->persistent_by_default;
      entry.replicated_by_default = type->replicated_by_default;
      entry.authored_components = type->components;

      BiomeSpawnTable& table = tables.categories[category];
      table.total_weight += entry.weight;
      table.entries.push_back(std::move(entry));
    }
    registry.tables_.push_back(std::move(tables));
  }

  return registry;
}

const BiomeSpawnTable* SpawnRuleRegistry::find_table(
    std::string_view biome_id,
    SpawnCategory category) const noexcept {
  for (const BiomeTables& tables : tables_) {
    if (tables.biome_id == biome_id) {
      const BiomeSpawnTable& table = tables.categories[spawn_category_index(category)];
      return table.empty() ? nullptr : &table;
    }
  }
  return nullptr;
}

usize SpawnRuleRegistry::entry_count() const noexcept {
  usize count = 0;
  for (const BiomeTables& tables : tables_) {
    for (const BiomeSpawnTable& table : tables.categories) {
      count += table.entries.size();
    }
  }
  return count;
}

bool SpawnConditionEvaluator::passes(
    const WorldRuntime& world,
    const SpawnEntry& entry,
    BlockPos candidate,
    SpawnCategory category,
    const SpawnEvaluationContext& context) const {
  (void)category;
  const SpawnConditions& conditions = entry.conditions;
  if (!candidate_section_is_simulated(world, candidate, context)) {
    return false;
  }
  if (conditions.min_y.has_value() && candidate.value.y < conditions.min_y.value()) {
    return false;
  }
  if (conditions.max_y.has_value() && candidate.value.y > conditions.max_y.value()) {
    return false;
  }
  if (!has_distance_to_player(
          candidate,
          context.player_positions,
          conditions.min_distance_from_player_blocks,
          conditions.max_distance_from_player_blocks)) {
    return false;
  }
  if (conditions.time.has_value()) {
    const f32 daylight = build_celestial_state(world.clock()).daylight_strength;
    if (conditions.time.value() == TimePredicate::Day && daylight < 0.5F) {
      return false;
    }
    if (conditions.time.value() == TimePredicate::Night && daylight >= 0.5F) {
      return false;
    }
  }
  const voxel::BlockRegistry* block_registry = context.block_registry;
  if (block_registry == nullptr) {
    return false;
  }

  const BlockPos below{{candidate.value.x, candidate.value.y - 1, candidate.value.z}};
  const BlockPos head{{candidate.value.x, candidate.value.y + 1, candidate.value.z}};
  const voxel::BlockVariantID below_block = world.get_block(below);
  const voxel::BlockVariantID body_block = world.get_block(candidate);
  const voxel::BlockVariantID head_block = world.get_block(head);

  if (conditions.requires_air_body &&
      !spawn_body_cell_is_clear(*block_registry, body_block)) {
    return false;
  }
  if (conditions.requires_air_head &&
      !spawn_body_cell_is_clear(*block_registry, head_block)) {
    return false;
  }
  if (conditions.requires_surface && !voxel::is_standing_surface(*block_registry, below_block)) {
    return false;
  }
  if (conditions.requires_water &&
      !world_cell_contains_fluid(world, *block_registry, candidate)) {
    return false;
  }
  if (!voxel::block_variant_has_any_tag(*block_registry, below_block, conditions.block_tags_below) ||
      !voxel::block_variant_has_any_tag(*block_registry, body_block, conditions.block_tags_inside) ||
      !voxel::block_variant_has_any_tag(*block_registry, head_block, conditions.block_tags_head)) {
    return false;
  }

  const voxel::LightWord light = world.get_light(candidate);
  const u8 visible = voxel::visible_light_level(light);
  if (conditions.min_light.has_value() && visible < conditions.min_light.value()) {
    return false;
  }
  if (conditions.max_light.has_value() && visible > conditions.max_light.value()) {
    return false;
  }
  if (conditions.requires_sky_access && voxel::light_sky(light) < voxel::MaxLightLevel) {
    return false;
  }
  return true;
}

void SpawnLoadTracker::refresh(const WorldRuntime& world) {
  snapshot_ = {};
  const std::vector<EntityID> ids = world.entity_ids();
  snapshot_.total_world_entities = static_cast<u32>(ids.size());
  for (const EntityID id : ids) {
    const NaturalSpawnComponent* natural = world.find_entity_natural_spawn(id);
    if (natural == nullptr) {
      continue;
    }
    const usize category = spawn_category_index(natural->category);
    ++snapshot_.natural_by_category[category];
    ++snapshot_.total_by_category[category];
  }
}

bool SpawnLoadTracker::has_world_budget(
    SpawnCategory category,
    usize player_count,
    const NaturalSpawningConfig& config) const noexcept {
  const SpawnCategoryCapConfig& caps = config.caps[spawn_category_index(category)];
  const u32 per_player_cap =
      caps.per_player * static_cast<u32>(std::max<usize>(player_count, 1U));
  const u32 category_count = snapshot_.natural_by_category[spawn_category_index(category)];
  return category_count < per_player_cap && category_count < caps.per_world_soft;
}

bool SpawnLoadTracker::has_category_budget(
    SpawnCategory category,
    usize player_count,
    const NaturalSpawningConfig& config) const noexcept {
  return has_world_budget(category, player_count, config);
}

bool SpawnLoadTracker::has_local_budget(
    const WorldRuntime& world,
    BlockPos pos,
    SpawnCategory category,
    EntityTypeID type,
    const NaturalSpawningConfig& config,
    f32 density_cap_scale) const noexcept {
  const u32 local_max = scaled_local_max(config, category, density_cap_scale);
  if (local_max == 0U) {
    return false;
  }
  const f32 radius = static_cast<f32>(config.local_density_radius_blocks);
  const f32 radius_sq = radius * radius;
  const Vec3f center = block_floor_center(pos);
  u32 count = 0;
  for (const EntityID id : world.entity_ids()) {
    const NaturalSpawnComponent* natural = world.find_entity_natural_spawn(id);
    if (natural == nullptr || natural->category != category) {
      continue;
    }
    const entity::EntityTransform* transform = world.find_entity_transform(id);
    if (transform == nullptr ||
        horizontal_distance_squared(transform->position, center) > radius_sq) {
      continue;
    }
    const entity::EntityType* entity_type = world.find_entity_type(id);
    const bool same_type = entity_type != nullptr && entity_type->type_id == type;
    (void)same_type;
    ++count;
    if (count >= local_max) {
      return false;
    }
  }
  return true;
}

void NaturalSpawnSystem::configure(NaturalSpawningConfig config) {
  config.interval_ticks = std::max<TickIndex>(config.interval_ticks, 1U);
  if (config.min_distance_from_player_blocks > config.max_distance_from_player_blocks) {
    std::swap(config.min_distance_from_player_blocks, config.max_distance_from_player_blocks);
  }
  config_ = config;
}

void NaturalSpawnSystem::set_rules(SpawnRuleRegistry rules) {
  rules_ = std::move(rules);
}

std::optional<NaturalSpawnJobRequest> NaturalSpawnSystem::make_job_request(
    const WorldRuntime& world,
    TickIndex tick) {
  if (!config_.enabled || tick % config_.interval_ticks != 0U) {
    return std::nullopt;
  }

  const auto started = std::chrono::steady_clock::now();
  NaturalSpawnJobRequest request;
  request.world_id = world.id();
  request.tick = tick;
  request.config = config_;
  request.rules = rules_;
  request.clock = world.clock();
  request.generation_seed = world.generation_seed();
  request.random_seed =
      rng_() ^
      (static_cast<u64>(tick) * 0x9E3779B97F4A7C15ULL) ^
      (static_cast<u64>(world.id().value) * 0xBF58476D1CE4E5B9ULL);

  if (const WorldGenerationConfig* generation = world.generation_config();
      generation != nullptr) {
    request.dimension = generation->resolved_dimension;
  }

  const std::vector<PlayerID> players = world.player_ids();
  request.players.reserve(players.size());
  for (const PlayerID player_id : players) {
    const player::PlayerState* player = world.find_player(player_id);
    if (player != nullptr) {
      request.players.push_back({player_id, player->position});
    }
  }

  const std::vector<EntityID> entities = world.entity_ids();
  request.load.total_world_entities = static_cast<u32>(entities.size());
  request.natural_entities.reserve(entities.size());
  for (const EntityID entity_id : entities) {
    const NaturalSpawnComponent* natural = world.find_entity_natural_spawn(entity_id);
    if (natural == nullptr) {
      continue;
    }
    const usize category = spawn_category_index(natural->category);
    ++request.load.natural_by_category[category];
    ++request.load.total_by_category[category];

    const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
    if (transform == nullptr) {
      continue;
    }
    request.natural_entities.push_back({entity_id,
                                        natural->category,
                                        natural->source_entity_type,
                                        transform->position,
                                        natural->spawn_tick,
                                        natural->transient,
                                        world.entity_is_persistent(entity_id),
                                        world.player_for_entity(entity_id).has_value()});
  }

  request.loaded_chunks = world.loaded_chunk_coords();
  request.active_chunks.reserve(request.loaded_chunks.size());
  for (const ChunkSectionCoord coord : request.loaded_chunks) {
    if (world.is_chunk_active(coord)) {
      request.active_chunks.push_back(coord);
    }
  }

  counters_.natural_entities_by_category = request.load.natural_by_category;
  counters_.total_entities_by_category = request.load.total_by_category;

  const u64 elapsed_us =
      elapsed_microseconds(started, std::chrono::steady_clock::now());
  ++measured_spawn_prepare_ticks_;
  total_spawn_prepare_cost_us_ += elapsed_us;
  counters_.avg_spawn_prepare_cost_us =
      total_spawn_prepare_cost_us_ / measured_spawn_prepare_ticks_;
  counters_.max_spawn_prepare_cost_us =
      std::max(counters_.max_spawn_prepare_cost_us, elapsed_us);

  return request;
}

NaturalSpawnJobResult NaturalSpawnSystem::build_job_result(
    NaturalSpawnJobRequest request) {
  const auto started = std::chrono::steady_clock::now();
  NaturalSpawnJobResult result;
  result.world_id = request.world_id;
  result.source_tick = request.tick;
  if (!request.config.enabled) {
    result.worker_duration_us =
        elapsed_microseconds(started, std::chrono::steady_clock::now());
    return result;
  }

  const std::vector<Vec3f> positions = player_positions(request.players);

  for (const NaturalSpawnEntitySnapshot& entity : request.natural_entities) {
    if (entity.player_owned) {
      continue;
    }
    const NaturalDespawnCategoryConfig& despawn =
        request.config.despawning[spawn_category_index(entity.category)];
    if (!despawn.natural_despawn) {
      continue;
    }
    if (request.tick < entity.spawn_tick ||
        request.tick - entity.spawn_tick < despawn.min_lifetime_ticks) {
      continue;
    }
    if (entity.persistent && !entity.transient) {
      continue;
    }

    const f32 nearest_sq = nearest_player_distance_sq(entity.position, positions);
    const f32 hard = static_cast<f32>(despawn.hard_distance_blocks);
    const f32 soft = static_cast<f32>(despawn.soft_distance_blocks);
    bool should_despawn = false;
    if (!std::isfinite(nearest_sq) || nearest_sq >= hard * hard) {
      should_despawn = true;
    } else if (nearest_sq >= soft * soft) {
      should_despawn = ((request.tick + entity.id.value) % 4U) == 0U;
    }
    if (should_despawn) {
      result.despawn_intents.push_back({entity.id,
                                        entity.category,
                                        entity.source_entity_type,
                                        entity.spawn_tick});
      ++result.counters.despawn_intents_generated;
    }
  }

  if (request.dimension == nullptr || request.dimension->biomes.empty()) {
    if (request.config.attempts_per_player_per_category > 0U &&
        !request.players.empty()) {
      ++result.counters.rejected_no_generation_context;
    }
    result.worker_duration_us =
        elapsed_microseconds(started, std::chrono::steady_clock::now());
    return result;
  }

  std::mt19937_64 rng{request.random_seed};
  HashMap<ChunkCoord, worldgen::TerrainProfile> profile_cache;
  SpawnLoadSnapshot planning_load = request.load;
  std::vector<NaturalSpawnEntitySnapshot> planning_entities = request.natural_entities;
  u32 world_successes = 0U;

  for (const SpawnCategory category : AllSpawnCategories) {
    if (!has_natural_entity_budget(planning_load, request.config)) {
      ++result.counters.rejected_world_cap;
      break;
    }
    if (!has_world_budget(
            planning_load,
            category,
            request.players.size(),
            request.config)) {
      ++result.counters.rejected_category_cap;
      continue;
    }

    for (const NaturalSpawnPlayerSnapshot& player : request.players) {
      if (world_successes >= request.config.max_successes_per_world_per_tick) {
        break;
      }

      u32 player_successes = 0U;
      for (u32 attempt = 0; attempt < request.config.attempts_per_player_per_category; ++attempt) {
        if (player_successes >= request.config.max_successes_per_player_per_tick ||
            world_successes >= request.config.max_successes_per_world_per_tick) {
          break;
        }

        ++result.counters.attempts_total;
        ++result.counters.attempts_last_interval;

        const i32 min_distance =
            static_cast<i32>(request.config.min_distance_from_player_blocks);
        const i32 max_distance =
            static_cast<i32>(request.config.max_distance_from_player_blocks);
        std::uniform_int_distribution<i32> offset_dist(-max_distance, max_distance);
        i32 dx = 0;
        i32 dz = 0;
        for (u32 tries = 0; tries < 8U; ++tries) {
          dx = offset_dist(rng);
          dz = offset_dist(rng);
          const i32 distance_sq = dx * dx + dz * dz;
          if (distance_sq >= min_distance * min_distance &&
              distance_sq <= max_distance * max_distance) {
            break;
          }
        }

        const i32 world_x = static_cast<i32>(std::floor(player.position.x)) + dx;
        const i32 world_z = static_cast<i32>(std::floor(player.position.z)) + dz;
        const ChunkCoord column{
            floor_div(world_x, ChunkSize),
            floor_div(world_z, ChunkSize)};
        auto profile_iter = profile_cache.find(column);
        if (profile_iter == profile_cache.end()) {
          profile_iter = profile_cache
                             .emplace(
                                 column,
                                 worldgen::generate_terrain_profile(
                                     *request.dimension,
                                     request.generation_seed,
                                     column))
                             .first;
        }

        const BlockPos candidate = pick_planned_candidate_near_player(
            player,
            category,
            profile_iter->second,
            column,
            rng);
        if (!has_distance_to_player(
                candidate,
                positions,
                request.config.min_distance_from_player_blocks,
                request.config.max_distance_from_player_blocks)) {
          ++result.counters.rejected_conditions;
          continue;
        }

        const ChunkSectionCoord section = block_to_chunk(candidate);
        if (!contains_chunk_section(request.loaded_chunks, section)) {
          ++result.counters.rejected_chunk_not_loaded;
          continue;
        }
        if (!planned_candidate_section_is_simulated(request, candidate, positions)) {
          ++result.counters.rejected_chunk_not_simulated;
          continue;
        }

        const i32 local_x = floor_mod(candidate.value.x, ChunkSize);
        const i32 local_z = floor_mod(candidate.value.z, ChunkSize);
        const usize column_index = voxel::local_column_index({local_x, local_z});
        const usize biome_index = std::min(
            profile_iter->second.biome_by_column[column_index],
            request.dimension->biomes.size() - 1U);
        const std::string& biome_id = request.dimension->biomes[biome_index].raw.id;
        const BiomeSpawnTable* table = request.rules.find_table(biome_id, category);
        if (table == nullptr || table->empty()) {
          ++result.counters.rejected_no_biome_table;
          continue;
        }
        if (table->rate_scale < 1.0F) {
          std::uniform_real_distribution<f32> rate_dist(0.0F, 1.0F);
          if (rate_dist(rng) >= table->rate_scale) {
            ++result.counters.rejected_conditions;
            continue;
          }
        }

        std::uniform_int_distribution<u32> weight_dist(0U, table->total_weight - 1U);
        const SpawnEntry* entry = table->weighted_pick(weight_dist(rng));
        if (entry == nullptr) {
          ++result.counters.rejected_no_biome_table;
          continue;
        }
        u32 global_remaining = remaining_natural_entity_budget(planning_load, request.config);
        if (global_remaining == 0U) {
          ++result.counters.rejected_world_cap;
          break;
        }
        if (!has_world_budget(
                planning_load,
                category,
                request.players.size(),
                request.config)) {
          ++result.counters.rejected_world_cap;
          continue;
        }
        if (!has_local_budget(
                planning_entities,
                candidate,
                category,
                entry->entity_type,
                request.config,
                table->density_cap_scale)) {
          ++result.counters.rejected_local_density;
          continue;
        }
        if (!time_conditions_pass(entry->conditions, request.clock)) {
          ++result.counters.rejected_conditions;
          continue;
        }

        std::uniform_int_distribution<u32> group_dist(entry->min_group, entry->max_group);
        const u32 desired_count = std::min(group_dist(rng), global_remaining);
        NaturalSpawnIntent intent;
        intent.entry = *entry;
        intent.positions.reserve(desired_count);
        for (u32 index = 0; index < desired_count; ++index) {
          const BlockPos pos =
              index == 0U ? candidate : pick_planned_group_member(candidate, rng);
          if (std::find(intent.positions.begin(), intent.positions.end(), pos) !=
              intent.positions.end()) {
            continue;
          }
          intent.positions.push_back(pos);
        }
        if (intent.positions.size() < entry->min_group) {
          ++result.counters.rejected_group_failed;
          continue;
        }

        const usize category_index = spawn_category_index(category);
        planning_load.natural_by_category[category_index] +=
            static_cast<u32>(intent.positions.size());
        planning_load.total_by_category[category_index] +=
            static_cast<u32>(intent.positions.size());
        for (const BlockPos pos : intent.positions) {
          planning_entities.push_back({{},
                                       category,
                                       entry->entity_type,
                                       block_floor_center(pos),
                                       request.tick,
                                       true,
                                       false,
                                       false});
        }

        result.counters.spawn_intents_generated +=
            static_cast<u64>(intent.positions.size());
        result.spawn_intents.push_back(std::move(intent));
        ++player_successes;
        ++world_successes;
      }
    }
  }

  result.worker_duration_us =
      elapsed_microseconds(started, std::chrono::steady_clock::now());
  return result;
}

NaturalSpawnApplyStats NaturalSpawnSystem::apply_job_result(
    WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const NaturalSpawnJobResult& result,
    TickIndex tick,
    bool record_job_completion,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) {
  (void)entity_registry;
  const auto started = std::chrono::steady_clock::now();
  NaturalSpawnApplyStats stats;
  counters_.attempts_last_interval = 0U;
  counters_.successes_last_interval = 0U;

  if (record_job_completion) {
    ++counters_.spawn_jobs_completed;
  }
  if (result.worker_duration_us > 0U) {
    ++measured_spawn_worker_ticks_;
    total_spawn_worker_cost_us_ += result.worker_duration_us;
    counters_.avg_spawn_worker_cost_us =
        total_spawn_worker_cost_us_ / measured_spawn_worker_ticks_;
    counters_.max_spawn_worker_cost_us =
        std::max(counters_.max_spawn_worker_cost_us, result.worker_duration_us);
  }

  merge_planning_counters(counters_, result.counters);
  if (result.canceled) {
    stats.apply_duration_us =
        elapsed_microseconds(started, std::chrono::steady_clock::now());
    return stats;
  }

  const TickIndex max_result_age =
      std::max<TickIndex>(config_.interval_ticks * 2U, 1U);
  if (result.world_id != world.id() ||
      tick < result.source_tick ||
      tick - result.source_tick > max_result_age) {
    ++counters_.spawn_jobs_stale;
    stats.stale = true;
    stats.apply_duration_us =
        elapsed_microseconds(started, std::chrono::steady_clock::now());
    return stats;
  }

  SpawnEvaluationContext context;
  context.block_registry = &block_registry;
  context.allow_loaded_candidate_sections_near_players = true;
  context.loaded_candidate_section_max_distance_blocks =
      config_.max_distance_from_player_blocks;
  const std::vector<PlayerID> player_ids = world.player_ids();
  context.player_positions.reserve(player_ids.size());
  for (const PlayerID player_id : player_ids) {
    const player::PlayerState* player = world.find_player(player_id);
    if (player != nullptr) {
      context.player_positions.push_back(player->position);
    }
  }

  for (const NaturalDespawnIntent& intent : result.despawn_intents) {
    const NaturalSpawnComponent* natural =
        world.find_entity_natural_spawn(intent.entity);
    if (natural == nullptr ||
        natural->category != intent.category ||
        natural->source_entity_type != intent.source_entity_type ||
        natural->spawn_tick != intent.spawn_tick ||
        world.player_for_entity(intent.entity).has_value()) {
      continue;
    }
    const NaturalDespawnCategoryConfig& despawn_config =
        config_.despawning[spawn_category_index(natural->category)];
    if (!despawn_config.natural_despawn ||
        tick < natural->spawn_tick ||
        tick - natural->spawn_tick < despawn_config.min_lifetime_ticks ||
        (world.entity_is_persistent(intent.entity) && !natural->transient)) {
      continue;
    }
    const entity::EntityTransform* transform =
        world.find_entity_transform(intent.entity);
    if (transform == nullptr) {
      continue;
    }
    const f32 nearest_sq =
        nearest_player_distance_sq(transform->position, context.player_positions);
    const f32 hard = static_cast<f32>(despawn_config.hard_distance_blocks);
    const f32 soft = static_cast<f32>(despawn_config.soft_distance_blocks);
    bool should_despawn = false;
    if (!std::isfinite(nearest_sq) || nearest_sq >= hard * hard) {
      should_despawn = true;
    } else if (nearest_sq >= soft * soft) {
      should_despawn = ((tick + intent.entity.value) % 4U) == 0U;
    }
    if (!should_despawn) {
      continue;
    }
    const SpawnCategory category = natural->category;
    if (world.destroy_entity(intent.entity, EntityDespawnReason::Removed)) {
      ++stats.despawned;
      ++counters_.despawn_intents_committed;
      record_despawn_counter(counters_, category);
    }
  }

  if (stats.despawned > 0U) {
    world.flush_entity_despawns();
  }

  load_tracker_.refresh(world);
  SpawnLoadSnapshot live_load = load_tracker_.snapshot();
  u32 committed_groups = 0U;
  for (const NaturalSpawnIntent& intent : result.spawn_intents) {
    if (intent.positions.empty() ||
        committed_groups >= config_.max_successes_per_world_per_tick) {
      break;
    }

    const BlockPos root = intent.positions.front();
    const ChunkSectionCoord section = block_to_chunk(root);
    if (world.find_chunk(section) == nullptr) {
      ++counters_.rejected_chunk_not_loaded;
      continue;
    }
    if (!candidate_section_is_simulated(world, root, context)) {
      ++counters_.rejected_chunk_not_simulated;
      continue;
    }
    if (!has_world_budget(
            live_load,
            intent.entry.category,
            context.player_positions.size(),
            config_)) {
      ++counters_.rejected_world_cap;
      continue;
    }
    u32 global_remaining = remaining_natural_entity_budget(live_load, config_);
    if (global_remaining == 0U) {
      ++counters_.rejected_world_cap;
      break;
    }
    if (!load_tracker_.has_local_budget(
            world,
            root,
            intent.entry.category,
            intent.entry.entity_type,
            config_)) {
      ++counters_.rejected_local_density;
      continue;
    }

    std::vector<BlockPos> valid_positions;
    valid_positions.reserve(intent.positions.size());
    for (const BlockPos pos : intent.positions) {
      if (evaluator_.passes(
              world,
              intent.entry,
              pos,
              intent.entry.category,
              context)) {
        valid_positions.push_back(pos);
      }
    }
    if (valid_positions.size() < intent.entry.min_group) {
      ++counters_.rejected_group_failed;
      continue;
    }
    if (valid_positions.size() > global_remaining) {
      valid_positions.resize(global_remaining);
      if (valid_positions.size() < intent.entry.min_group) {
        ++counters_.rejected_group_failed;
        continue;
      }
    }

    const NaturalDespawnCategoryConfig& despawn_config =
        config_.despawning[spawn_category_index(intent.entry.category)];
    const bool transient = !despawn_config.save_transient;
    u32 spawned = 0U;
    for (const BlockPos pos : valid_positions) {
      EntityCreateInfo info;
      info.type_id = intent.entry.entity_type;
      info.type_name = intent.entry.entity_type_name;
      info.position = block_floor_center(pos);
      info.persistent = !transient && intent.entry.persistent_by_default;
      info.replicated = intent.entry.replicated_by_default;
      info.authored_components = intent.entry.authored_components;
      info.vehicle_definitions = vehicle_definitions;
      const EntityID entity = world.create_entity(std::move(info));
      if (!world.entity_exists(entity) ||
          !world.set_entity_natural_spawn(
              entity,
              {intent.entry.category, intent.entry.entity_type, tick, transient})) {
        ++counters_.rejected_entity_creation_failed;
        continue;
      }
      ++spawned;
    }

    if (spawned == 0U) {
      continue;
    }
    ++committed_groups;
    stats.spawned += spawned;
    counters_.successes_total += spawned;
    counters_.successes_last_interval += spawned;
    counters_.spawn_intents_committed += spawned;
    const usize category_index = spawn_category_index(intent.entry.category);
    live_load.natural_by_category[category_index] += spawned;
    live_load.total_by_category[category_index] += spawned;
  }

  load_tracker_.refresh(world);
  publish_snapshot_counts();

  stats.apply_duration_us =
      elapsed_microseconds(started, std::chrono::steady_clock::now());
  ++measured_spawn_apply_ticks_;
  total_spawn_apply_cost_us_ += stats.apply_duration_us;
  counters_.avg_spawn_apply_cost_us =
      total_spawn_apply_cost_us_ / measured_spawn_apply_ticks_;
  counters_.max_spawn_apply_cost_us =
      std::max(counters_.max_spawn_apply_cost_us, stats.apply_duration_us);
  return stats;
}

void NaturalSpawnSystem::record_job_submitted(u32 pending_jobs) noexcept {
  ++counters_.spawn_jobs_submitted;
  counters_.spawn_jobs_pending = pending_jobs;
}

void NaturalSpawnSystem::record_pending_job_count(u32 pending_jobs) noexcept {
  counters_.spawn_jobs_pending = pending_jobs;
}

void NaturalSpawnSystem::tick(
    WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    TickIndex tick,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) {
  if (!config_.enabled) {
    return;
  }

  const auto started = std::chrono::steady_clock::now();
  counters_.attempts_last_interval = 0U;
  counters_.successes_last_interval = 0U;
  std::optional<NaturalSpawnJobRequest> request = make_job_request(world, tick);
  if (request.has_value()) {
    NaturalSpawnJobResult result = build_job_result(std::move(request.value()));
    (void)apply_job_result(
        world,
        block_registry,
        entity_registry,
        result,
        tick,
        false,
        vehicle_definitions);
  }

  const auto finished = std::chrono::steady_clock::now();
  const u64 elapsed_us = elapsed_microseconds(started, finished);
  ++measured_spawn_ticks_;
  total_spawn_tick_cost_us_ += elapsed_us;
  counters_.avg_spawn_tick_cost_us = total_spawn_tick_cost_us_ / measured_spawn_ticks_;
  counters_.max_spawn_tick_cost_us = std::max(counters_.max_spawn_tick_cost_us, elapsed_us);
}

u32 NaturalSpawnSystem::attempt_spawn_near_player(
    WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const entity::EntityRegistry& entity_registry,
    const entity::VehicleDefinitionRegistry* vehicle_definitions,
    const SpawnEvaluationContext& context,
    const player::PlayerState& player,
    SpawnCategory category,
    TickIndex tick,
    HashMap<ChunkCoord, CandidateProfile>& profile_cache) {
  (void)entity_registry;
  ++counters_.attempts_total;
  ++counters_.attempts_last_interval;

  const i32 min_distance = static_cast<i32>(config_.min_distance_from_player_blocks);
  const i32 max_distance = static_cast<i32>(config_.max_distance_from_player_blocks);
  std::uniform_int_distribution<i32> offset_dist(-max_distance, max_distance);
  i32 dx = 0;
  i32 dz = 0;
  for (u32 tries = 0; tries < 8U; ++tries) {
    dx = offset_dist(rng_);
    dz = offset_dist(rng_);
    const i32 distance_sq = dx * dx + dz * dz;
    if (distance_sq >= min_distance * min_distance &&
        distance_sq <= max_distance * max_distance) {
      break;
    }
  }

  const i32 world_x = static_cast<i32>(std::floor(player.position.x)) + dx;
  const i32 world_z = static_cast<i32>(std::floor(player.position.z)) + dz;
  const ChunkCoord column{floor_div(world_x, ChunkSize), floor_div(world_z, ChunkSize)};
  const std::optional<CandidateProfile> profile = profile_for(world, column, profile_cache);
  if (!profile.has_value()) {
    ++counters_.rejected_no_generation_context;
    return 0U;
  }

  const BlockPos candidate = pick_candidate_near_player(player, category, profile->profile, column);
  if (!has_distance_to_player(
          candidate,
          context.player_positions,
          config_.min_distance_from_player_blocks,
          config_.max_distance_from_player_blocks)) {
    ++counters_.rejected_conditions;
    return 0U;
  }

  const ChunkSectionCoord section = block_to_chunk(candidate);
  if (world.find_chunk(section) == nullptr) {
    ++counters_.rejected_chunk_not_loaded;
    return 0U;
  }
  if (!world.is_chunk_active(section)) {
    ++counters_.rejected_chunk_not_simulated;
    return 0U;
  }

  const i32 local_x = floor_mod(candidate.value.x, ChunkSize);
  const i32 local_z = floor_mod(candidate.value.z, ChunkSize);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  const worldgen::ResolvedDimension* dimension =
      world.generation_config() == nullptr
          ? nullptr
          : world.generation_config()->resolved_dimension.get();
  if (dimension == nullptr || dimension->biomes.empty()) {
    ++counters_.rejected_no_generation_context;
    return 0U;
  }
  const usize biome_index =
      std::min(profile->profile.biome_by_column[column_index], dimension->biomes.size() - 1U);
  const std::string& biome_id = dimension->biomes[biome_index].raw.id;
  const BiomeSpawnTable* table = rules_.find_table(biome_id, category);
  if (table == nullptr || table->empty()) {
    ++counters_.rejected_no_biome_table;
    return 0U;
  }
  if (table->rate_scale < 1.0F) {
    std::uniform_real_distribution<f32> rate_dist(0.0F, 1.0F);
    if (rate_dist(rng_) >= table->rate_scale) {
      ++counters_.rejected_conditions;
      return 0U;
    }
  }

  std::uniform_int_distribution<u32> weight_dist(0U, table->total_weight - 1U);
  const SpawnEntry* entry = table->weighted_pick(weight_dist(rng_));
  if (entry == nullptr) {
    ++counters_.rejected_no_biome_table;
    return 0U;
  }
  if (!load_tracker_.has_world_budget(category, context.player_positions.size(), config_)) {
    ++counters_.rejected_world_cap;
    return 0U;
  }
  if (!load_tracker_.has_local_budget(
          world,
          candidate,
          category,
          entry->entity_type,
          config_,
          table->density_cap_scale)) {
    ++counters_.rejected_local_density;
    return 0U;
  }
  if (!evaluator_.passes(world, *entry, candidate, category, context)) {
    ++counters_.rejected_conditions;
    return 0U;
  }

  return spawn_group(world, *entry, vehicle_definitions, candidate, context, tick);
}

u32 NaturalSpawnSystem::spawn_group(
    WorldRuntime& world,
    const SpawnEntry& entry,
    const entity::VehicleDefinitionRegistry* vehicle_definitions,
    BlockPos root,
    const SpawnEvaluationContext& context,
    TickIndex tick) {
  std::uniform_int_distribution<u32> group_dist(entry.min_group, entry.max_group);
  const u32 desired_count = group_dist(rng_);
  std::vector<BlockPos> valid_positions;
  valid_positions.reserve(desired_count);

  for (u32 index = 0; index < desired_count; ++index) {
    const BlockPos pos = index == 0U ? root : pick_group_member(root);
    if (std::find(valid_positions.begin(), valid_positions.end(), pos) != valid_positions.end()) {
      continue;
    }
    if (evaluator_.passes(world, entry, pos, entry.category, context)) {
      valid_positions.push_back(pos);
    }
  }

  if (valid_positions.size() < entry.min_group) {
    ++counters_.rejected_group_failed;
    return 0U;
  }

  u32 spawned = 0U;
  const NaturalDespawnCategoryConfig& despawn_config =
      config_.despawning[spawn_category_index(entry.category)];
  const bool transient = !despawn_config.save_transient;
  for (const BlockPos pos : valid_positions) {
    EntityCreateInfo info;
    info.type_id = entry.entity_type;
    info.type_name = entry.entity_type_name;
    info.position = block_floor_center(pos);
    info.persistent = !transient && entry.persistent_by_default;
    info.replicated = entry.replicated_by_default;
    info.authored_components = entry.authored_components;
    info.vehicle_definitions = vehicle_definitions;
    const EntityID entity = world.create_entity(std::move(info));
    if (!world.entity_exists(entity) ||
        !world.set_entity_natural_spawn(
            entity,
            {entry.category, entry.entity_type, tick, transient})) {
      ++counters_.rejected_entity_creation_failed;
      continue;
    }
    ++spawned;
  }

  counters_.successes_total += spawned;
  counters_.successes_last_interval += spawned;
  return spawned;
}

std::optional<NaturalSpawnSystem::CandidateProfile> NaturalSpawnSystem::profile_for(
    const WorldRuntime& world,
    ChunkCoord coord,
    HashMap<ChunkCoord, CandidateProfile>& profile_cache) {
  if (auto found = profile_cache.find(coord); found != profile_cache.end()) {
    return found->second;
  }
  const WorldGenerationConfig* config = world.generation_config();
  if (config == nullptr || !config->resolved_dimension) {
    return std::nullopt;
  }
  CandidateProfile profile;
  profile.profile = worldgen::generate_terrain_profile(
      *config->resolved_dimension,
      world.generation_seed(),
      coord);
  profile_cache.emplace(coord, profile);
  return profile;
}

BlockPos NaturalSpawnSystem::pick_candidate_near_player(
    const player::PlayerState& player,
    SpawnCategory category,
    const worldgen::TerrainProfile& profile,
    ChunkCoord column) {
  (void)player;
  const i32 local_x = std::uniform_int_distribution<i32>(0, ChunkSize - 1)(rng_);
  const i32 local_z = std::uniform_int_distribution<i32>(0, ChunkSize - 1)(rng_);
  const usize column_index = voxel::local_column_index({local_x, local_z});
  i32 y = profile.surface_y_by_column[column_index] + 1;
  if (category == SpawnCategory::Water) {
    y = profile.surface_y_by_column[column_index];
  }
  return {{column.x * ChunkSize + local_x,
           y,
           column.z * ChunkSize + local_z}};
}

BlockPos NaturalSpawnSystem::pick_group_member(BlockPos root) {
  std::uniform_int_distribution<i32> offset(-4, 4);
  return {{root.value.x + offset(rng_),
           root.value.y,
           root.value.z + offset(rng_)}};
}

u32 NaturalSpawnSystem::despawn_natural_entities(
    WorldRuntime& world,
    const SpawnEvaluationContext& context,
    TickIndex tick) {
  u32 despawned = 0U;
  const std::vector<EntityID> ids = world.entity_ids();
  for (const EntityID id : ids) {
    const NaturalSpawnComponent* natural = world.find_entity_natural_spawn(id);
    if (natural == nullptr) {
      continue;
    }
    if (world.player_for_entity(id).has_value()) {
      continue;
    }
    const NaturalDespawnCategoryConfig& config =
        config_.despawning[spawn_category_index(natural->category)];
    if (!config.natural_despawn) {
      continue;
    }
    if (tick < natural->spawn_tick ||
        tick - natural->spawn_tick < config.min_lifetime_ticks) {
      continue;
    }
    if (world.entity_is_persistent(id) && !natural->transient) {
      continue;
    }
    const entity::EntityTransform* transform = world.find_entity_transform(id);
    if (transform == nullptr) {
      continue;
    }

    const f32 nearest_sq =
        nearest_player_distance_sq(transform->position, context.player_positions);
    const f32 hard = static_cast<f32>(config.hard_distance_blocks);
    const f32 soft = static_cast<f32>(config.soft_distance_blocks);
    bool should_despawn = false;
    if (!std::isfinite(nearest_sq) || nearest_sq >= hard * hard) {
      should_despawn = true;
    } else if (nearest_sq >= soft * soft) {
      should_despawn = ((tick + id.value) % 4U) == 0U;
    }
    if (!should_despawn) {
      continue;
    }
    if (world.destroy_entity(id, EntityDespawnReason::Removed)) {
      ++despawned;
      switch (natural->category) {
        case SpawnCategory::Hostile:
          ++counters_.despawned_natural_hostile;
          break;
        case SpawnCategory::Ambient:
          ++counters_.despawned_natural_ambient;
          break;
        case SpawnCategory::Water:
          ++counters_.despawned_natural_water;
          break;
        default:
          ++counters_.despawned_natural_other;
          break;
      }
    }
  }
  return despawned;
}

void NaturalSpawnSystem::publish_snapshot_counts() noexcept {
  counters_.natural_entities_by_category = load_tracker_.snapshot().natural_by_category;
  counters_.total_entities_by_category = load_tracker_.snapshot().total_by_category;
}

} // namespace arv::world
