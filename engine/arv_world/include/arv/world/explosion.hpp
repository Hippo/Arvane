#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_runtime.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world {

struct ExplosionError {
  std::string message{};
};

struct ExplosionProfile {
  std::string id{"arvane:tnt"};
  f32 radius{4.0F};
  f32 power{10.0F};
  bool block_damage_enabled{true};
  bool entity_damage_enabled{true};
  bool drops_enabled{true};
  bool fire_enabled{};
  bool future_sound_enabled{true};
  std::string future_sound_id{"arvane:sound/explosion"};
  u32 max_destroyed_blocks{512U};
  u32 max_rays{512U};
  u32 max_ray_steps{96U};
  u32 max_entity_exposure_samples{3U};
  u32 max_drops{128U};
  f32 impulse_strength{8.0F};
  f32 max_entity_damage{18.0F};
  f32 distance_falloff{0.85F};
  f32 resistance_scale{0.35F};
  f32 destroy_threshold{0.35F};
};

struct ExplosionOverrides {
  std::optional<f32> power{};
  std::optional<f32> radius{};
  std::optional<bool> block_damage_enabled{};
  std::optional<bool> entity_damage_enabled{};
  std::optional<bool> drops_enabled{};
  std::optional<bool> fire_enabled{};
  std::optional<bool> future_sound_enabled{};
  std::optional<std::string> future_sound_id{};
  std::optional<u32> max_destroyed_blocks{};
  std::optional<u32> max_rays{};
  std::optional<u32> max_drops{};
};

struct ExplosionRequest {
  WorldID world_id{};
  Vec3f origin{};
  std::string profile_id{"arvane:tnt"};
  std::optional<EntityID> source_entity{};
  u64 seed{};
  ExplosionOverrides overrides{};
};

struct ExplosionBlockHit {
  BlockPos pos{};
  ChunkSectionCoord section{};
  voxel::BlockVariantID old_state{};
  f32 remaining_power{};
  bool should_drop{};
};

struct ExplosionEntityHit {
  EntityID entity{};
  std::optional<PlayerID> player_id{};
  f32 distance{};
  f32 exposure{};
  f32 damage{};
  Vec3f impulse{};
  bool damage_applied{};
  bool killed{};
};

struct ExplosionResult {
  Vec3f origin{};
  std::string profile_id{};
  u64 seed{};
  f32 radius{};
  f32 power{};
  bool block_damage_enabled{};
  bool entity_damage_enabled{};
  bool drops_enabled{};
  bool fire_enabled{};
  bool future_sound_enabled{};
  std::string future_sound_id{};
  Aabb3f affected_bounds{};
  std::vector<ExplosionBlockHit> destroyed_blocks{};
  std::vector<ExplosionEntityHit> entity_hits{};
  std::vector<ChunkSectionCoord> touched_sections{};
  std::vector<ChunkSectionCoord> dirty_sections{};
  u32 rays_cast{};
  u32 ray_steps{};
  u32 max_drops{};
};

[[nodiscard]] std::span<const ExplosionProfile> builtin_explosion_profiles() noexcept;
[[nodiscard]] const ExplosionProfile* find_builtin_explosion_profile(
    std::string_view id) noexcept;
[[nodiscard]] Expected<ExplosionProfile, ExplosionError> resolve_explosion_profile(
    const ExplosionRequest& request);
[[nodiscard]] Expected<ExplosionResult, ExplosionError> simulate_explosion(
    const WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const ExplosionRequest& request);
[[nodiscard]] Expected<ExplosionResult, ExplosionError> execute_explosion(
    WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const ExplosionRequest& request);

} // namespace arv::world
