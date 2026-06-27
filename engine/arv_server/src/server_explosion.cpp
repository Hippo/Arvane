#include "server_runtime_internal.hpp"

#include <algorithm>
#include <cmath>

namespace arv::server::detail {
namespace {

[[nodiscard]] world::ExplosionRequest to_world_request(
    const command::ExplosionCommandRequest& request) {
  world::ExplosionRequest out;
  out.world_id = request.world_id;
  out.origin = request.origin;
  out.profile_id = request.profile_id;
  out.source_entity = request.source_entity;
  out.seed = request.seed;
  out.overrides.power = request.power;
  out.overrides.radius = request.radius;
  out.overrides.block_damage_enabled = request.block_damage_enabled;
  out.overrides.entity_damage_enabled = request.entity_damage_enabled;
  out.overrides.drops_enabled = request.drops_enabled;
  out.overrides.fire_enabled = request.fire_enabled;
  out.overrides.future_sound_enabled = request.future_sound_enabled;
  out.overrides.max_destroyed_blocks = request.max_destroyed_blocks;
  out.overrides.max_rays = request.max_rays;
  out.overrides.max_drops = request.max_drops;
  return out;
}

[[nodiscard]] Vec3f drop_velocity(u64 seed, u32 index) noexcept {
  const u64 mixed =
      (seed + static_cast<u64>(index) * 0x9e3779b97f4a7c15ULL) ^
      0x85ebca6b27d4eb2fULL;
  const auto axis = [mixed](u32 shift) {
    const u32 bits = static_cast<u32>((mixed >> shift) & 0x3ffU);
    return (static_cast<f32>(bits) / 1023.0F) * 0.7F - 0.35F;
  };
  return {axis(0U), 0.2F + std::abs(axis(10U)) * 0.5F, axis(20U)};
}

[[nodiscard]] std::optional<player::ItemStack> drop_stack_for_hit(
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    const world::ExplosionBlockHit& hit) {
  if (!hit.should_drop) {
    return std::nullopt;
  }
  const voxel::BlockVariantRecord* variant = block_registry.variant(hit.old_state);
  if (variant == nullptr) {
    return std::nullopt;
  }
  const voxel::BlockRecord* block = block_registry.block(variant->block_id);
  if (block == nullptr || block->drops.kind == voxel::BlockDropKind::None) {
    return std::nullopt;
  }
  const std::string& item_name =
      block->drops.kind == voxel::BlockDropKind::Item
          ? block->drops.item_name
          : block->name;
  const std::optional<player::ItemID> item_id = item_registry.find_item(item_name);
  if (!item_id.has_value()) {
    return std::nullopt;
  }
  return player::ItemStack{item_id.value(), 1U};
}

} // namespace

command::ExplosionCommandResult ServerRuntimeImpl::create_explosion_from_command(
    const command::ExplosionCommandRequest& request) {
  command::ExplosionCommandResult out;
  out.profile_id = request.profile_id;
  out.origin = request.origin;
  world::WorldRuntime* runtime = world_by_id(request.world_id);
  if (runtime == nullptr) {
    out.message = "Unknown world for explosion.";
    return out;
  }
  if (block_registry_ == nullptr) {
    out.message = "Explosion requires a block registry.";
    return out;
  }
  const BlockPos origin_block{{
      floor_to_i32(request.origin.x),
      floor_to_i32(request.origin.y),
      floor_to_i32(request.origin.z),
  }};
  if (runtime->find_chunk(block_to_chunk(origin_block)) == nullptr) {
    out.message = "Explosion origin is in an unloaded chunk.";
    return out;
  }
  std::optional<world::ExplosionResult> result =
      execute_explosion_request(to_world_request(request));
  if (!result.has_value()) {
    out.message = "Unable to create explosion.";
    return out;
  }
  out.success = true;
  out.profile_id = result->profile_id;
  out.origin = result->origin;
  out.destroyed_blocks = static_cast<u32>(result->destroyed_blocks.size());
  out.affected_entities = static_cast<u32>(result->entity_hits.size());
  return out;
}

std::optional<world::ExplosionResult> ServerRuntimeImpl::execute_explosion_request(
    world::ExplosionRequest request) {
  if (request.world_id.value == 0U) {
    request.world_id = default_world_.id();
  }
  world::WorldRuntime* runtime = world_by_id(request.world_id);
  if (runtime == nullptr || block_registry_ == nullptr) {
    return std::nullopt;
  }
  auto result = world::execute_explosion(*runtime, *block_registry_, request);
  if (!result) {
    return std::nullopt;
  }
  world::ExplosionResult explosion = std::move(result.value());
  spawn_explosion_drops(*runtime, explosion);
  broadcast_explosion_event(*runtime, explosion);
  send_explosion_player_velocities(*runtime, explosion);
  return explosion;
}

void ServerRuntimeImpl::spawn_explosion_drops(
    world::WorldRuntime& runtime,
    const world::ExplosionResult& result) {
  if (!result.drops_enabled || result.max_drops == 0U ||
      block_registry_ == nullptr || item_registry_ == nullptr) {
    return;
  }
  u32 spawned = 0U;
  for (const world::ExplosionBlockHit& hit : result.destroyed_blocks) {
    if (spawned >= result.max_drops) {
      break;
    }
    std::optional<player::ItemStack> stack =
        drop_stack_for_hit(*block_registry_, *item_registry_, hit);
    if (!stack.has_value()) {
      continue;
    }
    (void)runtime.create_dropped_item(
        stack.value(),
        arv::block_center(hit.pos),
        drop_velocity(result.seed, spawned),
        dropped_items_.pickup_delay_seconds,
        true);
    ++spawned;
  }
}

void ServerRuntimeImpl::broadcast_explosion_event(
    world::WorldRuntime& runtime,
    const world::ExplosionResult& result) {
  protocol::ClientboundExplosionEvent packet;
  packet.world_id = runtime.id();
  packet.origin = result.origin;
  packet.profile_id = result.profile_id;
  packet.radius = result.radius;
  packet.seed = result.seed;
  packet.visual_enabled = true;
  packet.future_sound_enabled = result.future_sound_enabled;
  packet.future_sound_id = result.future_sound_id;
  const std::vector<u8> payload =
      protocol::serialize_clientbound_explosion_event(packet);
  const f32 send_radius = std::max(64.0F, result.radius + 32.0F);
  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated ||
        session.connection_state != net::ConnectionState::InGame ||
        session.closing ||
        session.world_id != runtime.id()) {
      return;
    }
    if (const player::PlayerState* player = runtime.find_player(session.player_id);
        player != nullptr &&
        distance_squared(player->position, result.origin) > send_radius * send_radius) {
      return;
    }
    (void)send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundExplosionEvent,
        protocol::ReliabilityClass::UnreliableEvent,
        protocol::NetworkLane::Gameplay,
        payload);
  });
}

void ServerRuntimeImpl::send_explosion_player_velocities(
    world::WorldRuntime& runtime,
    const world::ExplosionResult& result) {
  for (const world::ExplosionEntityHit& hit : result.entity_hits) {
    if (!hit.player_id.has_value()) {
      continue;
    }
    const player::PlayerState* player =
        runtime.find_player(hit.player_id.value());
    if (player == nullptr) {
      continue;
    }
    sessions_.for_each_session([&](const ServerSession& session) {
      if (!session.authenticated ||
          session.connection_state != net::ConnectionState::InGame ||
          session.player_id != hit.player_id.value() ||
          session.world_id != runtime.id()) {
        return;
      }
      send_player_velocity(session, *player);
    });
  }
}

} // namespace arv::server::detail
