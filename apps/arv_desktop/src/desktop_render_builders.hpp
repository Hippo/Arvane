#pragma once

#include "asset_helpers.hpp"
#include "desktop_session.hpp"

#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/render/render_types.hpp"

#include <chrono>

namespace arv::desktop {

[[nodiscard]] usize populate_chunk_draw_lists(
    render::FramePacket& frame,
    ChunkViewState& chunk_view,
    const io::ChunkDistanceConfig& distances);

void populate_world_frame_draw_items(
    render::FramePacket& frame,
    DesktopGameSession& session,
    ChunkViewState& chunk_view,
    const EntityRenderAssets& entity_render_assets,
    const HashMap<std::string, render::TextureRegionHandle>& player_skin_regions,
    const io::ChunkDistanceConfig& chunk_distances,
    const io::RenderConfig& render_config,
    bool show_chunk_boundaries,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point app_start_time,
    f32 interpolation_alpha);

} // namespace arv::desktop
