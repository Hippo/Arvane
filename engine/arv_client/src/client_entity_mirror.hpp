#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/protocol/entity_replication.hpp"

#include <vector>

namespace arv::client {

class ClientEntityMirrorStore {
public:
  void apply_spawn(protocol::ClientboundEntitySpawn spawn, WorldID current_world);
  void apply_despawn(const protocol::ClientboundEntityDespawn& despawn, WorldID current_world);
  void apply_snapshot(const protocol::ClientboundEntitySnapshot& snapshot, WorldID current_world);

  [[nodiscard]] usize count() const noexcept;
  [[nodiscard]] const ReplicatedEntityMirror* find(EntityNetID entity_id) const noexcept;
  [[nodiscard]] std::vector<ReplicatedEntityMirror> mirrors() const;

  void clear() noexcept;

private:
  HashMap<EntityNetID, ReplicatedEntityMirror> entities_{};
};

} // namespace arv::client
