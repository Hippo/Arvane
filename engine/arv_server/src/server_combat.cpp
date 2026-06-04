#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/physics/collision.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace arv::server {
namespace {

constexpr std::string_view AttackingItemTag = "arvane:combat/attacking";

[[nodiscard]] AttackResult rejected(AttackRejectReason reason) noexcept {
  AttackResult result;
  result.reject_reason = reason;
  return result;
}

[[nodiscard]] std::optional<EntityID> entity_for_net_id(
    const world::WorldRuntime& world,
    EntityNetID net_id) {
  for (const EntityID entity_id : world.entity_ids()) {
    const std::optional<EntityNetID> candidate = world.network_id_for_entity(entity_id);
    if (candidate.has_value() && candidate.value() == net_id) {
      return entity_id;
    }
  }
  return std::nullopt;
}

[[nodiscard]] Vec3f entity_attack_box(
    const world::WorldRuntime& world,
    EntityID entity_id) noexcept {
  Vec3f box{1.0F, 1.0F, 1.0F};
  if (const entity::EntityCollision* collision = world.find_entity_collision(entity_id);
      collision != nullptr) {
    box = collision->box;
  } else if (world.player_for_entity(entity_id).has_value()) {
    box = {
        player::DefaultPlayerBodyWidth,
        player::DefaultPlayerBodyHeight,
        player::DefaultPlayerBodyWidth};
  }
  return box;
}

[[nodiscard]] Aabb3f entity_attack_bounds_at(
    const world::WorldRuntime& world,
    EntityID entity_id,
    Vec3f position) noexcept {
  const Vec3f box = entity_attack_box(world, entity_id);
  const f32 half_x = std::max(box.x, 0.0F) * 0.5F;
  const f32 half_z = std::max(box.z, 0.0F) * 0.5F;
  const f32 height = std::max(box.y, 0.0F);
  return {
      {position.x - half_x,
       position.y,
       position.z - half_z},
      {position.x + half_x,
       position.y + height,
       position.z + half_z}};
}

[[nodiscard]] std::optional<Vec3f> authoritative_entity_position(
    const world::WorldRuntime& world,
    EntityID entity_id) noexcept {
  if (const std::optional<PlayerID> player_id = world.player_for_entity(entity_id);
      player_id.has_value()) {
    if (const player::PlayerState* player_state = world.find_player(player_id.value());
        player_state != nullptr) {
      return player_state->position;
    }
  }

  const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
  if (transform == nullptr) {
    return std::nullopt;
  }
  return transform->position;
}

[[nodiscard]] Vec3f attack_eye_position(const player::PlayerState& player_state) noexcept {
  const f32 eye_height =
      player_state.swimming
          ? player::DefaultSwimmingEyeHeight
          : (player_state.sneaking ? player::DefaultSneakingEyeHeight
                                   : player::DefaultStandingEyeHeight);
  return {
      player_state.position.x,
      player_state.position.y + eye_height,
      player_state.position.z};
}

[[nodiscard]] bool health_is_dead(const entity::EntityHealth* health) noexcept {
  return health != nullptr && health->current <= 0.0F;
}

[[nodiscard]] player::ItemAttackData default_attack_profile(
    const CombatConfig& config) noexcept {
  return {
      std::isfinite(config.default_attack_damage) && config.default_attack_damage > 0.0F
          ? config.default_attack_damage
          : 1.0F,
      std::isfinite(config.default_attack_range) && config.default_attack_range > 0.0F
          ? config.default_attack_range
          : 3.0F,
      seconds_to_ticks_ceil(config.default_attack_cooldown_seconds),
      0.0F,
      0.0F,
      0.0F};
}

[[nodiscard]] player::ItemAttackData attack_profile_for_held_item(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    const CombatConfig& config) noexcept {
  const std::optional<player::ItemStack>& held =
      player_state.inventory.slot(player_state.selected_hotbar_slot);
  if (!held.has_value() || held->count == 0U) {
    return default_attack_profile(config);
  }

  const player::ItemRecord* held_item = item_registry.item(held->item);
  if (held_item == nullptr || !held_item->attack_data.has_value()) {
    return default_attack_profile(config);
  }
  return held_item->attack_data.value();
}

[[nodiscard]] const player::ItemRecord* held_item_record(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry) noexcept {
  const std::optional<player::ItemStack>& held =
      player_state.inventory.slot(player_state.selected_hotbar_slot);
  if (!held.has_value() || held->count == 0U) {
    return nullptr;
  }
  return item_registry.item(held->item);
}

[[nodiscard]] bool item_has_tag(
    const player::ItemRecord& item,
    std::string_view tag) noexcept {
  return std::any_of(
      item.tags.begin(),
      item.tags.end(),
      [tag](const std::string& candidate) { return candidate == tag; });
}

[[nodiscard]] u32 critical_hash(
    PlayerID player_id,
    EntityNetID target_entity,
    u32 sequence,
    TickIndex current_tick) noexcept {
  u32 value = player_id.value * 747796405U + target_entity.value * 2891336453U;
  value ^= sequence * 277803737U;
  value ^= current_tick * 1442695041U;
  value ^= value >> 16U;
  value *= 2246822519U;
  value ^= value >> 13U;
  value *= 3266489917U;
  value ^= value >> 16U;
  return value;
}

[[nodiscard]] u32 critical_roll_percent(
    PlayerID player_id,
    EntityNetID target_entity,
    u32 sequence,
    TickIndex current_tick) noexcept {
  return critical_hash(player_id, target_entity, sequence, current_tick) % 100U;
}

[[nodiscard]] bool should_apply_critical_hit(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    const CombatConfig& config,
    EntityNetID target_entity,
    u32 sequence,
    TickIndex current_tick) noexcept {
  const player::ItemRecord* held_item = held_item_record(player_state, item_registry);
  if (held_item == nullptr || !item_has_tag(*held_item, AttackingItemTag)) {
    return false;
  }
  const f32 chance = std::clamp(
      std::isfinite(config.critical_chance) ? config.critical_chance : 0.0F,
      0.0F,
      100.0F);
  if (chance <= 0.0F) {
    return false;
  }
  if (chance >= 100.0F) {
    return true;
  }
  return static_cast<f32>(
             critical_roll_percent(
                 player_state.player_id,
                 target_entity,
                 sequence,
                 current_tick)) < chance;
}

[[nodiscard]] f32 critical_damage_multiplier(const CombatConfig& config) noexcept {
  if (std::isfinite(config.critical_multiplier) && config.critical_multiplier > 0.0F) {
    return config.critical_multiplier;
  }
  return 1.0F;
}

[[nodiscard]] Vec3f closest_point_on_aabb(Vec3f point, Aabb3f bounds) noexcept {
  return {
      std::clamp(point.x, bounds.min.x, bounds.max.x),
      std::clamp(point.y, bounds.min.y, bounds.max.y),
      std::clamp(point.z, bounds.min.z, bounds.max.z)};
}

[[nodiscard]] bool has_block_obstruction(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    Vec3f origin,
    Aabb3f target_bounds,
    f32 target_distance_sq) noexcept {
  if (target_distance_sq <= 0.0F) {
    return false;
  }
  const Vec3f target_point = closest_point_on_aabb(origin, target_bounds);
  const f32 target_distance = std::sqrt(target_distance_sq);
  world::WorldCollisionQuery query(world, block_registry, true);
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      query,
      origin,
      target_point - origin,
      target_distance);
  return hit.has_value() && hit->distance < target_distance;
}

