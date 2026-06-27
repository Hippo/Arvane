#include "world_runtime_internal.hpp"

#include "arv/physics/kinematics.hpp"
#include "arv/physics/player_movement.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace arv::world::detail {
namespace {

template<typename Component, typename Registry>
[[nodiscard]] auto find_component(
    Registry& registry,
    entt::entity handle) noexcept -> decltype(registry.template try_get<Component>(handle)) {
  return handle == entt::null || !registry.valid(handle)
             ? nullptr
             : registry.template try_get<Component>(handle);
}

[[nodiscard]] Aabb3f entity_body_bounds(Vec3f position, Vec3f box) noexcept {
  const Vec3f size{
      std::max(0.0F, box.x),
      std::max(0.0F, box.y),
      std::max(0.0F, box.z)};
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  return {
      {position.x - half_x, position.y, position.z - half_z},
      {position.x + half_x, position.y + size.y, position.z + half_z}};
}

[[nodiscard]] auto find_seat(
    entity::RideableComponent& rideable,
    std::string_view seat_id) noexcept {
  return std::find_if(
      rideable.seats.begin(),
      rideable.seats.end(),
      [&](const entity::Seat& seat) {
        return seat.id == seat_id;
      });
}

[[nodiscard]] auto find_seat(
    const entity::RideableComponent& rideable,
    std::string_view seat_id) noexcept {
  return std::find_if(
      rideable.seats.begin(),
      rideable.seats.end(),
      [&](const entity::Seat& seat) {
        return seat.id == seat_id;
      });
}

[[nodiscard]] auto first_available_controlling_seat(
    entity::RideableComponent& rideable) noexcept {
  return std::find_if(
      rideable.seats.begin(),
      rideable.seats.end(),
      [](const entity::Seat& seat) {
        return seat.controls_vehicle && seat.occupant.value == 0U;
      });
}

[[nodiscard]] auto first_available_seat(entity::RideableComponent& rideable) noexcept {
  return std::find_if(
      rideable.seats.begin(),
      rideable.seats.end(),
      [](const entity::Seat& seat) {
        return seat.occupant.value == 0U;
      });
}

} // namespace

WorldEntityStore::WorldEntityStore(WorldID world_id) : world_id_(world_id) {
}

EntityID WorldEntityStore::create_entity(EntityCreateInfo info) {
  if (info.authored_components.has_value() &&
      !authored_vehicle_components_resolved(
          info.authored_components.value(),
          info.vehicle_definitions)) {
    return {};
  }

  if (info.type_name.empty()) {
    info.type_name = "arvane:unknown";
  }
  if (info.type_id.value == 0U && info.type_name == entity::PlayerEntityTypeName) {
    info.type_id = entity::PlayerEntityTypeID;
  }

  const EntityID entity_id{next_entity_id_++};
  const entt::entity handle = registry_.create();
  handles_[entity_id] = handle;

  registry_.emplace<entity::RuntimeEntity>(handle, entity_id);
  registry_.emplace<entity::EntityType>(handle, info.type_id, info.type_name);
  registry_.emplace<entity::EntityTransform>(
      handle,
      info.position,
      info.position,
      info.rotation,
      info.rotation,
      Vec3f{1.0F, 1.0F, 1.0F});
  registry_.emplace<entity::EntityVelocity>(handle, info.velocity);
  registry_.emplace<entity::ChunkRelevance>(handle, chunk_for_position(info.position));

  if (info.replicated) {
    registry_.emplace<entity::NetworkEntity>(handle, EntityNetID{next_entity_net_id_++});
  }

  if (info.persistent) {
    if (info.persistent_uuid.empty()) {
      // TODO(arvane): Replace this deterministic placeholder with real UUID
      // allocation once the global entity/profile identity policy is specified.
      info.persistent_uuid =
          "entity:" + std::to_string(world_id_.value) + ":" + std::to_string(entity_id.value);
    }
    registry_.emplace<entity::PersistentEntity>(
        handle,
        std::move(info.persistent_uuid),
        info.type_id,
        info.type_name);
  }

  if (info.owning_player.has_value()) {
    registry_.emplace<entity::PlayerEntity>(handle, info.owning_player.value());
    const f32 initial_body_yaw =
        info.player_state.has_value() ? info.player_state->yaw_radians : 0.0F;
    registry_.emplace<entity::PlayerVisualPose>(
        handle,
        initial_body_yaw,
        0.0F,
        info.player_state.has_value() ? info.player_state->pitch_radians : 0.0F,
        true);
    auto& transform = registry_.get<entity::EntityTransform>(handle);
    transform.previous_rotation = model_forward_yaw_rotation(initial_body_yaw);
    transform.rotation = transform.previous_rotation;
    player_entities_[info.owning_player.value()] = entity_id;

    player::PlayerState player_state =
        info.player_state.has_value() ? std::move(info.player_state.value()) : player::PlayerState{};
    player_state.player_id = info.owning_player.value();
    player_state.world_id = world_id_;
    player_state.position = info.position;
    player_state.velocity = info.velocity;
    registry_.emplace<player::PlayerState>(handle, std::move(player_state));
  }

  if (info.dropped_item_stack.has_value()) {
    registry_.emplace<entity::DroppedItem>(
        handle,
        info.dropped_item_stack.value(),
        info.dropped_item_pickup_delay_seconds);
  }

  if (info.authored_components.has_value()) {
    apply_authored_components(
        registry_,
        handle,
        info.authored_components.value(),
        info.vehicle_definitions);
  }

  return entity_id;
}

EntityID WorldEntityStore::create_dropped_item(
    player::ItemStack stack,
    Vec3f position,
    Vec3f velocity,
    Seconds pickup_delay_seconds,
    bool persistent) {
  EntityCreateInfo info;
  info.type_name = std::string{entity::DroppedItemEntityTypeName};
  info.position = position;
  info.velocity = velocity;
  info.persistent = persistent;
  info.replicated = true;
  player::normalize_item_stack_data(stack);
  info.dropped_item_stack = stack;
  info.dropped_item_pickup_delay_seconds = std::max<Seconds>(pickup_delay_seconds, 0.0F);
  return create_entity(std::move(info));
}

bool WorldEntityStore::destroy_entity(EntityID entity_id, EntityDespawnReason reason) {
  (void)reason;
  if (!exists(entity_id)) {
    return false;
  }
  if (std::find(pending_despawns_.begin(), pending_despawns_.end(), entity_id) ==
      pending_despawns_.end()) {
    pending_despawns_.push_back(entity_id);
  }
  return true;
}

