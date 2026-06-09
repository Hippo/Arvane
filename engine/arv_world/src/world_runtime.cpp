#include "world_runtime_internal.hpp"

#include "arv/core/string.hpp"
#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <thread>
#include <utility>

namespace arv::world {
namespace {

constexpr std::array<GameRuleDefinition, 7> DefaultGameRules{{
    {"doDaylightCycle", GameRuleValueType::Boolean, "Controls whether world time advances through the day-night cycle.", true, 0, {}},
    {"doWeatherCycle", GameRuleValueType::Boolean, "Controls whether automatic weather changes are allowed.", true, 0, {}},
    {"doMobSpawning", GameRuleValueType::Boolean, "Controls whether natural mob spawning is allowed.", true, 0, {}},
    {"keepInventory", GameRuleValueType::Boolean, "Controls whether players keep inventory after death.", false, 0, {}},
    {"mobGriefing", GameRuleValueType::Boolean, "Controls whether mobs may alter blocks or inventory.", true, 0, {}},
    {"fallDamage", GameRuleValueType::Boolean, "Controls whether players and entities take fall damage.", true, 0, {}},
    {"fireSpread", GameRuleValueType::Boolean, "Controls whether fire can spread between blocks.", true, 0, {}},
}};

[[nodiscard]] GameRuleState state_from_definition(const GameRuleDefinition& definition) {
  GameRuleState state;
  state.name = definition.name;
  state.type = definition.type;
  state.description = definition.description;
  state.bool_value = definition.default_bool;
  state.integer_value = definition.default_integer;
  state.string_value = definition.default_string;
  return state;
}

[[nodiscard]] auto find_rule(std::vector<GameRuleState>& rules, std::string_view name) noexcept {
  return std::find_if(rules.begin(), rules.end(), [&](const GameRuleState& rule) {
    return rule.name == name;
  });
}

[[nodiscard]] auto find_rule(
    const std::vector<GameRuleState>& rules,
    std::string_view name) noexcept {
  return std::find_if(rules.begin(), rules.end(), [&](const GameRuleState& rule) {
    return rule.name == name;
  });
}

void apply_rule_side_effects(WorldRuntime& world, const GameRuleState& rule) noexcept {
  if (rule.name == "doDaylightCycle" && rule.type == GameRuleValueType::Boolean) {
    world.set_daylight_cycle(rule.bool_value);
  }
}

} // namespace

std::string_view world_difficulty_name(WorldDifficulty difficulty) noexcept {
  switch (difficulty) {
    case WorldDifficulty::Peaceful:
      return "peaceful";
    case WorldDifficulty::Easy:
      return "easy";
    case WorldDifficulty::Normal:
      return "normal";
    case WorldDifficulty::Hard:
      return "hard";
  }
  return "normal";
}

std::optional<WorldDifficulty> world_difficulty_from_string(
    std::string_view text) noexcept {
  const std::string lowered = lower_ascii_copy(text);
  if (lowered == "peaceful") {
    return WorldDifficulty::Peaceful;
  }
  if (lowered == "easy") {
    return WorldDifficulty::Easy;
  }
  if (lowered == "normal") {
    return WorldDifficulty::Normal;
  }
  if (lowered == "hard") {
    return WorldDifficulty::Hard;
  }
  return std::nullopt;
}

std::span<const GameRuleDefinition> default_game_rule_definitions() noexcept {
  return DefaultGameRules;
}

std::vector<GameRuleState> make_default_game_rule_states() {
  std::vector<GameRuleState> rules;
  rules.reserve(DefaultGameRules.size());
  for (const GameRuleDefinition& definition : DefaultGameRules) {
    rules.push_back(state_from_definition(definition));
  }
  return rules;
}

std::string game_rule_value_to_string(const GameRuleState& rule) {
  switch (rule.type) {
    case GameRuleValueType::Boolean:
      return rule.bool_value ? "true" : "false";
    case GameRuleValueType::Integer:
      return std::to_string(rule.integer_value);
    case GameRuleValueType::String:
      return rule.string_value;
  }
  return {};
}

WorldRuntime::WorldRuntime(WorldID id) : impl_(std::make_unique<Impl>(id)) {
}

WorldRuntime::~WorldRuntime() = default;

WorldRuntime::WorldRuntime(WorldRuntime&&) noexcept = default;

WorldRuntime& WorldRuntime::operator=(WorldRuntime&&) noexcept = default;

WorldID WorldRuntime::id() const noexcept {
  return impl_->id;
}

void WorldRuntime::bind_owner_thread() noexcept {
  impl_->owner_thread_id = std::this_thread::get_id();
}

bool WorldRuntime::is_owner_thread() const noexcept {
  return impl_->owner_thread_id == std::this_thread::get_id();
}

void WorldRuntime::assert_owner_thread() const noexcept {
  assert(is_owner_thread() && "WorldRuntime mutation must run on the world owner thread");
}

TickIndex WorldRuntime::tick_index() const noexcept {
  return impl_->tick_index;
}

void WorldRuntime::set_tick_index(TickIndex tick_index) noexcept {
  assert_owner_thread();
  impl_->tick_index = tick_index;
  impl_->clock.world_tick = tick_index;
}

const WorldClock& WorldRuntime::clock() const noexcept {
  return impl_->clock;
}

void WorldRuntime::configure_clock(
    Seconds day_length_seconds,
    f32 start_day_fraction,
    TickRate fixed_tick_rate,
    bool daylight_cycle,
    f32 time_scale) noexcept {
  assert_owner_thread();
  const TickIndex world_tick = impl_->tick_index;
  impl_->clock = make_world_clock(
      fixed_tick_rate,
      day_length_seconds,
      start_day_fraction,
      daylight_cycle,
      time_scale);
  impl_->clock.world_tick = world_tick;
}

void WorldRuntime::set_clock(WorldClock clock, TickRate fixed_tick_rate) noexcept {
  assert_owner_thread();
  retime_world_clock(clock, fixed_tick_rate);
  impl_->clock = clock;
  impl_->tick_index = impl_->clock.world_tick;
}

void WorldRuntime::set_day_time_tick(u32 day_time_tick) noexcept {
  assert_owner_thread();
  world::set_day_time_tick(impl_->clock, day_time_tick);
}

void WorldRuntime::set_day_fraction(f32 day_fraction) noexcept {
  assert_owner_thread();
  world::set_day_fraction(impl_->clock, day_fraction);
}

void WorldRuntime::set_daylight_cycle(bool daylight_cycle) noexcept {
  assert_owner_thread();
  impl_->clock.daylight_cycle = daylight_cycle;
}

void WorldRuntime::set_time_scale(f32 time_scale) noexcept {
  assert_owner_thread();
  impl_->clock.time_scale = time_scale;
}

void WorldRuntime::set_day_length_seconds(
    Seconds day_length_seconds,
    TickRate fixed_tick_rate) noexcept {
  assert_owner_thread();
  const f32 fraction = day_fraction(impl_->clock);
  impl_->clock.day_length_seconds = day_length_seconds;
  retime_world_clock(impl_->clock, fixed_tick_rate);
  set_day_fraction(fraction);
}

void WorldRuntime::add_day_time_ticks(u32 delta_ticks) noexcept {
  assert_owner_thread();
  world::add_day_ticks(impl_->clock, static_cast<i64>(delta_ticks));
}

std::string WorldRuntime::weather_id() const {
  return impl_->weather;
}

bool WorldRuntime::set_weather_id(std::string weather_id) {
  assert_owner_thread();
  if (weather_id.empty()) {
    return false;
  }
  impl_->weather = std::move(weather_id);
  return true;
}

WorldDifficulty WorldRuntime::difficulty() const noexcept {
  return impl_->difficulty;
}

void WorldRuntime::set_difficulty(WorldDifficulty difficulty) noexcept {
  assert_owner_thread();
  impl_->difficulty = difficulty;
}

std::span<const GameRuleState> WorldRuntime::game_rules() const noexcept {
  return impl_->game_rules;
}

const GameRuleState* WorldRuntime::game_rule(std::string_view name) const noexcept {
  const auto found = find_rule(impl_->game_rules, name);
  return found == impl_->game_rules.end() ? nullptr : &*found;
}

bool WorldRuntime::set_game_rule_bool(std::string_view name, bool value) {
  assert_owner_thread();
  auto found = find_rule(impl_->game_rules, name);
  if (found == impl_->game_rules.end() || found->type != GameRuleValueType::Boolean) {
    return false;
  }
  found->bool_value = value;
  apply_rule_side_effects(*this, *found);
  return true;
}

bool WorldRuntime::set_game_rule_integer(std::string_view name, i64 value) {
  assert_owner_thread();
  auto found = find_rule(impl_->game_rules, name);
  if (found == impl_->game_rules.end() || found->type != GameRuleValueType::Integer) {
    return false;
  }
  found->integer_value = value;
  return true;
}

bool WorldRuntime::set_game_rule_string(std::string_view name, std::string value) {
  assert_owner_thread();
  auto found = find_rule(impl_->game_rules, name);
  if (found == impl_->game_rules.end() || found->type != GameRuleValueType::String) {
    return false;
  }
  found->string_value = std::move(value);
  return true;
}

bool WorldRuntime::apply_status_effect(
    EntityID entity_id,
    std::string effect_id,
    u32 duration_ticks,
    u32 amplifier) {
  assert_owner_thread();
  if (!entity_exists(entity_id) || effect_id.empty() || duration_ticks == 0U ||
      amplifier == 0U) {
    return false;
  }

  std::vector<StatusEffectInstance>& effects = impl_->status_effects[entity_id];
  auto found = std::find_if(effects.begin(), effects.end(), [&](const StatusEffectInstance& effect) {
    return effect.effect_id == effect_id;
  });
  if (found == effects.end()) {
    effects.push_back({std::move(effect_id), duration_ticks, amplifier});
    return true;
  }
  found->remaining_ticks = duration_ticks;
  found->amplifier = amplifier;
  return true;
}

u32 WorldRuntime::clear_status_effects(
    EntityID entity_id,
    std::optional<std::string_view> effect_id) {
  assert_owner_thread();
  auto found = impl_->status_effects.find(entity_id);
  if (found == impl_->status_effects.end()) {
    return 0U;
  }
  if (!effect_id.has_value()) {
    const u32 removed = static_cast<u32>(found->second.size());
    impl_->status_effects.erase(found);
    return removed;
  }

  const usize before = found->second.size();
  found->second.erase(
      std::remove_if(
          found->second.begin(),
          found->second.end(),
          [&](const StatusEffectInstance& effect) {
            return effect.effect_id == effect_id.value();
          }),
      found->second.end());
  const u32 removed = static_cast<u32>(before - found->second.size());
  if (found->second.empty()) {
    impl_->status_effects.erase(found);
  }
  return removed;
}

std::vector<StatusEffectInstance> WorldRuntime::status_effects(EntityID entity_id) const {
  const auto found = impl_->status_effects.find(entity_id);
  return found == impl_->status_effects.end()
             ? std::vector<StatusEffectInstance>{}
             : found->second;
}

arv::Location WorldRuntime::spawn_point() const noexcept {
  return impl_->spawn_point;
}

void WorldRuntime::set_spawn_point(arv::Location spawn) noexcept {
  assert_owner_thread();
  if (!spawn.world.has_value()) {
    spawn.world = impl_->id;
  }
  impl_->spawn_point = spawn;
}

void WorldRuntime::set_generation_context(u64 seed, WorldGenerationConfig config) {
  assert_owner_thread();
  configure_height(world_height_settings_from_generation_config(config));
  for (auto& [coord, chunk] : impl_->chunks.chunks()) {
    (void)coord;
    chunk.clear_generation_surface_profile();
  }
  impl_->generation_seed = seed;
  impl_->generation_config = std::move(config);
}

u64 WorldRuntime::generation_seed() const noexcept {
  return impl_->generation_seed;
}

const WorldGenerationConfig* WorldRuntime::generation_config() const noexcept {
  return impl_->generation_config.has_value() ? &impl_->generation_config.value() : nullptr;
}

void WorldRuntime::clear_generation_context() noexcept {
  assert_owner_thread();
  for (auto& [coord, chunk] : impl_->chunks.chunks()) {
    (void)coord;
    chunk.clear_generation_surface_profile();
  }
  impl_->generation_config.reset();
  impl_->generation_seed = 0U;
}

WorldTickStats WorldRuntime::tick(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& fluid_definition,
    const EntityGoalRegistry* goal_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry) {
  assert_owner_thread();
  ARV_PROFILE_ZONE("Simulation tick");
  tick_world_clock(impl_->clock);
  impl_->tick_index = impl_->clock.world_tick;
  for (auto it = impl_->status_effects.begin(); it != impl_->status_effects.end();) {
    for (StatusEffectInstance& effect : it->second) {
      if (effect.remaining_ticks > 0U) {
        --effect.remaining_ticks;
      }
    }
    it->second.erase(
        std::remove_if(
            it->second.begin(),
            it->second.end(),
            [](const StatusEffectInstance& effect) {
              return effect.remaining_ticks == 0U;
            }),
        it->second.end());
    if (it->second.empty() || !entity_exists(it->first)) {
      it = impl_->status_effects.erase(it);
    } else {
      ++it;
    }
  }

  WorldTickStats stats;
  stats.entity_updates_processed =
      impl_->entity_simulation.tick_entities(
          *this,
          impl_->entities,
          impl_->regions,
          &registry,
          goal_registry,
          vehicle_registry);
  stats.scheduled_ticks_processed =
      impl_->tick_queues.process_due_scheduled_ticks(impl_->tick_index);

  const WorldTickPolicy policy = impl_->tick_queues.policy();
  if (policy.block_update_interval == 0U ||
      impl_->tick_index % policy.block_update_interval == 0U) {
    stats.block_updates_processed =
        impl_->tick_queues.drain_block_updates(policy.block_update_budget);
  }

  if (policy.fluid_interval == 0U ||
      impl_->tick_index % policy.fluid_interval == 0U) {
    stats.fluid_updates_processed =
        impl_->fluid.process_updates(*this, registry, fluid_definition, policy.fluid_budget);
  }

  return stats;
}

} // namespace arv::world