[[nodiscard]] Vec3f aabb_center(Aabb3f bounds) noexcept {
  return {
      (bounds.min.x + bounds.max.x) * 0.5F,
      (bounds.min.y + bounds.max.y) * 0.5F,
      (bounds.min.z + bounds.max.z) * 0.5F};
}

[[nodiscard]] f32 sane_non_negative(f32 value, f32 fallback) noexcept {
  if (std::isfinite(value) && value >= 0.0F) {
    return value;
  }
  return fallback;
}

[[nodiscard]] f32 attack_or_config_value(f32 attack_value, f32 config_value) noexcept {
  if (std::isfinite(attack_value) && attack_value > 0.0F) {
    return attack_value;
  }
  return sane_non_negative(config_value, 0.0F);
}

[[nodiscard]] f32 normalized_horizontal_retention(
    const CombatConfig& config) noexcept {
  if (!std::isfinite(config.existing_horizontal_retention)) {
    return 0.45F;
  }
  return std::clamp(config.existing_horizontal_retention, 0.0F, 1.0F);
}

[[nodiscard]] f32 knockback_resistance_factor(
    const world::WorldRuntime& world,
    EntityID target) noexcept {
  const entity::KnockbackResistanceComponent* resistance =
      world.find_entity_knockback_resistance(target);
  if (resistance == nullptr || !std::isfinite(resistance->resistance)) {
    return 1.0F;
  }
  return 1.0F - std::clamp(resistance->resistance, 0.0F, 1.0F);
}