void WorldEntityStore::flush_despawns(WorldRegionRuntime& regions) {
  auto record_ride_ended = [&](EntityID passenger, EntityID rideable) {
    const bool duplicate = std::any_of(
        ride_ended_events_.begin(),
        ride_ended_events_.end(),
        [&](const RideEndedEvent& event) {
          return event.passenger == passenger && event.rideable == rideable;
        });
    if (duplicate) {
      return;
    }
    const entity::EntityTransform* passenger_transform = find_transform(passenger);
    ride_ended_events_.push_back({passenger,
                                  rideable,
                                  network_id_for_entity(passenger),
                                  network_id_for_entity(rideable),
                                  passenger_transform == nullptr ? Vec3f{} : passenger_transform->position});
  };

  for (const EntityID entity_id : pending_despawns_) {
    const entt::entity entity_handle = handle(entity_id);
    if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
      continue;
    }
    if (const auto* player_entity = registry_.try_get<entity::PlayerEntity>(entity_handle);
        player_entity != nullptr) {
      player_entities_.erase(player_entity->player_id);
      regions.erase_player(player_entity->player_id);
    }
    if (const auto* attachment = registry_.try_get<entity::AttachedToComponent>(entity_handle);
        attachment != nullptr) {
      record_ride_ended(entity_id, attachment->parent);
    }
    (void)dismount_entity(entity_id);
    if (const auto* rideable = registry_.try_get<entity::RideableComponent>(entity_handle);
        rideable != nullptr) {
      std::vector<EntityID> passengers;
      passengers.reserve(rideable->seats.size());
      for (const entity::Seat& seat : rideable->seats) {
        if (seat.occupant.value != 0U) {
          passengers.push_back(seat.occupant);
        }
      }
      for (const EntityID passenger : passengers) {
        record_ride_ended(passenger, entity_id);
        (void)dismount_entity(passenger);
      }
    }
    handles_.erase(entity_id);
    registry_.destroy(entity_handle);
  }
  pending_despawns_.clear();
}

std::vector<RideEndedEvent> WorldEntityStore::drain_ride_ended_events() {
  std::vector<RideEndedEvent> events = std::move(ride_ended_events_);
  ride_ended_events_.clear();
  return events;
}

bool WorldEntityStore::exists(EntityID entity_id) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  return entity_handle != entt::null && registry_.valid(entity_handle);
}

usize WorldEntityStore::count() const noexcept {
  return handles_.size();
}

std::vector<EntityID> WorldEntityStore::ids() const {
  std::vector<EntityID> result;
  result.reserve(handles_.size());
  for (const auto& [entity_id, entity_handle] : handles_) {
    if (entity_handle != entt::null && registry_.valid(entity_handle)) {
      result.emplace_back(entity_id);
    }
  }
  return result;
}

std::optional<EntityID> WorldEntityStore::entity_for_player(PlayerID player_id) const noexcept {
  const auto found = player_entities_.find(player_id);
  return found == player_entities_.end() ? std::nullopt : std::optional<EntityID>{found->second};
}

std::optional<PlayerID> WorldEntityStore::player_for_entity(EntityID entity_id) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return std::nullopt;
  }
  const auto* player_entity = registry_.try_get<entity::PlayerEntity>(entity_handle);
  return player_entity == nullptr ? std::nullopt : std::optional<PlayerID>{player_entity->player_id};
}

std::optional<EntityNetID> WorldEntityStore::network_id_for_entity(EntityID entity_id) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return std::nullopt;
  }
  const auto* network = registry_.try_get<entity::NetworkEntity>(entity_handle);
  return network == nullptr ? std::nullopt : std::optional<EntityNetID>{network->net_id};
}

std::optional<EntityID> WorldEntityStore::entity_for_network_id(EntityNetID net_id) const noexcept {
  const auto view = registry_.view<entity::RuntimeEntity, entity::NetworkEntity>();
  for (const entt::entity entity_handle : view) {
    const auto& network = view.get<entity::NetworkEntity>(entity_handle);
    if (network.net_id == net_id) {
      return view.get<entity::RuntimeEntity>(entity_handle).entity_id;
    }
  }
  return std::nullopt;
}

std::optional<ChunkSectionCoord> WorldEntityStore::entity_chunk(EntityID entity_id) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return std::nullopt;
  }
  const auto* relevance = registry_.try_get<entity::ChunkRelevance>(entity_handle);
  return relevance == nullptr ? std::nullopt : std::optional<ChunkSectionCoord>{relevance->current_chunk};
}

usize WorldEntityStore::player_count() const noexcept {
  return player_entities_.size();
}

std::vector<PlayerID> WorldEntityStore::player_ids() const {
  std::vector<PlayerID> ids;
  ids.reserve(player_entities_.size());
  for (const auto& [player_id, entity_id] : player_entities_) {
    (void)entity_id;
    ids.emplace_back(player_id);
  }
  return ids;
}

entity::EntityTransform* WorldEntityStore::find_transform(EntityID entity_id) noexcept {
  return find_component<entity::EntityTransform>(registry_, handle(entity_id));
}

const entity::EntityTransform* WorldEntityStore::find_transform(EntityID entity_id) const noexcept {
  return find_component<entity::EntityTransform>(registry_, handle(entity_id));
}

entity::EntityVelocity* WorldEntityStore::find_velocity(EntityID entity_id) noexcept {
  return find_component<entity::EntityVelocity>(registry_, handle(entity_id));
}

const entity::EntityVelocity* WorldEntityStore::find_velocity(EntityID entity_id) const noexcept {
  return find_component<entity::EntityVelocity>(registry_, handle(entity_id));
}

const entity::EntityType* WorldEntityStore::find_type(EntityID entity_id) const noexcept {
  return find_component<entity::EntityType>(registry_, handle(entity_id));
}

entity::EntityCollision* WorldEntityStore::find_collision(EntityID entity_id) noexcept {
  return find_component<entity::EntityCollision>(registry_, handle(entity_id));
}

const entity::EntityCollision* WorldEntityStore::find_collision(EntityID entity_id) const noexcept {
  return find_component<entity::EntityCollision>(registry_, handle(entity_id));
}

entity::EntityMovement* WorldEntityStore::find_movement(EntityID entity_id) noexcept {
  return find_component<entity::EntityMovement>(registry_, handle(entity_id));
}

const entity::EntityMovement* WorldEntityStore::find_movement(EntityID entity_id) const noexcept {
  return find_component<entity::EntityMovement>(registry_, handle(entity_id));
}

entity::RideableComponent* WorldEntityStore::find_rideable(EntityID entity_id) noexcept {
  return find_component<entity::RideableComponent>(registry_, handle(entity_id));
}

