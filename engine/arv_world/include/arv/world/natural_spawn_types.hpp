#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace arv::world {

enum class SpawnCategory : u8 {
  Passive,
  Hostile,
  Ambient,
  Water,
  Underground,
  Special,
  Count,
};

inline constexpr usize SpawnCategoryCount = static_cast<usize>(SpawnCategory::Count);

inline constexpr std::array<SpawnCategory, SpawnCategoryCount> AllSpawnCategories{{
    SpawnCategory::Passive,
    SpawnCategory::Hostile,
    SpawnCategory::Ambient,
    SpawnCategory::Water,
    SpawnCategory::Underground,
    SpawnCategory::Special,
}};

[[nodiscard]] constexpr usize spawn_category_index(SpawnCategory category) noexcept {
  return static_cast<usize>(category);
}

[[nodiscard]] std::string_view spawn_category_name(SpawnCategory category) noexcept;
[[nodiscard]] std::optional<SpawnCategory> spawn_category_from_string(
    std::string_view name) noexcept;

struct NaturalSpawnComponent {
  SpawnCategory category{SpawnCategory::Passive};
  EntityTypeID source_entity_type{};
  TickIndex spawn_tick{};
  bool transient{true};
};

enum class TimePredicate : u8 {
  Day,
  Night,
};

[[nodiscard]] std::optional<TimePredicate> time_predicate_from_string(
    std::string_view name) noexcept;
[[nodiscard]] std::string_view time_predicate_name(TimePredicate predicate) noexcept;

struct SpawnCategoryCapConfig {
  u32 per_player{};
  u32 per_world_soft{};
  u32 local_max{};
};

struct NaturalDespawnCategoryConfig {
  bool natural_despawn{};
  u32 soft_distance_blocks{};
  u32 hard_distance_blocks{};
  TickIndex min_lifetime_ticks{};
  bool save_transient{};
};

struct NaturalSpawningConfig {
  bool enabled{true};
  TickIndex interval_ticks{10};
  u32 min_distance_from_player_blocks{24};
  u32 max_distance_from_player_blocks{96};
  u32 attempts_per_player_per_category{24};
  u32 max_successes_per_player_per_tick{6};
  u32 max_successes_per_world_per_tick{48};
  u32 max_natural_entities{100};
  u32 local_density_radius_blocks{32};
  std::array<SpawnCategoryCapConfig, SpawnCategoryCount> caps{{
      {48, 1024, 24},
      {48, 1024, 24},
      {32, 768, 32},
      {32, 768, 24},
      {32, 768, 24},
      {8, 128, 8},
  }};
  std::array<NaturalDespawnCategoryConfig, SpawnCategoryCount> despawning{{
      {false, 96, 160, 600, true},
      {true, 96, 160, 600, false},
      {true, 64, 128, 200, false},
      {true, 96, 160, 400, false},
      {true, 96, 160, 600, false},
      {true, 96, 160, 600, false},
  }};
};

} // namespace arv::world
