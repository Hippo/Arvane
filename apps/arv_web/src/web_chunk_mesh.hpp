#pragma once

#include "arv/client/client_session.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/entity_updates.hpp"

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
  u32 state_flags{};
};

[[nodiscard]] bool same_chunk_section_coord(ChunkSectionCoord lhs, ChunkSectionCoord rhs) noexcept;
[[nodiscard]] std::vector<WebChunkVertex> build_fallback_entity_vertices(
    std::span<const WebEntityMarker> markers);
[[nodiscard]] std::vector<WebEntityMarker> build_visible_fallback_entity_markers(
    std::span<const client::ClientSessionEntityMirror> mirrors,
    const std::optional<client::ClientSessionWorldJoin>& join);

} // namespace arv::web