const entity::RideableComponent* WorldEntityStore::find_rideable(EntityID entity_id) const noexcept {
  return find_component<entity::RideableComponent>(registry_, handle(entity_id));
}

entity::AttachedToComponent* WorldEntityStore::find_attachment(EntityID entity_id) noexcept {
  return find_component<entity::AttachedToComponent>(registry_, handle(entity_id));
}

const entity::AttachedToComponent* WorldEntityStore::find_attachment(EntityID entity_id) const noexcept {
  return find_component<entity::AttachedToComponent>(registry_, handle(entity_id));
}

entity::VehiclePhysicsComponent* WorldEntityStore::find_vehicle_physics(EntityID entity_id) noexcept {
  return find_component<entity::VehiclePhysicsComponent>(registry_, handle(entity_id));
}

const entity::VehiclePhysicsComponent* WorldEntityStore::find_vehicle_physics(EntityID entity_id) const noexcept {
  return find_component<entity::VehiclePhysicsComponent>(registry_, handle(entity_id));
}

entity::VehicleCargoComponent* WorldEntityStore::find_vehicle_cargo(EntityID entity_id) noexcept {
  return find_component<entity::VehicleCargoComponent>(registry_, handle(entity_id));
}

const entity::VehicleCargoComponent* WorldEntityStore::find_vehicle_cargo(EntityID entity_id) const noexcept {
  return find_component<entity::VehicleCargoComponent>(registry_, handle(entity_id));
}

bool WorldEntityStore::set_vehicle_cargo(
    EntityID entity_id,
    entity::VehicleCargoComponent cargo) noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  registry_.emplace_or_replace<entity::VehicleCargoComponent>(
      entity_handle,
      std::move(cargo));
  return true;
}

entity::EntityHealth* WorldEntityStore::find_health(EntityID entity_id) noexcept {
  return find_component<entity::EntityHealth>(registry_, handle(entity_id));
}

const entity::EntityHealth* WorldEntityStore::find_health(EntityID entity_id) const noexcept {
  return find_component<entity::EntityHealth>(registry_, handle(entity_id));
}

entity::KnockbackResistanceComponent* WorldEntityStore::find_knockback_resistance(
    EntityID entity_id) noexcept {
  return find_component<entity::KnockbackResistanceComponent>(registry_, handle(entity_id));
}

const entity::KnockbackResistanceComponent* WorldEntityStore::find_knockback_resistance(
    EntityID entity_id) const noexcept {
  return find_component<entity::KnockbackResistanceComponent>(registry_, handle(entity_id));
}

entity::BrainComponent* WorldEntityStore::find_brain(EntityID entity_id) noexcept {
  return find_component<entity::BrainComponent>(registry_, handle(entity_id));
}

const entity::BrainComponent* WorldEntityStore::find_brain(EntityID entity_id) const noexcept {
  return find_component<entity::BrainComponent>(registry_, handle(entity_id));
}

entity::MovementIntent* WorldEntityStore::find_movement_intent(EntityID entity_id) noexcept {
  return find_component<entity::MovementIntent>(registry_, handle(entity_id));
}

const entity::MovementIntent* WorldEntityStore::find_movement_intent(EntityID entity_id) const noexcept {
  return find_component<entity::MovementIntent>(registry_, handle(entity_id));
}

entity::EntityAnimationState* WorldEntityStore::find_animation_state(EntityID entity_id) noexcept {
  return find_component<entity::EntityAnimationState>(registry_, handle(entity_id));
}

const entity::EntityAnimationState* WorldEntityStore::find_animation_state(EntityID entity_id) const noexcept {
  return find_component<entity::EntityAnimationState>(registry_, handle(entity_id));
}

entity::DroppedItem* WorldEntityStore::find_dropped_item(EntityID entity_id) noexcept {
  return find_component<entity::DroppedItem>(registry_, handle(entity_id));
}

const entity::DroppedItem* WorldEntityStore::find_dropped_item(EntityID entity_id) const noexcept {
  return find_component<entity::DroppedItem>(registry_, handle(entity_id));
}

bool WorldEntityStore::set_animation(EntityID entity_id, std::string animation_name) noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  auto& animation = registry_.get_or_emplace<entity::EntityAnimationState>(entity_handle);
  if (animation_name.empty()) {
    animation.active.clear();
    return true;
  }
  if (animation.active.size() == 1U && animation.active.front().name == animation_name) {
    return true;
  }
  animation.active.clear();
  animation.active.push_back({std::move(animation_name), 0.0F});
  return true;
}

bool WorldEntityStore::play_animation(EntityID entity_id, std::string animation_name) noexcept {
  if (animation_name.empty()) {
    return false;
  }
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  auto& animation = registry_.get_or_emplace<entity::EntityAnimationState>(entity_handle);
  for (entity::EntityAnimationPlayback& active : animation.active) {
    if (active.name == animation_name) {
      active.time_seconds = 0.0F;
      return true;
    }
  }
  animation.active.push_back({std::move(animation_name), 0.0F});
  return true;
}

bool WorldEntityStore::set_player_visual_pose(
    PlayerID player_id,
    f32 look_yaw_radians,
    f32 look_pitch_radians,
    f32 max_head_body_yaw_radians) noexcept {
  const std::optional<EntityID> entity_id = entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return false;
  }
  const entt::entity entity_handle = handle(entity_id.value());
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  auto* transform = registry_.try_get<entity::EntityTransform>(entity_handle);
  if (transform == nullptr) {
    return false;
  }
  auto& pose = registry_.get_or_emplace<entity::PlayerVisualPose>(entity_handle);
  if (!pose.initialized) {
    pose.body_yaw_radians = look_yaw_radians;
    pose.initialized = true;
  }

  const f32 max_yaw = std::max(0.0F, max_head_body_yaw_radians);
  f32 head_yaw = wrap_radians(look_yaw_radians - pose.body_yaw_radians);
  if (max_yaw <= 0.0F) {
    pose.body_yaw_radians = look_yaw_radians;
    head_yaw = 0.0F;
  } else if (head_yaw > max_yaw) {
    pose.body_yaw_radians = look_yaw_radians - max_yaw;
    head_yaw = max_yaw;
  } else if (head_yaw < -max_yaw) {
    pose.body_yaw_radians = look_yaw_radians + max_yaw;
    head_yaw = -max_yaw;
  }

  pose.body_yaw_radians = wrap_radians(pose.body_yaw_radians);
  pose.head_yaw_radians = head_yaw;
  pose.head_pitch_radians = look_pitch_radians;
  transform->previous_rotation = transform->rotation;
  transform->rotation = model_forward_yaw_rotation(pose.body_yaw_radians);
  return true;
}

