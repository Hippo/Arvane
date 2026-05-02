#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/protocol/entity_replication.hpp"

#include <vector>

namespace arv::client {

class ClientDroppedItemMirrorStore {
public:
  void apply_snapshot(
      const protocol::DroppedItemSnapshot& snapshot,
      WorldID current_world);

  [[nodiscard]] usize count() const noexcept;
  [[nodiscard]] const DroppedItemMirror* find(EntityNetID drop_id) const noexcept;
  [[nodiscard]] std::vector<DroppedItemMirror> mirrors() const;

  void clear() noexcept;

private:
  HashMap<EntityNetID, DroppedItemMirror> dropped_items_{};
};

} // namespace arv::client
