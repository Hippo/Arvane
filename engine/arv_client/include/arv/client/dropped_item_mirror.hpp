#pragma once

#include "arv/core/time.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/entity_replication.hpp"

#include <vector>

namespace arv::client {

struct DroppedItemMirror {
  EntityNetID drop_id{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f render_position{};
  Vec3f interpolation_start_position{};
  player::ItemStack stack{};
  u64 last_server_tick{};
  u64 last_position_change_server_tick{};
  Seconds interpolation_elapsed_seconds{};
  Seconds interpolation_duration_seconds{};
};

class ClientDroppedItemMirrorStore {
public:
  void apply_snapshot(
      const protocol::ClientboundDroppedItemSnapshot& snapshot,
      WorldID current_world,
      TickRate server_tick_rate);
  void advance_interpolation(Seconds delta_seconds) noexcept;

  [[nodiscard]] usize count() const noexcept;
  [[nodiscard]] const DroppedItemMirror* find(EntityNetID drop_id) const noexcept;
  [[nodiscard]] std::vector<DroppedItemMirror> mirrors() const;

  void clear() noexcept;

private:
  HashMap<EntityNetID, DroppedItemMirror> dropped_items_{};
};

} // namespace arv::client