struct TargetVelocity {
  Vec3f* velocity{};
  entity::EntityVelocity* entity_velocity{};
  player::PlayerState* player_state{};
  bool grounded{};
};

[[nodiscard]] bool entity_has_ground_support(
    world::WorldRuntime& world,
    EntityID entity_id,
    Aabb3f target_bounds,
    const voxel::BlockRegistry* block_registry) noexcept {
  if (block_registry == nullptr) {
    return false;
  }
  const entity::EntityCollision* collision = world.find_entity_collision(entity_id);
  if (collision == nullptr) {
    return false;
  }
  const world::WorldCollisionQuery query(world, *block_registry, true);
  const physics::KinematicConfig physics_config;
  return physics::has_support_below(
      query,
      target_bounds,
      physics_config.collision_epsilon * 2.0F);
}

[[nodiscard]] std::optional<TargetVelocity> target_velocity_for_knockback(
    world::WorldRuntime& world,
    EntityID target,
    Aabb3f target_bounds,
    const voxel::BlockRegistry* block_registry) noexcept {
  entity::EntityVelocity* entity_velocity = world.find_entity_velocity(target);
  if (const std::optional<PlayerID> player_id = world.player_for_entity(target);
      player_id.has_value()) {
    player::PlayerState* player_state = world.find_player(player_id.value());
    if (player_state == nullptr) {
      return std::nullopt;
    }
    return TargetVelocity{
        &player_state->velocity,
        entity_velocity,
        player_state,
        player_state->grounded};
  }

  if (entity_velocity == nullptr) {
    return std::nullopt;
  }
  return TargetVelocity{
      &entity_velocity->linear,
      entity_velocity,
      nullptr,
      entity_has_ground_support(world, target, target_bounds, block_registry)};
}

[[nodiscard]] Vec3f knockback_direction(
    const player::PlayerState& attacker_state,
    Aabb3f target_bounds) noexcept {
  const Vec3f attacker_eye = attack_eye_position(attacker_state);
  const Vec3f target_center = aabb_center(target_bounds);
  Vec3f direction{
      target_center.x - attacker_eye.x,
      0.0F,
      target_center.z - attacker_eye.z};
  direction = normalize_or_zero(direction);
  if (direction == Vec3f{}) {
    direction = normalize_or_zero(horizontal_forward(attacker_state.yaw_radians));
  }
  return direction;
}

void apply_attack_knockback(
    world::WorldRuntime& world,
    const player::PlayerState& attacker_state,
    EntityID target,
    Aabb3f target_bounds,
    const player::ItemAttackData& attack,
    const CombatConfig& config,
    PlayerCombatState& combat_state,
    AttackResult& result,
    const voxel::BlockRegistry* block_registry) noexcept {
  const Vec3f direction = knockback_direction(attacker_state, target_bounds);
  if (direction == Vec3f{}) {
    return;
  }

  std::optional<TargetVelocity> target_velocity =
      target_velocity_for_knockback(world, target, target_bounds, block_registry);
  if (!target_velocity.has_value() || target_velocity->velocity == nullptr) {
    return;
  }

  const f32 retention = normalized_horizontal_retention(config);
  const f32 base_horizontal =
      attack_or_config_value(attack.horizontal_knockback, config.base_horizontal_knockback);
  const bool use_sprint_bonus =
      combat_state.was_sprinting_forward_last_tick &&
      combat_state.sprint_knockback_ready;
  const f32 sprint_bonus =
      use_sprint_bonus
          ? attack_or_config_value(attack.sprint_knockback_bonus, config.sprint_horizontal_bonus)
          : 0.0F;
  const f32 resistance_factor = knockback_resistance_factor(world, target);
  const f32 horizontal_strength = (base_horizontal + sprint_bonus) * resistance_factor;

  const f32 grounded_vertical =
      attack_or_config_value(attack.vertical_knockback, config.base_vertical_knockback);
  const f32 airborne_vertical =
      attack_or_config_value(attack.vertical_knockback, config.airborne_vertical_knockback);
  const f32 vertical_cap =
      sane_non_negative(config.vertical_knockback_cap, std::max(grounded_vertical, airborne_vertical));
  const f32 vertical_strength =
      (target_velocity->grounded ? grounded_vertical : airborne_vertical) *
      resistance_factor;

  Vec3f& velocity = *target_velocity->velocity;
  const Vec3f current_horizontal{velocity.x, 0.0F, velocity.z};
  const Vec3f new_horizontal =
      current_horizontal * retention + direction * horizontal_strength;
  velocity.x = new_horizontal.x;
  velocity.z = new_horizontal.z;
  if (vertical_cap > 0.0F) {
    velocity.y = std::min(std::max(velocity.y, vertical_strength), vertical_cap);
  }

  if (target_velocity->player_state != nullptr) {
    target_velocity->player_state->knockback_control_suppression_ticks =
        std::max(
            target_velocity->player_state->knockback_control_suppression_ticks,
            config.knockback_control_suppression_ticks);
    result.knockback_player_id = target_velocity->player_state->player_id;
    result.knockback_velocity = velocity;
    result.knockback_control_suppression_ticks =
        target_velocity->player_state->knockback_control_suppression_ticks;
    if (velocity.y > 0.0F) {
      target_velocity->player_state->grounded = false;
    }
  }
  if (target_velocity->entity_velocity != nullptr) {
    target_velocity->entity_velocity->linear = velocity;
  }
  if (use_sprint_bonus) {
    combat_state.sprint_knockback_ready = false;
  }
  result.knockback_applied =
      horizontal_strength > 0.0F || (vertical_cap > 0.0F && vertical_strength > 0.0F);
}

} // namespace