bool WorldEntityStore::set_dropped_item_stack(EntityID entity_id, player::ItemStack stack) noexcept {
  entity::DroppedItem* dropped = find_dropped_item(entity_id);
  if (dropped == nullptr) {
    return false;
  }
  player::normalize_item_stack_data(stack);
  dropped->stack = stack;
  return true;
}

bool WorldEntityStore::is_persistent(EntityID entity_id) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  return entity_handle != entt::null && registry_.valid(entity_handle) &&
         registry_.all_of<entity::PersistentEntity>(entity_handle);
}

std::optional<std::string> WorldEntityStore::persistent_uuid(EntityID entity_id) const {
  const entity::PersistentEntity* persistent =
      find_component<entity::PersistentEntity>(registry_, handle(entity_id));
  if (persistent == nullptr) {
    return std::nullopt;
  }
  return persistent->uuid;
}

bool WorldEntityStore::set_natural_spawn(
    EntityID entity_id,
    NaturalSpawnComponent component) noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  registry_.emplace_or_replace<NaturalSpawnComponent>(entity_handle, component);
  return true;
}

NaturalSpawnComponent* WorldEntityStore::find_natural_spawn(EntityID entity_id) noexcept {
  return find_component<NaturalSpawnComponent>(registry_, handle(entity_id));
}

const NaturalSpawnComponent* WorldEntityStore::find_natural_spawn(EntityID entity_id) const noexcept {
  return find_component<NaturalSpawnComponent>(registry_, handle(entity_id));
}

bool WorldEntityStore::add_tag(EntityID entity_id, std::string tag) {
  if (tag.empty()) {
    return false;
  }
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  auto& tags = registry_.get_or_emplace<entity::EntityTags>(entity_handle);
  tags.tags.insert(std::move(tag));
  return true;
}

bool WorldEntityStore::remove_tag(EntityID entity_id, std::string_view tag) noexcept {
  const entt::entity entity_handle = handle(entity_id);
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return false;
  }
  auto* tags = registry_.try_get<entity::EntityTags>(entity_handle);
  if (tags == nullptr) {
    return true;
  }
  tags->tags.erase(std::string{tag});
  if (tags->tags.empty()) {
    registry_.remove<entity::EntityTags>(entity_handle);
  }
  return true;
}

bool WorldEntityStore::has_tag(EntityID entity_id, std::string_view tag) const noexcept {
  const entt::entity entity_handle = handle(entity_id);
  const auto* tags = find_component<entity::EntityTags>(registry_, entity_handle);
  return tags != nullptr && tags->tags.find(std::string{tag}) != tags->tags.end();
}

bool WorldEntityStore::set_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept {
  const entt::entity entity_handle = handle(entity_id);
  entity::EntityTransform* transform = find_component<entity::EntityTransform>(
      registry_,
      entity_handle);
  if (transform == nullptr) {
    return false;
  }
  transform->previous_position = transform->position;
  transform->previous_rotation = transform->rotation;
  transform->position = position;
  transform->rotation = rotation;
  update_chunk(entity_handle);
  return true;
}

bool WorldEntityStore::set_position(EntityID entity_id, Vec3f position) noexcept {
  entity::EntityTransform* transform = find_transform(entity_id);
  if (transform == nullptr) {
    return false;
  }
  return set_transform(entity_id, position, transform->rotation);
}

bool WorldEntityStore::mount_entity(
    EntityID rideable_id,
    EntityID passenger_id,
    std::optional<std::string_view> requested_seat) noexcept {
  if (rideable_id == passenger_id || !exists(rideable_id) || !exists(passenger_id)) {
    return false;
  }
  if (find_attachment(passenger_id) != nullptr) {
    return false;
  }
  entity::RideableComponent* rideable = find_rideable(rideable_id);
  if (rideable == nullptr || rideable->seats.empty()) {
    return false;
  }
  auto seat = rideable->seats.end();
  if (requested_seat.has_value() && !requested_seat->empty()) {
    seat = find_seat(*rideable, requested_seat.value());
  } else {
    seat = first_available_controlling_seat(*rideable);
    if (seat == rideable->seats.end()) {
      seat = first_available_seat(*rideable);
    }
  }
  if (seat == rideable->seats.end() || seat->occupant.value != 0U) {
    return false;
  }

  seat->occupant = passenger_id;
  if (seat->controls_vehicle) {
    rideable->controller = passenger_id;
  }
  const entt::entity passenger_handle = handle(passenger_id);
  registry_.emplace_or_replace<entity::AttachedToComponent>(
      passenger_handle,
      rideable_id,
      seat->id,
      seat->local_position,
      seat->local_rotation);
  return update_passenger_attachment_transform(passenger_id);
}

bool WorldEntityStore::dismount_entity(EntityID passenger_id) noexcept {
  const entt::entity passenger_handle = handle(passenger_id);
  entity::AttachedToComponent* attachment =
      find_component<entity::AttachedToComponent>(registry_, passenger_handle);
  if (attachment == nullptr) {
    return false;
  }
  entity::RideableComponent* rideable = find_rideable(attachment->parent);
  if (rideable != nullptr) {
    auto seat = find_seat(*rideable, attachment->seat_id);
    if (seat != rideable->seats.end() && seat->occupant == passenger_id) {
      seat->occupant = {};
    }
    if (rideable->controller == passenger_id) {
      rideable->controller = {};
      for (const entity::Seat& candidate : rideable->seats) {
        if (candidate.controls_vehicle && candidate.occupant.value != 0U) {
          rideable->controller = candidate.occupant;
          break;
        }
      }
    }
  }
  registry_.remove<entity::AttachedToComponent>(passenger_handle);
  return true;
}

std::optional<EntityID> WorldEntityStore::mounted_rideable(EntityID passenger_id) const noexcept {
  const entity::AttachedToComponent* attachment = find_attachment(passenger_id);
  return attachment == nullptr ? std::nullopt : std::optional<EntityID>{attachment->parent};
}

std::optional<std::string> WorldEntityStore::mounted_seat(EntityID passenger_id) const {
  const entity::AttachedToComponent* attachment = find_attachment(passenger_id);
  return attachment == nullptr ? std::nullopt : std::optional<std::string>{attachment->seat_id};
}

