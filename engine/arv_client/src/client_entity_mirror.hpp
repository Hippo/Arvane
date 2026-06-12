#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/protocol/entity_replication.hpp"

#include <optional>
#include <span>
#include <vector>

namespace arv::client {

class ClientEntityMirrorStore {
public:
  void apply_spawn(protocol::ClientboundEntitySpawn spawn, WorldID current_world);
  void apply_despawn(const protocol::ClientboundEntityDespawn& despawn, WorldID current_world);
  void apply_snapshot(
      const protocol::ClientboundEntitySnapshot& snapshot,
      WorldID current_world,
      TickRate server_tick_rate,
      std::optional<EntityNetID> locally_controlled_vehicle);
  void apply_vehicle_state(
      const protocol::ClientboundVehicleState& state,
      WorldID current_world,
      TickRate server_tick_rate,
      bool local_driver_owns_vehicle);
  void apply_vehicle_correction(const protocol::ClientboundVehicleCorrection& correction);
  void apply_local_vehicle_prediction(
      EntityNetID vehicle_id,
      Vec3f position,
      Vec3f velocity,
      Quatf rotation,
      std::span<const protocol::EntityAnimationPlayback> animations);
  void apply_ride_started(const protocol::ClientboundRideStarted& event);
  void apply_ride_ended(const protocol::ClientboundRideEnded& event);
  void apply_passengers_set(const protocol::ClientboundEntityPassengersSet& event);
  void advance_interpolation(Seconds delta_seconds) noexcept;

  [[nodiscard]] usize count() const noexcept;
  [[nodiscard]] const ReplicatedEntityMirror* find(EntityNetID entity_id) const noexcept;
  [[nodiscard]] std::vector<ReplicatedEntityMirror> mirrors() const;

  void prune_except_world(WorldID world_id);
  void clear() noexcept;

private:
  HashMap<EntityNetID, ReplicatedEntityMirror> entities_{};
};

} // namespace arv::client