AttackResult validate_and_apply_entity_attack(
    world::WorldRuntime& world,
    player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    const CombatConfig& config,
    PlayerCombatState& combat_state,
    EntityNetID target_entity,
    u32 sequence,
    TickIndex current_tick,
    const voxel::BlockRegistry* block_registry) noexcept {
  const std::optional<EntityID> attacker = world.entity_for_player(player_state.player_id);
  if (!attacker.has_value() || !world.entity_exists(attacker.value())) {
    return rejected(AttackRejectReason::MissingPlayer);
  }

  if (combat_state.has_last_attack_sequence &&
      combat_state.last_attack_sequence == sequence) {
    return rejected(AttackRejectReason::Duplicate);
  }

  const std::optional<EntityID> target = entity_for_net_id(world, target_entity);
  if (!target.has_value() || !world.entity_exists(target.value()) ||
      target.value() == attacker.value()) {
    return rejected(AttackRejectReason::MissingTarget);
  }

  if (player_state.world_id != world.id()) {
    return rejected(AttackRejectReason::DifferentWorld);
  }

  if (health_is_dead(world.find_entity_health(attacker.value()))) {
    return rejected(AttackRejectReason::AttackerDead);
  }

  if (!player_state.resolved_permissions.core.can_interact) {
    return rejected(AttackRejectReason::GamemodeDisallowsAttack);
  }

  if (current_tick < combat_state.next_allowed_attack_tick) {
    return rejected(AttackRejectReason::Cooldown);
  }

  const player::ItemAttackData attack =
      attack_profile_for_held_item(player_state, item_registry, config);
  const bool critical_hit = should_apply_critical_hit(
      player_state,
      item_registry,
      config,
      target_entity,
      sequence,
      current_tick);
  combat_state.last_attack_sequence = sequence;
  combat_state.has_last_attack_sequence = true;
  combat_state.next_allowed_attack_tick = current_tick + attack.cooldown_ticks;

  const f32 attack_range =
      std::isfinite(attack.attack_range) && attack.attack_range > 0.0F
          ? attack.attack_range
          : config.default_attack_range;
  const f32 validation_range = std::min(
      attack_range + std::max(config.attack_validation_grace_blocks, 0.0F),
      std::max(config.max_attack_validation_range, 0.0F));
  const f32 validation_range_sq = validation_range * validation_range;
  const Vec3f eye_position = attack_eye_position(player_state);
  const std::optional<Vec3f> target_position =
      authoritative_entity_position(world, target.value());
  if (!target_position.has_value()) {
    return rejected(AttackRejectReason::MissingTarget);
  }
  const Aabb3f target_bounds =
      entity_attack_bounds_at(world, target.value(), target_position.value());
  const f32 distance_sq = distance_squared_point_to_aabb(
      eye_position,
      target_bounds);
  if (distance_sq > validation_range_sq) {
    return rejected(AttackRejectReason::OutOfRange);
  }
  if (config.use_current_block_obstruction_check && block_registry != nullptr &&
      has_block_obstruction(
          world,
          *block_registry,
          eye_position,
          target_bounds,
          distance_sq)) {
    return rejected(AttackRejectReason::BlockObstructed);
  }

  const entity::EntityHealth* target_health = world.find_entity_health(target.value());
  if (target_health == nullptr) {
    return rejected(AttackRejectReason::TargetNotDamageable);
  }
  if (target_health->current <= 0.0F) {
    return rejected(AttackRejectReason::TargetDead);
  }

  const world::DamageResult damage = world.apply_damage({
      attacker.value(),
      target.value(),
      critical_hit ? attack.damage_amount * critical_damage_multiplier(config)
                   : attack.damage_amount,
      world::DamageCause::EntityAttack});
  AttackResult result;
  result.accepted = true;
  result.damage_applied = damage.applied;
  result.critical_hit = critical_hit && damage.applied;
  result.reject_reason = AttackRejectReason::None;
  if (damage.applied) {
    apply_attack_knockback(
        world,
        player_state,
        target.value(),
        target_bounds,
        attack,
        config,
        combat_state,
        result,
        block_registry);
  }
  return result;
}

} // namespace arv::server