bool WorldEntityStore::update_passenger_attachment_transform(EntityID passenger_id) noexcept {
  const entity::AttachedToComponent* attachment = find_attachment(passenger_id);
  if (attachment == nullptr) {
    return false;
  }
  const entity::EntityTransform* parent = find_transform(attachment->parent);
  if (parent == nullptr) {
    return false;
  }
  const Vec3f world_position =
      parent->position + rotate_vector(parent->rotation, attachment->local_position);
  const Quatf world_rotation = normalize_quat(
      multiply_quat(parent->rotation, attachment->local_rotation));
  return set_transform(passenger_id, world_position, world_rotation);
}

u32 WorldEntityStore::update_passenger_attachment_transforms() noexcept {
  const auto view = registry_.view<entity::RuntimeEntity, entity::AttachedToComponent>();
  u32 updated = 0;
  for (const entt::entity handle : view) {
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    if (update_passenger_attachment_transform(runtime.entity_id)) {
      ++updated;
    }
  }
  return updated;
}

std::optional<Vec3f> WorldEntityStore::find_safe_dismount_position(
    EntityID rideable_id,
    const WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const player::PlayerTuning& tuning) const {
  const entity::EntityTransform* transform = find_transform(rideable_id);
  const entity::EntityCollision* collision = find_collision(rideable_id);
  if (transform == nullptr || collision == nullptr) {
    return std::nullopt;
  }

  const Aabb3f vehicle_bounds = entity_body_bounds(transform->position, collision->box);
  const physics::KinematicConfig player_config =
      physics::kinematic_config_from_tuning(tuning);
  const WorldCollisionQuery query(world, block_registry, true);
  const f32 lateral = std::max(collision->box.x, collision->box.z) * 0.5F +
                      tuning.body_width + 0.25F;
  const f32 rear_front = collision->box.z * 0.5F + tuning.body_width + 0.25F;
  const std::array<Vec3f, 5> local_offsets{{
      {-lateral, 0.0F, 0.0F},
      {lateral, 0.0F, 0.0F},
      {0.0F, 0.0F, -rear_front},
      {0.0F, 0.0F, rear_front},
      {0.0F, collision->box.y + 0.1F, 0.0F},
  }};

  const auto candidate_fits = [&](Vec3f position) {
    const Aabb3f bounds = physics::make_body_bounds(position, player_config);
    if (physics::overlaps_solid_blocks(query, bounds, player_config.collision_epsilon)) {
      return false;
    }
    const bool supported =
        physics::has_support_below(
            query,
            bounds,
            std::max(player_config.collision_epsilon * 2.0F, 0.05F));
    if (supported) {
      return true;
    }
    const BlockPos feet{{floor_to_i32(position.x), floor_to_i32(position.y), floor_to_i32(position.z)}};
    return world_cell_contains_fluid(world, block_registry, feet);
  };

  for (const Vec3f& local_offset : local_offsets) {
    const Vec3f position = transform->position + rotate_vector(transform->rotation, local_offset);
    if (distance(position, transform->position) > lateral + rear_front + collision->box.y + 2.0F) {
      continue;
    }
    if (candidate_fits(position)) {
      return position;
    }
  }

  const Vec3f top{
      transform->position.x,
      vehicle_bounds.max.y + player_config.collision_epsilon,
      transform->position.z};
  if (candidate_fits(top)) {
    return top;
  }
  return std::nullopt;
}

std::vector<entity::EntitySnapshotEntry> WorldEntityStore::collect_entity_snapshots() const {
  std::vector<entity::EntitySnapshotEntry> snapshots;
  const auto view = registry_.view<
      entity::RuntimeEntity,
      entity::EntityType,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::NetworkEntity,
      entity::ChunkRelevance>();
  snapshots.reserve(view.size_hint());
  for (const entt::entity entity_handle : view) {
    if (registry_.all_of<entity::DroppedItem>(entity_handle)) {
      continue;
    }
    const auto& runtime = view.get<entity::RuntimeEntity>(entity_handle);
    const auto& type = view.get<entity::EntityType>(entity_handle);
    const auto& transform = view.get<entity::EntityTransform>(entity_handle);
    const auto& velocity = view.get<entity::EntityVelocity>(entity_handle);
    const auto& network = view.get<entity::NetworkEntity>(entity_handle);
    const auto& relevance = view.get<entity::ChunkRelevance>(entity_handle);

    entity::EntitySnapshotEntry entry;
    entry.entity_id = runtime.entity_id;
    entry.net_id = network.net_id;
    entry.type_id = type.type_id;
    entry.type_name = type.type_name;
    entry.position = transform.position;
    entry.velocity = velocity.linear;
    entry.rotation = transform.rotation;
    entry.chunk = relevance.current_chunk;
    if (const auto* player_entity = registry_.try_get<entity::PlayerEntity>(entity_handle);
        player_entity != nullptr) {
      entry.owning_player = player_entity->player_id;
    }
    if (const auto* animation = registry_.try_get<entity::EntityAnimationState>(entity_handle);
        animation != nullptr) {
      entry.animations = animation->active;
    }
    if (const auto* pose = registry_.try_get<entity::PlayerVisualPose>(entity_handle);
        pose != nullptr) {
      entry.head_rotation_degrees = {
          radians_to_degrees(pose->head_pitch_radians),
          radians_to_degrees(pose->head_yaw_radians),
          0.0F};
    }
    if (const auto* health = registry_.try_get<entity::EntityHealth>(entity_handle);
        health != nullptr) {
      entry.has_health = true;
      entry.health_current = health->current;
      entry.health_max = health->max;
      entry.health_invulnerable = health->invulnerable;
    }
    snapshots.emplace_back(std::move(entry));
  }
  return snapshots;
}

std::vector<entity::DroppedItemEntitySnapshot> WorldEntityStore::collect_dropped_item_snapshots() const {
  std::vector<entity::DroppedItemEntitySnapshot> snapshots;
  const auto view = registry_.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::NetworkEntity,
      entity::ChunkRelevance,
      entity::DroppedItem>();
  snapshots.reserve(view.size_hint());
  for (const entt::entity entity_handle : view) {
    const auto& runtime = view.get<entity::RuntimeEntity>(entity_handle);
    const auto& transform = view.get<entity::EntityTransform>(entity_handle);
    const auto& velocity = view.get<entity::EntityVelocity>(entity_handle);
    const auto& network = view.get<entity::NetworkEntity>(entity_handle);
    const auto& relevance = view.get<entity::ChunkRelevance>(entity_handle);
    const auto& dropped = view.get<entity::DroppedItem>(entity_handle);
    if (dropped.stack.count == 0U) {
      continue;
    }

    snapshots.push_back({runtime.entity_id,
                         network.net_id,
                         world_id_,
                         transform.position,
                         velocity.linear,
                         relevance.current_chunk,
                         dropped.stack,
                         dropped.pickup_delay_seconds});
  }
  return snapshots;
}

