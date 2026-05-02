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

struct EntityRegistryError {
  std::string message{};
};

struct EntityDefinition {
  std::string name{};
  std::string display_name{};
  std::string category{};
  std::string model_asset{};
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

struct NetworkEntity {
  EntityNetID net_id{};
};

struct DroppedItem {
  player::ItemStack stack{};
  Seconds pickup_delay_seconds{};
};

struct ChunkRelevance {
  ChunkCoord current_chunk{};
};

struct EntitySnapshotEntry {
  EntityID entity_id{};
  EntityNetID net_id{};
  EntityTypeID type_id{};
  std::string type_name{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  ChunkCoord chunk{};
  std::optional<PlayerID> owning_player{};
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
  ChunkCoord chunk{};
  player::ItemStack stack{};
  Seconds pickup_delay_seconds{};
};

} // namespace arv::entity