namespace arv::server::detail {

void ServerRuntimeImpl::handle_attack_entity(
    net::ConnectionID connection_id,
    const protocol::ServerboundAttackEntity& attack) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated ||
      session->connection_state != net::ConnectionState::InGame) {
    return;
  }

  pending_entity_attacks_.push_back({session->session_id, attack});
}

void ServerRuntimeImpl::process_pending_entity_attacks() {
  if (pending_entity_attacks_.empty()) {
    return;
  }

  std::vector<PendingEntityAttack> attacks = std::move(pending_entity_attacks_);
  pending_entity_attacks_.clear();
  for (const PendingEntityAttack& pending : attacks) {
    const ServerSession* session = sessions_.find(pending.session_id);
    if (session == nullptr || !session->authenticated ||
        session->connection_state != net::ConnectionState::InGame) {
      continue;
    }

    player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr || item_registry_ == nullptr) {
      continue;
    }

    PlayerCombatState& combat_state = combat_states_[session->session_id];
    const AttackResult result = validate_and_apply_entity_attack(
        default_world_,
        *player_state,
        *item_registry_,
        combat_,
        combat_state,
        pending.attack.target_entity,
        pending.attack.sequence,
        tick_index_,
        block_registry_);
    if (result.accepted && !result.damage_applied) {
      core::info(
          "combat",
          "Accepted entity attack without damage player={} target={} seq={}",
          session->player_id.value,
          pending.attack.target_entity.value,
          pending.attack.sequence);
    }
    if (result.critical_hit) {
      const std::optional<EntityID> target_entity =
          entity_for_net_id(default_world_, pending.attack.target_entity);
      if (target_entity.has_value()) {
        const std::optional<Vec3f> target_position =
            authoritative_entity_position(default_world_, target_entity.value());
        if (target_position.has_value()) {
          const Aabb3f target_bounds =
              entity_attack_bounds_at(default_world_, target_entity.value(), target_position.value());
          protocol::ClientboundSpawnParticles particles;
          particles.world_id = player_state->world_id;
          particles.particle_type = "arvane:critical_hit";
          particles.position = aabb_center(target_bounds);
          particles.normal = {0.0F, 1.0F, 0.0F};
          particles.extents = {0.35F, 0.45F, 0.35F};
          particles.radius = 0.35F;
          particles.count = 10U;
          particles.seed = critical_hash(
              player_state->player_id,
              pending.attack.target_entity,
              pending.attack.sequence,
              tick_index_);
          particles.shape = protocol::ParticleSpawnShape::Sphere;
          sessions_.for_each_session([&](const ServerSession& recipient) {
            if (!recipient.authenticated ||
                recipient.connection_state != net::ConnectionState::InGame ||
                recipient.world_id != player_state->world_id) {
              return;
            }
            (void)send_protocol_message(
                *transport_,
                recipient.connection_id,
                protocol::MessageType::ClientboundSpawnParticles,
                protocol::ReliabilityClass::UnreliableEvent,
                protocol::NetworkLane::Gameplay,
                protocol::serialize_clientbound_spawn_particles(particles));
          });
        }
      }
    }
    if (result.knockback_player_id.has_value()) {
      const PlayerID target_player_id = result.knockback_player_id.value();
      const player::PlayerState* target_player =
          default_world_.find_player(target_player_id);
      if (target_player == nullptr) {
        continue;
      }
      sessions_.for_each_session([&](const ServerSession& target_session) {
        if (!target_session.authenticated ||
            target_session.connection_state != net::ConnectionState::InGame ||
            target_session.player_id != target_player_id) {
          return;
        }
        send_player_velocity(target_session, *target_player);
      });
    }
  }
}

} // namespace arv::server::detail