std::vector<entity::PersistentEntityRecord> WorldEntityStore::persistent_records() const {
  std::vector<entity::PersistentEntityRecord> records;
  const auto view = registry_.view<
      entity::PersistentEntity,
      entity::EntityTransform,
      entity::EntityVelocity>();
  records.reserve(view.size_hint());
  for (const entt::entity entity_handle : view) {
    if (registry_.all_of<entity::PlayerEntity>(entity_handle)) {
      continue;
    }
    if (const auto* natural = registry_.try_get<NaturalSpawnComponent>(entity_handle);
        natural != nullptr && natural->transient) {
      continue;
    }
    const auto& persistent = view.get<entity::PersistentEntity>(entity_handle);
    const auto& transform = view.get<entity::EntityTransform>(entity_handle);
    const auto& velocity = view.get<entity::EntityVelocity>(entity_handle);
    entity::PersistentEntityRecord record{
        persistent.uuid,
        persistent.type_name,
        world_id_,
        transform.position,
        transform.rotation,
        velocity.linear};
    if (const auto* dropped = registry_.try_get<entity::DroppedItem>(entity_handle);
        dropped != nullptr) {
      record.dropped_item_stack = dropped->stack;
      record.dropped_item_pickup_delay_seconds = dropped->pickup_delay_seconds;
    }
    if (const auto* health = registry_.try_get<entity::EntityHealth>(entity_handle);
        health != nullptr) {
      record.has_health = true;
      record.health_current = health->current;
      record.health_max = health->max;
      record.health_invulnerable = health->invulnerable;
    }
    if (const auto* vehicle = registry_.try_get<entity::VehiclePhysicsComponent>(entity_handle);
        vehicle != nullptr && vehicle->fuel_capacity_seconds > 0.0F) {
      record.has_vehicle_fuel = true;
      record.vehicle_fuel_item = vehicle->fuel_item;
      record.vehicle_fuel_seconds = vehicle->fuel_seconds;
      record.vehicle_fuel_capacity_seconds = vehicle->fuel_capacity_seconds;
      record.vehicle_fuel_burn_per_second = vehicle->fuel_burn_per_second;
    }
    if (const auto* cargo = registry_.try_get<entity::VehicleCargoComponent>(entity_handle);
        cargo != nullptr) {
      record.has_vehicle_cargo = true;
      record.vehicle_cargo_slot_count =
          static_cast<u16>(std::min<usize>(
              cargo->inventory.slot_count(),
              entity::MaxVehicleCargoSlots));
      record.vehicle_cargo_slots.assign(
          cargo->inventory.slots().begin(),
          cargo->inventory.slots().begin() + record.vehicle_cargo_slot_count);
    }
    records.push_back(std::move(record));
  }
  return records;
}

void WorldEntityStore::restore_persistent_entities(
    std::span<const entity::PersistentEntityRecord> records,
    const entity::EntityRegistry* entity_registry,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) {
  for (const entity::PersistentEntityRecord& record : records) {
    if (record.world_id != world_id_) {
      continue;
    }
    EntityCreateInfo info;
    info.type_name = record.type_name;
    if (entity_registry != nullptr) {
      if (const std::optional<EntityTypeID> type_id = entity_registry->find_type(record.type_name);
          type_id.has_value()) {
        info.type_id = type_id.value();
        if (const entity::EntityTypeRecord* type = entity_registry->type(type_id.value());
            type != nullptr) {
          info.authored_components = type->components;
        }
      }
    }
    info.position = record.position;
    info.rotation = record.rotation;
    info.velocity = record.velocity;
    info.persistent = true;
    info.persistent_uuid = record.uuid;
    info.replicated = true;
    info.dropped_item_stack = record.dropped_item_stack;
    info.dropped_item_pickup_delay_seconds = record.dropped_item_pickup_delay_seconds;
    info.vehicle_definitions = vehicle_definitions;
    const EntityID entity_id = create_entity(std::move(info));
    if (record.has_health) {
      entity::EntityHealth* health = find_health(entity_id);
      if (health == nullptr) {
        const entt::entity entity_handle = handle(entity_id);
        if (entity_handle != entt::null && registry_.valid(entity_handle)) {
          health = &registry_.emplace_or_replace<entity::EntityHealth>(entity_handle);
        }
      }
      if (health != nullptr) {
        if (std::isfinite(record.health_max) && record.health_max > 0.0F) {
          health->max = record.health_max;
        }
        health->current =
            std::clamp(
                std::isfinite(record.health_current) ? record.health_current : health->current,
                0.0F,
                health->max);
        health->invulnerable = record.health_invulnerable;
      }
    }
    if (record.has_vehicle_fuel) {
      entity::VehiclePhysicsComponent* vehicle = find_vehicle_physics(entity_id);
      if (vehicle != nullptr &&
          std::isfinite(record.vehicle_fuel_capacity_seconds) &&
          record.vehicle_fuel_capacity_seconds > 0.0F) {
        vehicle->fuel_item = record.vehicle_fuel_item;
        vehicle->fuel_capacity_seconds = record.vehicle_fuel_capacity_seconds;
        vehicle->fuel_seconds =
            std::clamp(
                std::isfinite(record.vehicle_fuel_seconds)
                    ? record.vehicle_fuel_seconds
                    : vehicle->fuel_seconds,
                0.0F,
                vehicle->fuel_capacity_seconds);
        if (std::isfinite(record.vehicle_fuel_burn_per_second) &&
            record.vehicle_fuel_burn_per_second > 0.0F) {
          vehicle->fuel_burn_per_second = record.vehicle_fuel_burn_per_second;
        }
      }
    }
    if (record.has_vehicle_cargo &&
        record.vehicle_cargo_slot_count <= entity::MaxVehicleCargoSlots) {
      const entt::entity entity_handle = handle(entity_id);
      if (entity_handle != entt::null && registry_.valid(entity_handle)) {
        entity::VehicleCargoComponent cargo{
            player::Inventory{record.vehicle_cargo_slot_count}};
        const usize slot_count = std::min<usize>(
            record.vehicle_cargo_slots.size(),
            cargo.inventory.slot_count());
        for (usize slot = 0; slot < slot_count; ++slot) {
          (void)cargo.inventory.set_slot(slot, record.vehicle_cargo_slots[slot]);
        }
        registry_.emplace_or_replace<entity::VehicleCargoComponent>(
            entity_handle,
            std::move(cargo));
      }
    }
  }
}

entt::entity WorldEntityStore::handle(EntityID entity_id) const noexcept {
  const auto found = handles_.find(entity_id);
  return found == handles_.end() ? entt::null : found->second;
}

