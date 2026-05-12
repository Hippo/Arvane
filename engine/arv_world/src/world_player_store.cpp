#include "world_runtime_internal.hpp"

#include <string>
#include <utility>

namespace arv::world::detail {

player::PlayerState& WorldPlayerStore::ensure_player(
    WorldEntityStore& entities,
    WorldID world_id,
    PlayerID player_id) {
  if (const std::optional<EntityID> existing = entities.entity_for_player(player_id);
      existing.has_value()) {
    const entt::entity handle = entities.handle(existing.value());
    return entities.registry().get<player::PlayerState>(handle);
  }

  EntityCreateInfo info;
  info.type_id = entity::PlayerEntityTypeID;
  info.type_name = std::string{entity::PlayerEntityTypeName};
  info.position = {8.0F, 20.0F, -20.0F};
  info.persistent = true;
  info.persistent_uuid = "player:" + std::to_string(player_id.value);
  info.replicated = true;
  info.owning_player = player_id;
  const EntityID entity_id = entities.create_entity(std::move(info));
  const entt::entity handle = entities.handle(entity_id);
  (void)entities.registry().get_or_emplace<entity::EntityHealth>(
      handle,
      20.0F,
      20.0F,
      false);
  return entities.registry().get<player::PlayerState>(handle);
}

player::PlayerState* WorldPlayerStore::find_player(
    WorldEntityStore& entities,
    PlayerID player_id) noexcept {
  const std::optional<EntityID> entity_id = entities.entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return nullptr;
  }
  const entt::entity handle = entities.handle(entity_id.value());
  return handle == entt::null || !entities.registry().valid(handle)
             ? nullptr
             : entities.registry().try_get<player::PlayerState>(handle);
}

const player::PlayerState* WorldPlayerStore::find_player(
    const WorldEntityStore& entities,
    PlayerID player_id) noexcept {
  const std::optional<EntityID> entity_id = entities.entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return nullptr;
  }
  const entt::entity handle = entities.handle(entity_id.value());
  return handle == entt::null || !entities.registry().valid(handle)
             ? nullptr
             : entities.registry().try_get<player::PlayerState>(handle);
}

usize WorldPlayerStore::player_count(const WorldEntityStore& entities) const noexcept {
  return entities.player_count();
}

std::vector<PlayerID> WorldPlayerStore::player_ids(const WorldEntityStore& entities) const {
  return entities.player_ids();
}

} // namespace arv::world::detail

namespace arv::world {

player::PlayerState& WorldRuntime::ensure_player(PlayerID player_id) {
  assert_owner_thread();
  return impl_->players.ensure_player(impl_->entities, impl_->id, player_id);
}

player::PlayerState* WorldRuntime::find_player(PlayerID player_id) noexcept {
  return impl_->players.find_player(impl_->entities, player_id);
}

const player::PlayerState* WorldRuntime::find_player(PlayerID player_id) const noexcept {
  return impl_->players.find_player(impl_->entities, player_id);
}

usize WorldRuntime::player_count() const noexcept {
  return impl_->players.player_count(impl_->entities);
}

std::vector<PlayerID> WorldRuntime::player_ids() const {
  return impl_->players.player_ids(impl_->entities);
}

} // namespace arv::world
