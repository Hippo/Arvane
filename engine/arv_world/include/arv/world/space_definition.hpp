#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::world {

inline constexpr u32 SpaceDefinitionSchemaVersion = 1U;

enum class WorldDefinitionKind : u8 {
  Planet,
  Space,
};

enum class SpaceBodyType : u8 {
  Planet,
  Moon,
  Station,
  AsteroidField,
};

enum class SpaceBodyEntryMode : u8 {
  FixedSpawn,
  DirectionalSpawn,
  DockingSpawn,
  Disabled,
};

struct SpaceDefinitionDiagnostic {
  std::string resource_id{};
  std::string field_path{"root"};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

struct WorldRulesDefinition {
  bool allow_block_placement{true};
  bool allow_vehicle_launch{true};
  bool allow_space_entry{};
  bool unload_empty_chunks_aggressively{};
};

struct SpaceTransitionSpawnDefinition {
  std::string mode{"fixed_spawn"};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f rotation_degrees{};
  f32 velocity_scale{1.0F};
};

struct SpaceExitDefinition {
  bool enabled{};
  std::string target_space_world{};
  i32 min_y{};
  f32 min_vertical_speed{};
  std::vector<std::string> require_vehicle_tags{};
  bool consume_fuel{};
  SpaceTransitionSpawnDefinition transition_spawn{};
};

struct SpaceWorldDefinition {
  f32 coordinate_scale{1.0F};
  Vec3f default_spawn_position{};
  Vec3f default_spawn_rotation_degrees{};
  std::vector<std::string> bodies{};
};

struct WorldDefinition {
  std::string id{};
  u32 version{SpaceDefinitionSchemaVersion};
  WorldDefinitionKind kind{WorldDefinitionKind::Planet};
  std::string display_name{};
  std::string worldgen{};
  std::string environment{};
  WorldRulesDefinition rules{};
  std::optional<SpaceExitDefinition> space_exit{};
  std::optional<SpaceWorldDefinition> space{};
};

struct EnvironmentReentryDefinition {
  bool enabled{};
  std::string effect{};
  f32 drag_scale{1.0F};
  bool heat_damage{};
};

struct EnvironmentDefinition {
  std::string id{};
  u32 version{SpaceDefinitionSchemaVersion};
  f32 gravity_scale{1.0F};
  bool has_atmosphere{true};
  bool has_oxygen{true};
  std::string sky{};
  f32 ambient_light{0.15F};
  std::string fog{"none"};
  EnvironmentReentryDefinition reentry{};
};

struct SpaceBodyRenderDefinition {
  std::string model{};
  std::string texture{};
  bool atmosphere_halo{};
};

struct SpaceBodyEntryDefinition {
  bool enabled{};
  SpaceBodyEntryMode mode{SpaceBodyEntryMode::FixedSpawn};
  std::vector<std::string> require_vehicle_tags{};
  Vec3f spawn_position{};
  Vec3f spawn_velocity{};
  Vec3f spawn_rotation_degrees{};
  std::string effect{};
};

struct SpaceBodyDefinition {
  std::string id{};
  u32 version{SpaceDefinitionSchemaVersion};
  SpaceBodyType type{SpaceBodyType::Planet};
  std::string display_name{};
  std::string space_world{};
  std::string target_world{};
  Vec3f position{};
  f32 visual_radius{};
  f32 entry_radius{};
  SpaceBodyRenderDefinition render{};
  SpaceBodyEntryDefinition entry{};
};

class SpaceDefinitionRegistry {
public:
  [[nodiscard]] const WorldDefinition* find_world(
      std::string_view id) const noexcept;
  [[nodiscard]] const EnvironmentDefinition* find_environment(
      std::string_view id) const noexcept;
  [[nodiscard]] const SpaceBodyDefinition* find_space_body(
      std::string_view id) const noexcept;

  [[nodiscard]] usize world_count() const noexcept;
  [[nodiscard]] usize environment_count() const noexcept;
  [[nodiscard]] usize space_body_count() const noexcept;

  HashMap<std::string, WorldDefinition> worlds{};
  HashMap<std::string, EnvironmentDefinition> environments{};
  HashMap<std::string, SpaceBodyDefinition> space_bodies{};
};

using SpaceDefinitionRegistryResult =
    Expected<SpaceDefinitionRegistry, SpaceDefinitionDiagnostic>;

[[nodiscard]] std::optional<WorldDefinitionKind> world_definition_kind_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::optional<SpaceBodyType> space_body_type_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::optional<SpaceBodyEntryMode> space_body_entry_mode_from_string(
    std::string_view value) noexcept;

[[nodiscard]] SpaceDefinitionRegistryResult load_space_definition_registry_from_packs(
    const asset::PackManager& packs);

} // namespace arv::world