void WorldEntityStore::update_chunk(entt::entity entity_handle) noexcept {
  if (entity_handle == entt::null || !registry_.valid(entity_handle)) {
    return;
  }
  auto* transform = registry_.try_get<entity::EntityTransform>(entity_handle);
  auto* relevance = registry_.try_get<entity::ChunkRelevance>(entity_handle);
  if (transform == nullptr || relevance == nullptr) {
    return;
  }
  relevance->current_chunk = chunk_for_position(transform->position);
}

} // namespace arv::world::detail

namespace arv::world {

EntityID WorldRuntime::create_entity(EntityCreateInfo info) {
  assert_owner_thread();
  return impl_->entities.create_entity(std::move(info));
}

EntityID WorldRuntime::create_dropped_item(
    player::ItemStack stack,
    Vec3f position,
    Vec3f velocity,
    Seconds pickup_delay_seconds,
    bool persistent) {
  assert_owner_thread();
  return impl_->entities.create_dropped_item(
      stack,
      position,
      velocity,
      pickup_delay_seconds,
      persistent);
}

bool WorldRuntime::destroy_entity(EntityID entity_id, EntityDespawnReason reason) {
  assert_owner_thread();
  (void)clear_status_effects(entity_id);
  return impl_->entities.destroy_entity(entity_id, reason);
}

void WorldRuntime::flush_entity_despawns() {
  assert_owner_thread();
  impl_->entities.flush_despawns(impl_->regions);
}

std::vector<RideEndedEvent> WorldRuntime::drain_ride_ended_events() {
  assert_owner_thread();
  return impl_->entities.drain_ride_ended_events();
}

bool WorldRuntime::entity_exists(EntityID entity_id) const noexcept {
  return impl_->entities.exists(entity_id);
}

usize WorldRuntime::entity_count() const noexcept {
  return impl_->entities.count();
}

std::vector<EntityID> WorldRuntime::entity_ids() const {
  return impl_->entities.ids();
}

std::optional<EntityID> WorldRuntime::entity_for_player(PlayerID player_id) const noexcept {
  return impl_->entities.entity_for_player(player_id);
}

std::optional<PlayerID> WorldRuntime::player_for_entity(EntityID entity_id) const noexcept {
  return impl_->entities.player_for_entity(entity_id);
}

std::optional<EntityNetID> WorldRuntime::network_id_for_entity(EntityID entity_id) const noexcept {
  return impl_->entities.network_id_for_entity(entity_id);
}

std::optional<EntityID> WorldRuntime::entity_for_network_id(EntityNetID net_id) const noexcept {
  return impl_->entities.entity_for_network_id(net_id);
}

std::optional<ChunkSectionCoord> WorldRuntime::entity_chunk(EntityID entity_id) const noexcept {
  return impl_->entities.entity_chunk(entity_id);
}

entity::EntityTransform* WorldRuntime::find_entity_transform(EntityID entity_id) noexcept {
  return impl_->entities.find_transform(entity_id);
}

const entity::EntityTransform* WorldRuntime::find_entity_transform(EntityID entity_id) const noexcept {
  return impl_->entities.find_transform(entity_id);
}

entity::EntityVelocity* WorldRuntime::find_entity_velocity(EntityID entity_id) noexcept {
  return impl_->entities.find_velocity(entity_id);
}

const entity::EntityVelocity* WorldRuntime::find_entity_velocity(EntityID entity_id) const noexcept {
  return impl_->entities.find_velocity(entity_id);
}

const entity::EntityType* WorldRuntime::find_entity_type(EntityID entity_id) const noexcept {
  return impl_->entities.find_type(entity_id);
}

entity::EntityCollision* WorldRuntime::find_entity_collision(EntityID entity_id) noexcept {
  return impl_->entities.find_collision(entity_id);
}

const entity::EntityCollision* WorldRuntime::find_entity_collision(EntityID entity_id) const noexcept {
  return impl_->entities.find_collision(entity_id);
}

entity::EntityMovement* WorldRuntime::find_entity_movement(EntityID entity_id) noexcept {
  return impl_->entities.find_movement(entity_id);
}

const entity::EntityMovement* WorldRuntime::find_entity_movement(EntityID entity_id) const noexcept {
  return impl_->entities.find_movement(entity_id);
}

entity::RideableComponent* WorldRuntime::find_entity_rideable(EntityID entity_id) noexcept {
  return impl_->entities.find_rideable(entity_id);
}

const entity::RideableComponent* WorldRuntime::find_entity_rideable(EntityID entity_id) const noexcept {
  return impl_->entities.find_rideable(entity_id);
}

entity::AttachedToComponent* WorldRuntime::find_entity_attachment(EntityID entity_id) noexcept {
  return impl_->entities.find_attachment(entity_id);
}

const entity::AttachedToComponent* WorldRuntime::find_entity_attachment(EntityID entity_id) const noexcept {
  return impl_->entities.find_attachment(entity_id);
}

entity::VehiclePhysicsComponent* WorldRuntime::find_entity_vehicle_physics(EntityID entity_id) noexcept {
  return impl_->entities.find_vehicle_physics(entity_id);
}

const entity::VehiclePhysicsComponent* WorldRuntime::find_entity_vehicle_physics(EntityID entity_id) const noexcept {
  return impl_->entities.find_vehicle_physics(entity_id);
}

entity::VehicleCargoComponent* WorldRuntime::find_entity_vehicle_cargo(EntityID entity_id) noexcept {
  return impl_->entities.find_vehicle_cargo(entity_id);
}

const entity::VehicleCargoComponent* WorldRuntime::find_entity_vehicle_cargo(EntityID entity_id) const noexcept {
  return impl_->entities.find_vehicle_cargo(entity_id);
}

bool WorldRuntime::set_entity_vehicle_cargo(
    EntityID entity_id,
    entity::VehicleCargoComponent cargo) noexcept {
  return impl_->entities.set_vehicle_cargo(entity_id, std::move(cargo));
}

entity::EntityHealth* WorldRuntime::find_entity_health(EntityID entity_id) noexcept {
  return impl_->entities.find_health(entity_id);
}

const entity::EntityHealth* WorldRuntime::find_entity_health(EntityID entity_id) const noexcept {
  return impl_->entities.find_health(entity_id);
}

entity::KnockbackResistanceComponent* WorldRuntime::find_entity_knockback_resistance(
    EntityID entity_id) noexcept {
  return impl_->entities.find_knockback_resistance(entity_id);
}

const entity::KnockbackResistanceComponent* WorldRuntime::find_entity_knockback_resistance(
    EntityID entity_id) const noexcept {
  return impl_->entities.find_knockback_resistance(entity_id);
}

entity::BrainComponent* WorldRuntime::find_entity_brain(EntityID entity_id) noexcept {
  return impl_->entities.find_brain(entity_id);
}

const entity::BrainComponent* WorldRuntime::find_entity_brain(EntityID entity_id) const noexcept {
  return impl_->entities.find_brain(entity_id);
}

entity::MovementIntent* WorldRuntime::find_entity_movement_intent(EntityID entity_id) noexcept {
  return impl_->entities.find_movement_intent(entity_id);
}

const entity::MovementIntent* WorldRuntime::find_entity_movement_intent(EntityID entity_id) const noexcept {
  return impl_->entities.find_movement_intent(entity_id);
}

entity::EntityAnimationState* WorldRuntime::find_entity_animation_state(EntityID entity_id) noexcept {
  return impl_->entities.find_animation_state(entity_id);
}

const entity::EntityAnimationState* WorldRuntime::find_entity_animation_state(EntityID entity_id) const noexcept {
  return impl_->entities.find_animation_state(entity_id);
}

entity::DroppedItem* WorldRuntime::find_dropped_item(EntityID entity_id) noexcept {
  return impl_->entities.find_dropped_item(entity_id);
}

const entity::DroppedItem* WorldRuntime::find_dropped_item(EntityID entity_id) const noexcept {
  return impl_->entities.find_dropped_item(entity_id);
}

bool WorldRuntime::set_entity_animation(EntityID entity_id, std::string animation_name) noexcept {
  assert_owner_thread();
  return impl_->entities.set_animation(entity_id, std::move(animation_name));
}

bool WorldRuntime::play_entity_animation(EntityID entity_id, std::string animation_name) noexcept {
  assert_owner_thread();
  return impl_->entities.play_animation(entity_id, std::move(animation_name));
}

bool WorldRuntime::set_player_visual_pose(
    PlayerID player_id,
    f32 look_yaw_radians,
    f32 look_pitch_radians,
    f32 max_head_body_yaw_radians) noexcept {
  assert_owner_thread();
  return impl_->entities.set_player_visual_pose(
      player_id,
      look_yaw_radians,
      look_pitch_radians,
      max_head_body_yaw_radians);
}

bool WorldRuntime::set_dropped_item_stack(EntityID entity_id, player::ItemStack stack) noexcept {
  assert_owner_thread();
  return impl_->entities.set_dropped_item_stack(entity_id, stack);
}

bool WorldRuntime::entity_is_persistent(EntityID entity_id) const noexcept {
  return impl_->entities.is_persistent(entity_id);
}

std::optional<std::string> WorldRuntime::persistent_entity_uuid(EntityID entity_id) const {
  return impl_->entities.persistent_uuid(entity_id);
}

bool WorldRuntime::set_entity_natural_spawn(
    EntityID entity_id,
    NaturalSpawnComponent component) noexcept {
  assert_owner_thread();
  return impl_->entities.set_natural_spawn(entity_id, component);
}

NaturalSpawnComponent* WorldRuntime::find_entity_natural_spawn(EntityID entity_id) noexcept {
  return impl_->entities.find_natural_spawn(entity_id);
}

const NaturalSpawnComponent* WorldRuntime::find_entity_natural_spawn(EntityID entity_id) const noexcept {
  return impl_->entities.find_natural_spawn(entity_id);
}

bool WorldRuntime::add_entity_tag(EntityID entity_id, std::string tag) {
  assert_owner_thread();
  return impl_->entities.add_tag(entity_id, std::move(tag));
}

bool WorldRuntime::remove_entity_tag(EntityID entity_id, std::string_view tag) noexcept {
  assert_owner_thread();
  return impl_->entities.remove_tag(entity_id, tag);
}

bool WorldRuntime::entity_has_tag(EntityID entity_id, std::string_view tag) const noexcept {
  return impl_->entities.has_tag(entity_id, tag);
}

bool WorldRuntime::set_entity_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept {
  assert_owner_thread();
  return impl_->entities.set_transform(entity_id, position, rotation);
}

bool WorldRuntime::set_entity_position(EntityID entity_id, Vec3f position) noexcept {
  assert_owner_thread();
  return impl_->entities.set_position(entity_id, position);
}

bool WorldRuntime::mount_entity(
    EntityID rideable_id,
    EntityID passenger_id,
    std::optional<std::string_view> requested_seat) noexcept {
  assert_owner_thread();
  return impl_->entities.mount_entity(rideable_id, passenger_id, requested_seat);
}

bool WorldRuntime::dismount_entity(EntityID passenger_id) noexcept {
  assert_owner_thread();
  return impl_->entities.dismount_entity(passenger_id);
}

std::optional<EntityID> WorldRuntime::mounted_rideable(EntityID passenger_id) const noexcept {
  return impl_->entities.mounted_rideable(passenger_id);
}

std::optional<std::string> WorldRuntime::mounted_seat(EntityID passenger_id) const {
  return impl_->entities.mounted_seat(passenger_id);
}

bool WorldRuntime::update_passenger_attachment_transform(EntityID passenger_id) noexcept {
  assert_owner_thread();
  return impl_->entities.update_passenger_attachment_transform(passenger_id);
}

u32 WorldRuntime::update_passenger_attachment_transforms() noexcept {
  assert_owner_thread();
  return impl_->entities.update_passenger_attachment_transforms();
}

std::optional<Vec3f> WorldRuntime::find_safe_dismount_position(
    EntityID rideable_id,
    const voxel::BlockRegistry& block_registry,
    const player::PlayerTuning& tuning) const {
  return impl_->entities.find_safe_dismount_position(
      rideable_id,
      *this,
      block_registry,
      tuning);
}

std::vector<entity::EntitySnapshotEntry> WorldRuntime::collect_entity_snapshots() const {
  return impl_->entities.collect_entity_snapshots();
}

std::vector<entity::DroppedItemEntitySnapshot> WorldRuntime::collect_dropped_item_snapshots() const {
  return impl_->entities.collect_dropped_item_snapshots();
}

std::vector<entity::PersistentEntityRecord> WorldRuntime::persistent_entity_records() const {
  return impl_->entities.persistent_records();
}

void WorldRuntime::restore_persistent_entities(
    std::span<const entity::PersistentEntityRecord> records,
    const entity::EntityRegistry* entity_registry,
    const entity::VehicleDefinitionRegistry* vehicle_definitions) {
  assert_owner_thread();
  impl_->entities.restore_persistent_entities(records, entity_registry, vehicle_definitions);
}

} // namespace arv::world
