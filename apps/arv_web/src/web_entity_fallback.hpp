#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/core/math.hpp"

#include <optional>
#include <span>
#include <vector>

namespace arv::web {

struct WebChunkVertex {
  Vec3f position{};
  Vec3f color{};
};

struct WebEntityMarker {
  Vec3f position{};
  EntityTypeID entity_type{};
  bool dead{};
};

[[nodiscard]] std::vector<WebChunkVertex> build_fallback_entity_vertices(
    std::span<const WebEntityMarker> markers);
[[nodiscard]] std::vector<WebEntityMarker> build_visible_fallback_entity_markers(
    std::span<const client::ReplicatedEntityMirror> mirrors,
    WorldID world_id,
    std::optional<EntityNetID> local_entity_id,
    std::optional<PlayerID> local_player_id);

} // namespace arv::web
