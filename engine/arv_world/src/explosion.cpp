#include "arv/world/explosion.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

namespace arv::world {
namespace {

constexpr f32 RayStep = 0.35F;
constexpr u32 RayShellSize = 12U;

[[nodiscard]] auto block_sort_key(BlockPos pos) noexcept {
  return std::tuple{pos.value.x, pos.value.y, pos.value.z};
}

[[nodiscard]] auto section_sort_key(ChunkSectionCoord coord) noexcept {
  return std::tuple{coord.value.x, coord.value.y, coord.value.z};
}

[[nodiscard]] u64 mix_u64(u64 value) noexcept {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] f32 unit_from_u64(u64 value) noexcept {
  constexpr f64 Denominator = static_cast<f64>(u64{1} << 53U);
  return static_cast<f32>(
      static_cast<f64>((value >> 11U) & ((u64{1} << 53U) - 1U)) / Denominator);
}

[[nodiscard]] u64 seed_for_request(
    const ExplosionRequest& request,
    const WorldRuntime& world) noexcept {
  if (request.seed != 0U) {
    return request.seed;
  }
  u64 seed = static_cast<u64>(world.tick_index()) ^ 0xd6e8feb86659fd93ULL;
  seed ^= mix_u64(static_cast<u64>(std::bit_cast<u32>(request.origin.x)));
  seed ^= mix_u64(static_cast<u64>(std::bit_cast<u32>(request.origin.y)) << 1U);
  seed ^= mix_u64(static_cast<u64>(std::bit_cast<u32>(request.origin.z)) << 2U);
  return seed == 0U ? 1U : seed;
}

[[nodiscard]] std::vector<Vec3f> build_ray_directions() {
  std::vector<Vec3f> directions;
  directions.reserve(RayShellSize * RayShellSize * 6U);
  const i32 max = static_cast<i32>(RayShellSize) - 1;
  for (i32 x = 0; x <= max; ++x) {
    for (i32 y = 0; y <= max; ++y) {
      for (i32 z = 0; z <= max; ++z) {
        const bool shell =
            x == 0 || y == 0 || z == 0 || x == max || y == max || z == max;
        if (!shell) {
          continue;
        }
        const Vec3f direction = normalize_or_zero({
            static_cast<f32>(x) / static_cast<f32>(max) * 2.0F - 1.0F,
            static_cast<f32>(y) / static_cast<f32>(max) * 2.0F - 1.0F,
            static_cast<f32>(z) / static_cast<f32>(max) * 2.0F - 1.0F});
        if (length(direction) > 0.0F) {
          directions.push_back(direction);
        }
      }
    }
  }
  return directions;
}

[[nodiscard]] std::span<const Vec3f> ray_directions() {
  static const std::vector<Vec3f> directions = build_ray_directions();
  return directions;
}

[[nodiscard]] Aabb3f affected_bounds(Vec3f origin, f32 radius) noexcept {
  const f32 padded = std::max(0.0F, radius) + 1.0F;
  return {
      {origin.x - padded, origin.y - padded, origin.z - padded},
      {origin.x + padded, origin.y + padded, origin.z + padded}};
}

struct DenseMaskBounds {
  Vec3i min{};
  Vec3i max{};
  i32 size_x{};
  i32 size_y{};
  i32 size_z{};
};

[[nodiscard]] DenseMaskBounds dense_bounds(Aabb3f bounds) noexcept {
  DenseMaskBounds out;
  out.min = {
      floor_to_i32(bounds.min.x),
      floor_to_i32(bounds.min.y),
      floor_to_i32(bounds.min.z)};
  out.max = {
      floor_to_i32(bounds.max.x),
      floor_to_i32(bounds.max.y),
      floor_to_i32(bounds.max.z)};
  out.size_x = std::max(0, out.max.x - out.min.x + 1);
  out.size_y = std::max(0, out.max.y - out.min.y + 1);
  out.size_z = std::max(0, out.max.z - out.min.z + 1);
  return out;
}

[[nodiscard]] bool contains(DenseMaskBounds bounds, BlockPos pos) noexcept {
  return pos.value.x >= bounds.min.x && pos.value.x <= bounds.max.x &&
         pos.value.y >= bounds.min.y && pos.value.y <= bounds.max.y &&
         pos.value.z >= bounds.min.z && pos.value.z <= bounds.max.z;
}

[[nodiscard]] usize dense_index(DenseMaskBounds bounds, BlockPos pos) noexcept {
  const usize x = static_cast<usize>(pos.value.x - bounds.min.x);
  const usize y = static_cast<usize>(pos.value.y - bounds.min.y);
  const usize z = static_cast<usize>(pos.value.z - bounds.min.z);
  return (y * static_cast<usize>(bounds.size_z) + z) *
             static_cast<usize>(bounds.size_x) +
         x;
}

[[nodiscard]] BlockPos block_pos_from_point(Vec3f point) noexcept {
  return {{floor_to_i32(point.x), floor_to_i32(point.y), floor_to_i32(point.z)}};
}

[[nodiscard]] bool variant_blocks_exposure(
    const voxel::BlockRegistry& registry,
    voxel::BlockVariantID variant_id) noexcept {
  if (registry.is_air(variant_id)) {
    return false;
  }
  const voxel::BlockVariantRecord* variant = registry.variant(variant_id);
  if (variant == nullptr) {
    return false;
  }
  return variant->blast.absorbs_blast ||
         variant->properties.solid ||
         variant->properties.collidable ||
         variant->properties.blocks_light;
}

[[nodiscard]] bool block_should_drop(
    const voxel::BlockRegistry& registry,
    voxel::BlockVariantID variant_id,
    const ExplosionProfile& profile) noexcept {
  if (!profile.drops_enabled) {
    return false;
  }
  const voxel::BlockVariantRecord* variant = registry.variant(variant_id);
  if (variant == nullptr || !variant->blast.drops_items) {
    return false;
  }
  const voxel::BlockRecord* block = registry.block(variant->block_id);
  return block != nullptr && block->drops.kind != voxel::BlockDropKind::None;
}

[[nodiscard]] Aabb3f entity_bounds(
    Vec3f position,
    const entity::EntityCollision* collision) noexcept {
  const Vec3f box =
      collision == nullptr
          ? Vec3f{0.6F, 1.8F, 0.6F}
          : Vec3f{
                std::max(0.0F, collision->box.x),
                std::max(0.0F, collision->box.y),
                std::max(0.0F, collision->box.z)};
  return {
      {position.x - box.x * 0.5F, position.y, position.z - box.z * 0.5F},
      {position.x + box.x * 0.5F, position.y + box.y, position.z + box.z * 0.5F}};
}

[[nodiscard]] Vec3f aabb_center(Aabb3f bounds) noexcept {
  return {
      (bounds.min.x + bounds.max.x) * 0.5F,
      (bounds.min.y + bounds.max.y) * 0.5F,
      (bounds.min.z + bounds.max.z) * 0.5F};
}

[[nodiscard]] std::vector<Vec3f> exposure_samples(
    Aabb3f bounds,
    u32 max_samples) {
  std::vector<Vec3f> samples;
  if (max_samples == 0U) {
    return samples;
  }
  samples.push_back(aabb_center(bounds));
  if (samples.size() < max_samples) {
    samples.push_back({
        (bounds.min.x + bounds.max.x) * 0.5F,
        bounds.max.y,
        (bounds.min.z + bounds.max.z) * 0.5F});
  }
  if (samples.size() < max_samples) {
    samples.push_back({
        (bounds.min.x + bounds.max.x) * 0.5F,
        bounds.min.y,
        (bounds.min.z + bounds.max.z) * 0.5F});
  }
  if (samples.size() < max_samples) {
    samples.push_back({bounds.min.x, aabb_center(bounds).y, bounds.min.z});
  }
  if (samples.size() < max_samples) {
    samples.push_back({bounds.max.x, aabb_center(bounds).y, bounds.max.z});
  }
  return samples;
}

[[nodiscard]] bool destroyed_mask_contains(
    DenseMaskBounds bounds,
    std::span<const u8> mask,
    BlockPos pos) noexcept {
  return contains(bounds, pos) && dense_index(bounds, pos) < mask.size() &&
         mask[dense_index(bounds, pos)] != 0U;
}

[[nodiscard]] bool has_line_of_sight(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    Vec3f origin,
    Vec3f target,
    DenseMaskBounds destroyed_bounds,
    std::span<const u8> destroyed_mask) noexcept {
  const Vec3f delta = target - origin;
  const f32 total = length(delta);
  if (total <= 0.001F) {
    return true;
  }
  const Vec3f direction = delta * (1.0F / total);
  const u32 steps = static_cast<u32>(std::ceil(total / 0.35F));
  for (u32 i = 1U; i < steps; ++i) {
    const Vec3f point = origin + direction * (static_cast<f32>(i) * 0.35F);
    const BlockPos pos = block_pos_from_point(point);
    if (destroyed_mask_contains(destroyed_bounds, destroyed_mask, pos)) {
      continue;
    }
    const voxel::BlockVariantID variant = world.get_block(pos);
    if (variant_blocks_exposure(registry, variant)) {
      return false;
    }
  }
  return true;
}

void sort_unique_sections(std::vector<ChunkSectionCoord>& sections) {
  std::sort(sections.begin(), sections.end(), [](ChunkSectionCoord lhs, ChunkSectionCoord rhs) {
    return section_sort_key(lhs) < section_sort_key(rhs);
  });
  sections.erase(
      std::unique(sections.begin(), sections.end()),
      sections.end());
}

[[nodiscard]] std::array<ExplosionProfile, 6> make_builtin_profiles() {
  ExplosionProfile tnt;
  tnt.id = "arvane:tnt";
  tnt.radius = 4.0F;
  tnt.power = 10.5F;
  tnt.max_destroyed_blocks = 768U;
  tnt.max_rays = 640U;
  tnt.max_ray_steps = 96U;
  tnt.impulse_strength = 9.0F;
  tnt.max_entity_damage = 20.0F;

  ExplosionProfile small = tnt;
  small.id = "arvane:small";
  small.radius = 2.5F;
  small.power = 6.0F;
  small.max_destroyed_blocks = 192U;
  small.max_rays = 384U;
  small.impulse_strength = 5.0F;
  small.max_entity_damage = 9.0F;

  ExplosionProfile large = tnt;
  large.id = "arvane:large";
  large.radius = 7.0F;
  large.power = 17.0F;
  large.max_destroyed_blocks = 2048U;
  large.max_rays = 1152U;
  large.max_ray_steps = 144U;
  large.max_drops = 256U;
  large.impulse_strength = 14.0F;
  large.max_entity_damage = 34.0F;

  ExplosionProfile shockwave = tnt;
  shockwave.id = "arvane:shockwave";
  shockwave.radius = 5.0F;
  shockwave.power = 8.0F;
  shockwave.block_damage_enabled = false;
  shockwave.drops_enabled = false;
  shockwave.impulse_strength = 16.0F;
  shockwave.max_entity_damage = 8.0F;

  ExplosionProfile vehicle = tnt;
  vehicle.id = "arvane:vehicle";
  vehicle.radius = 3.2F;
  vehicle.power = 8.0F;
  vehicle.max_destroyed_blocks = 384U;
  vehicle.max_rays = 512U;
  vehicle.max_drops = 64U;
  vehicle.impulse_strength = 8.0F;
  vehicle.max_entity_damage = 14.0F;

  ExplosionProfile debug = tnt;
  debug.id = "arvane:debug";
  debug.radius = 4.0F;
  debug.power = 10.0F;
  debug.max_destroyed_blocks = 1024U;
  debug.max_rays = 768U;
  debug.max_drops = 0U;
  debug.drops_enabled = false;

  return {tnt, small, large, shockwave, vehicle, debug};
}

} // namespace

std::span<const ExplosionProfile> builtin_explosion_profiles() noexcept {
  static const std::array<ExplosionProfile, 6> profiles = make_builtin_profiles();
  return profiles;
}

const ExplosionProfile* find_builtin_explosion_profile(std::string_view id) noexcept {
  for (const ExplosionProfile& profile : builtin_explosion_profiles()) {
    if (profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

Expected<ExplosionProfile, ExplosionError> resolve_explosion_profile(
    const ExplosionRequest& request) {
  const ExplosionProfile* builtin = find_builtin_explosion_profile(request.profile_id);
  if (builtin == nullptr) {
    return Expected<ExplosionProfile, ExplosionError>::failure(
        {"unknown explosion profile: " + request.profile_id});
  }
  ExplosionProfile profile = *builtin;
  const ExplosionOverrides& overrides = request.overrides;
  if (overrides.power.has_value()) {
    profile.power = overrides.power.value();
  }
  if (overrides.radius.has_value()) {
    profile.radius = overrides.radius.value();
  }
  if (overrides.block_damage_enabled.has_value()) {
    profile.block_damage_enabled = overrides.block_damage_enabled.value();
  }
  if (overrides.entity_damage_enabled.has_value()) {
    profile.entity_damage_enabled = overrides.entity_damage_enabled.value();
  }
  if (overrides.drops_enabled.has_value()) {
    profile.drops_enabled = overrides.drops_enabled.value();
  }
  if (overrides.fire_enabled.has_value()) {
    profile.fire_enabled = overrides.fire_enabled.value();
  }
  if (overrides.future_sound_enabled.has_value()) {
    profile.future_sound_enabled = overrides.future_sound_enabled.value();
  }
  if (overrides.future_sound_id.has_value()) {
    profile.future_sound_id = overrides.future_sound_id.value();
  }
  if (overrides.max_destroyed_blocks.has_value()) {
    profile.max_destroyed_blocks = overrides.max_destroyed_blocks.value();
  }
  if (overrides.max_rays.has_value()) {
    profile.max_rays = overrides.max_rays.value();
  }
  if (overrides.max_drops.has_value()) {
    profile.max_drops = overrides.max_drops.value();
  }

  if (!std::isfinite(profile.radius) || profile.radius <= 0.0F) {
    return Expected<ExplosionProfile, ExplosionError>::failure(
        {"explosion radius must be finite and positive"});
  }
  if (!std::isfinite(profile.power) || profile.power <= 0.0F) {
    return Expected<ExplosionProfile, ExplosionError>::failure(
        {"explosion power must be finite and positive"});
  }
  profile.max_rays = std::max<u32>(1U, profile.max_rays);
  profile.max_ray_steps = std::max<u32>(1U, profile.max_ray_steps);
  profile.max_entity_exposure_samples =
      std::max<u32>(1U, profile.max_entity_exposure_samples);
  profile.max_drops = profile.drops_enabled ? profile.max_drops : 0U;
  if (!profile.future_sound_enabled) {
    profile.future_sound_id.clear();
  }
  return Expected<ExplosionProfile, ExplosionError>::success(std::move(profile));
}

Expected<ExplosionResult, ExplosionError> simulate_explosion(
    const WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const ExplosionRequest& request) {
  if (request.world_id.value != 0U && request.world_id != world.id()) {
    return Expected<ExplosionResult, ExplosionError>::failure(
        {"explosion request world does not match runtime world"});
  }
  if (!finite(request.origin)) {
    return Expected<ExplosionResult, ExplosionError>::failure(
        {"explosion origin must be finite"});
  }
  auto resolved = resolve_explosion_profile(request);
  if (!resolved) {
    return Expected<ExplosionResult, ExplosionError>::failure(resolved.error());
  }
  const ExplosionProfile profile = std::move(resolved.value());
  const u64 seed = seed_for_request(request, world);

  ExplosionResult result;
  result.origin = request.origin;
  result.profile_id = profile.id;
  result.seed = seed;
  result.radius = profile.radius;
  result.power = profile.power;
  result.block_damage_enabled = profile.block_damage_enabled;
  result.entity_damage_enabled = profile.entity_damage_enabled;
  result.drops_enabled = profile.drops_enabled;
  result.fire_enabled = profile.fire_enabled;
  result.future_sound_enabled = profile.future_sound_enabled;
  result.future_sound_id = profile.future_sound_id;
  result.affected_bounds = affected_bounds(request.origin, profile.radius);
  result.max_drops = profile.max_drops;

  const DenseMaskBounds bounds = dense_bounds(result.affected_bounds);
  const u64 volume =
      static_cast<u64>(bounds.size_x) *
      static_cast<u64>(bounds.size_y) *
      static_cast<u64>(bounds.size_z);
  if (volume > 8'000'000ULL) {
    return Expected<ExplosionResult, ExplosionError>::failure(
        {"explosion affected area is too large"});
  }
  std::vector<u8> destroyed_mask(static_cast<usize>(volume), 0U);

  if (profile.block_damage_enabled && profile.max_destroyed_blocks > 0U) {
    const std::span<const Vec3f> directions = ray_directions();
    const u32 rays = std::min<u32>(
        profile.max_rays,
        static_cast<u32>(directions.size()));
    result.rays_cast = rays;
    for (u32 ray = 0U; ray < rays; ++ray) {
      const usize direction_index =
          (static_cast<usize>(ray) * directions.size()) / static_cast<usize>(rays);
      const Vec3f direction = directions[direction_index];
      const f32 random_scale =
          0.72F + unit_from_u64(mix_u64(seed + ray * 0x9e3779b97f4a7c15ULL)) * 0.56F;
      f32 remaining = profile.power * random_scale;
      for (u32 step = 0U; step < profile.max_ray_steps; ++step) {
        const f32 traveled = static_cast<f32>(step + 1U) * RayStep;
        if (traveled > profile.radius || remaining <= 0.0F) {
          break;
        }
        ++result.ray_steps;
        remaining -= RayStep * profile.distance_falloff;
        const BlockPos pos = block_pos_from_point(request.origin + direction * traveled);
        if (!contains(bounds, pos)) {
          continue;
        }
        const voxel::BlockVariantID variant_id = world.get_block(pos);
        if (block_registry.is_air(variant_id)) {
          continue;
        }
        const voxel::BlockVariantRecord* variant = block_registry.variant(variant_id);
        if (variant == nullptr) {
          continue;
        }
        remaining -= variant->blast.resistance * profile.resistance_scale;
        if (remaining <= 0.0F) {
          break;
        }
        if (variant->blast.destroyable_by_explosions &&
            remaining >= profile.destroy_threshold) {
          const usize index = dense_index(bounds, pos);
          if (index < destroyed_mask.size() && destroyed_mask[index] == 0U) {
            destroyed_mask[index] = 1U;
            result.destroyed_blocks.push_back({
                pos,
                block_to_chunk(pos),
                variant_id,
                remaining,
                block_should_drop(block_registry, variant_id, profile)});
            result.touched_sections.push_back(block_to_chunk(pos));
            if (result.destroyed_blocks.size() >= profile.max_destroyed_blocks) {
              break;
            }
          }
        }
        if (variant->blast.absorbs_blast) {
          break;
        }
      }
      if (result.destroyed_blocks.size() >= profile.max_destroyed_blocks) {
        break;
      }
    }
    std::sort(
        result.destroyed_blocks.begin(),
        result.destroyed_blocks.end(),
        [](const ExplosionBlockHit& lhs, const ExplosionBlockHit& rhs) {
          return block_sort_key(lhs.pos) < block_sort_key(rhs.pos);
        });
    sort_unique_sections(result.touched_sections);
  }

  if (profile.entity_damage_enabled) {
    for (const EntityID entity_id : world.entity_ids()) {
      if (request.source_entity.has_value() && entity_id == request.source_entity.value()) {
        continue;
      }
      const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
      if (transform == nullptr) {
        continue;
      }
      const entity::EntityCollision* collision = world.find_entity_collision(entity_id);
      const Aabb3f bounds_for_entity = entity_bounds(transform->position, collision);
      if (distance_squared_point_to_aabb(request.origin, bounds_for_entity) >
          profile.radius * profile.radius) {
        continue;
      }
      const Vec3f center = aabb_center(bounds_for_entity);
      const f32 entity_distance = distance(request.origin, center);
      if (entity_distance > profile.radius) {
        continue;
      }
      const std::vector<Vec3f> samples =
          exposure_samples(bounds_for_entity, profile.max_entity_exposure_samples);
      u32 visible = 0U;
      for (const Vec3f sample : samples) {
        if (has_line_of_sight(
                world,
                block_registry,
                request.origin,
                sample,
                bounds,
                destroyed_mask)) {
          ++visible;
        }
      }
      const f32 exposure =
          samples.empty()
              ? 0.0F
              : static_cast<f32>(visible) / static_cast<f32>(samples.size());
      if (exposure <= 0.0F) {
        continue;
      }
      const f32 distance_factor =
          std::clamp(1.0F - (entity_distance / profile.radius), 0.0F, 1.0F);
      const f32 effect = distance_factor * exposure;
      if (effect <= 0.0F) {
        continue;
      }
      Vec3f impulse_direction = normalize_or_zero(center - request.origin);
      if (length(impulse_direction) <= 0.001F) {
        impulse_direction = {0.0F, 1.0F, 0.0F};
      }
      const entity::KnockbackResistanceComponent* resistance =
          world.find_entity_knockback_resistance(entity_id);
      const f32 resistance_factor =
          resistance == nullptr ? 1.0F : std::clamp(1.0F - resistance->resistance, 0.0F, 1.0F);
      result.entity_hits.push_back({
          entity_id,
          world.player_for_entity(entity_id),
          entity_distance,
          exposure,
          profile.max_entity_damage * effect,
          impulse_direction * (profile.impulse_strength * effect * resistance_factor),
          false,
          false});
    }
  }

  return Expected<ExplosionResult, ExplosionError>::success(std::move(result));
}

Expected<ExplosionResult, ExplosionError> execute_explosion(
    WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const ExplosionRequest& request) {
  auto simulated = simulate_explosion(world, block_registry, request);
  if (!simulated) {
    return Expected<ExplosionResult, ExplosionError>::failure(simulated.error());
  }
  ExplosionResult result = std::move(simulated.value());

  std::vector<BlockMutation> mutations;
  mutations.reserve(result.destroyed_blocks.size());
  for (const ExplosionBlockHit& hit : result.destroyed_blocks) {
    mutations.push_back({hit.pos, block_registry.air_variant_id()});
  }
  const BlockMutationBatchResult mutation_result =
      world.apply_block_mutation_batch(mutations);
  result.touched_sections = mutation_result.touched_sections;
  result.dirty_sections = mutation_result.dirty_sections;

  for (ExplosionEntityHit& hit : result.entity_hits) {
    world::DamageResult damage;
    if (hit.damage > 0.0F) {
      damage = world.apply_damage({
          request.source_entity.value_or(EntityID{}),
          hit.entity,
          hit.damage,
          DamageCause::Explosion,
          true});
      hit.damage_applied = damage.applied;
      hit.killed = damage.killed;
    }
    if (hit.killed || !world.entity_exists(hit.entity)) {
      continue;
    }
    if (entity::EntityVelocity* velocity = world.find_entity_velocity(hit.entity);
        velocity != nullptr) {
      velocity->linear = velocity->linear + hit.impulse;
    }
    if (hit.player_id.has_value()) {
      if (player::PlayerState* player = world.find_player(hit.player_id.value());
          player != nullptr) {
        player->velocity = player->velocity + hit.impulse;
        player->grounded = false;
        player->knockback_control_suppression_ticks =
            std::max<u32>(player->knockback_control_suppression_ticks, 6U);
      }
    }
  }
  return Expected<ExplosionResult, ExplosionError>::success(std::move(result));
}

} // namespace arv::world
