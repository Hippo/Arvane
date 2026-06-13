#pragma once

#include "arv/client/client_session.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_updates.hpp"

#include <array>
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

struct Mat4 {
  std::array<float, 16> values{};
};

[[nodiscard]] bool same_chunk_section_coord(ChunkSectionCoord lhs, ChunkSectionCoord rhs) noexcept;
[[nodiscard]] std::vector<WebChunkVertex> build_fallback_chunk_vertices(
    const protocol::ClientboundChunkSectionData& section);
[[nodiscard]] std::vector<WebChunkVertex> build_fallback_entity_vertices(
    std::span<const WebEntityMarker> markers);
[[nodiscard]] std::vector<WebEntityMarker> build_visible_fallback_entity_markers(
    std::span<const client::ClientSessionEntityMirror> mirrors,
    const std::optional<client::ClientSessionWorldJoin>& join);
[[nodiscard]] Mat4 multiply(Mat4 lhs, Mat4 rhs) noexcept;
[[nodiscard]] Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z) noexcept;
[[nodiscard]] Mat4 look_at(Vec3f eye, Vec3f center, Vec3f up) noexcept;

} // namespace arv::web
