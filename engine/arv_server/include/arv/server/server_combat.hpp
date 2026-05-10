#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_runtime.hpp"

#include <optional>

namespace arv::server {

struct CombatConfig {
  f32 default_attack_damage{1.0F};
  f32 default_attack_range{3.0F};
  f32 default_attack_cooldown_seconds{0.35F};
  f32 attack_validation_grace_blocks{3.0F};
  f32 max_attack_validation_range{6.0F};
  f32 existing_horizontal_retention{0.45F};
  f32 base_horizontal_knockback{8.0F};
  f32 sprint_horizontal_bonus{3.0F};
  f32 base_vertical_knockback{7.0F};
  f32 airborne_vertical_knockback{5.0F};
  f32 vertical_knockback_cap{9.0F};
  u32 knockback_control_suppression_ticks{2U};
  bool use_current_block_obstruction_check{};
};

struct PlayerCombatState {
  u32 last_attack_sequence{};
  bool has_last_attack_sequence{};
  TickIndex next_allowed_attack_tick{};
  bool sprint_knockback_ready{};
  bool was_sprinting_forward_last_tick{};
};

enum class AttackRejectReason : u8 {
  None,
  Duplicate,
  MissingPlayer,
  MissingTarget,
  DifferentWorld,
  AttackerDead,
  GamemodeDisallowsAttack,
  MissingHeldItem,
  MissingAttackData,
  Cooldown,
  OutOfRange,
  BlockObstructed,
  TargetNotDamageable,
  TargetDead,
};

struct AttackResult {
  bool accepted{};
  bool damage_applied{};
  bool knockback_applied{};
  std::optional<PlayerID> knockback_player_id{};
  Vec3f knockback_velocity{};
  u32 knockback_control_suppression_ticks{};
  AttackRejectReason reject_reason{AttackRejectReason::None};
};

[[nodiscard]] AttackResult validate_and_apply_entity_attack(
    world::WorldRuntime& world,
    player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    const CombatConfig& config,
    PlayerCombatState& combat_state,
    EntityNetID target_entity,
    u32 sequence,
    TickIndex current_tick,
    const voxel::BlockRegistry* block_registry = nullptr) noexcept;

} // namespace arv::server
