#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::entity {

inline constexpr EntityTypeID PlayerEntityTypeID{1};
inline constexpr std::string_view PlayerEntityTypeName = "arvane:player";
inline constexpr std::string_view DroppedItemEntityTypeName = "arvane:dropped_item";
inline constexpr std::string_view TransformComponentID = "arvane:transform";
inline constexpr std::string_view VelocityComponentID = "arvane:velocity";
inline constexpr std::string_view CollisionComponentID = "arvane:collision";
inline constexpr std::string_view MovementComponentID = "arvane:movement";
inline constexpr std::string_view HealthComponentID = "arvane:health";
inline constexpr std::string_view BrainComponentID = "arvane:brain";
inline constexpr std::string_view AnimationComponentID = "arvane:animation";

struct EntityRegistryError {
  std::string message{};
};

struct AuthoredCollisionComponent {
  Vec3f box{1.0F, 1.0F, 1.0F};
};

struct AuthoredMovementComponent {
  f32 walk_speed{};
};

struct AuthoredHealthComponent {
  f32 max{1.0F};
};

struct AuthoredGoalDefinition {
  std::string type{};
  i32 priority{};
  f32 radius{};
  f32 speed_multiplier{1.0F};
  u32 duration_ticks{60};
  u32 cooldown_ticks{};
  f32 range{};
  std::string target{};
  std::string animation{};
};

struct AuthoredBrainComponent {
  std::string behavior{};
  u32 tick_interval{1};
  std::vector<AuthoredGoalDefinition> goals{};
};

struct AuthoredAnimationComponent {
  std::string initial{};
};

struct EntityComponentSet {
  bool transform{};
  bool velocity{};
  std::optional<AuthoredCollisionComponent> collision{};
  std::optional<AuthoredMovementComponent> movement{};
  std::optional<AuthoredHealthComponent> health{};
  std::optional<AuthoredBrainComponent> brain{};
  std::optional<AuthoredAnimationComponent> animation{};
  std::vector<std::string> component_ids{};
};

struct EntityDefinition {
  std::string name{};
  std::string display_name{};
  std::string category{};
  std::string model_asset{};
  std::string animation_asset{};
  EntityComponentSet components{};
  bool spawnable{true};
  bool persistent_by_default{};
  bool replicated_by_default{true};
};

struct EntityTypeRecord {
  EntityTypeID id{};
  std::string name{};
  std::string display_name{};
  std::string category{};
  std::string model_asset{};
  std::string animation_asset{};
  EntityComponentSet components{};
  bool spawnable{true};
  bool persistent_by_default{};
  bool replicated_by_default{true};
};

class EntityRegistry {
public:
  using Result = Expected<EntityRegistry, EntityRegistryError>;

  [[nodiscard]] static Result build(std::vector<EntityDefinition> definitions);
  [[nodiscard]] static Result build_with_builtins(std::vector<EntityDefinition> explicit_definitions);

  [[nodiscard]] const EntityTypeRecord* type(EntityTypeID id) const noexcept;
  [[nodiscard]] std::optional<EntityTypeID> find_type(std::string_view name) const;
  [[nodiscard]] usize type_count() const noexcept {
    return types_.size() > 0U ? types_.size() - 1U : 0U;
  }
  [[nodiscard]] std::span<const EntityTypeRecord> records() const noexcept {
    return types_.empty() ? std::span<const EntityTypeRecord>{}
                          : std::span<const EntityTypeRecord>{types_.data() + 1U, types_.size() - 1U};
  }

private:
  std::vector<EntityTypeRecord> types_{{}};
  HashMap<std::string, EntityTypeID> name_to_id_{};
};

[[nodiscard]] EntityDefinition make_player_entity_definition();
[[nodiscard]] std::string normalize_entity_model_asset(std::string_view model);
[[nodiscard]] std::string normalize_entity_animation_asset(std::string_view animation);
[[nodiscard]] Expected<std::vector<EntityDefinition>, EntityRegistryError> parse_entity_definitions_json(
    std::string_view text);

struct RuntimeEntity {
  EntityID entity_id{};
};

struct EntityTransform {
  Vec3f previous_position{};
  Vec3f position{};
  Quatf previous_rotation{};
  Quatf rotation{};
  Vec3f scale{1.0F, 1.0F, 1.0F};
};

struct EntityVelocity {
  Vec3f linear{};
};

struct EntityCollision {
  Vec3f box{1.0F, 1.0F, 1.0F};
};

struct EntityMovement {
  f32 walk_speed{};
};

struct EntityHealth {
  f32 max{1.0F};
  f32 current{1.0F};
};

struct MovementIntent {
  Vec3f desired_direction{};
  f32 speed_multiplier{1.0F};
  bool jump{};
  bool sprint{};
};

enum class GoalStopReason : u8 {
  Completed,
  Interrupted,
  Invalid,
};

struct GoalRuntimeState {
  u32 remaining_ticks{};
  TickIndex cooldown_until{};
  Vec3f movement_direction{};
};

struct GoalSlot {
  std::string type{};
  i32 priority{};
  f32 radius{};
  f32 speed_multiplier{1.0F};
  u32 duration_ticks{60};
  u32 cooldown_ticks{};
  f32 range{};
  std::string target{};
  std::string animation{};
  GoalRuntimeState state{};
};

struct GoalBlackboard {
  std::optional<EntityID> target_entity{};
  Vec3f home_position{};
  Vec3f wander_destination{};
};

struct BrainComponent {
  std::string behavior{};
  u32 tick_interval{1};
  TickIndex next_tick{};
  std::vector<GoalSlot> goals{};
  std::optional<usize> active_goal{};
  GoalBlackboard blackboard{};
};

struct EntityAnimationState {
  std::string name{};
  Seconds time_seconds{};
};

struct EntityType {
  EntityTypeID type_id{};
  std::string type_name{};
};

struct PersistentEntity {
  std::string uuid{};
  EntityTypeID type_id{};
  std::string type_name{};
};

struct PlayerEntity {
  PlayerID player_id{};
};

struct PlayerVisualPose {
  f32 body_yaw_radians{};
  f32 head_yaw_radians{};
  f32 head_pitch_radians{};
  bool initialized{};
};

struct NetworkEntity {
  EntityNetID net_id{};
};

struct DroppedItem {
  player::ItemStack stack{};
  Seconds pickup_delay_seconds{};
};

struct ChunkRelevance {
  ChunkSectionCoord current_chunk{};
};

struct EntitySnapshotEntry {
  EntityID entity_id{};
  EntityNetID net_id{};
  EntityTypeID type_id{};
  std::string type_name{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  ChunkSectionCoord chunk{};
  std::optional<PlayerID> owning_player{};
  std::string animation_name{};
  Seconds animation_time_seconds{};
  Vec3f head_rotation_degrees{};
};

struct PersistentEntityRecord {
  std::string uuid{};
  std::string type_name{};
  WorldID world_id{};
  Vec3f position{};
  Quatf rotation{};
  Vec3f velocity{};
  std::optional<player::ItemStack> dropped_item_stack{};
  Seconds dropped_item_pickup_delay_seconds{};
};

struct DroppedItemEntitySnapshot {
  EntityID entity_id{};
  EntityNetID net_id{};
  WorldID world_id{};
  Vec3f position{};
  Vec3f velocity{};
  ChunkSectionCoord chunk{};
  player::ItemStack stack{};
  Seconds pickup_delay_seconds{};
};

} // namespace arv::entity
