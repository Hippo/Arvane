#pragma once

#include "arv/client/block_container_animation.hpp"
#include "arv/client/block_outline_target.hpp"
#include "arv/client/dropped_item_mirror.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/item_rendering.hpp"
#include "arv/client/replicated_entity_mirror.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/render/render_types.hpp"

#include <optional>
#include <span>

namespace arv::client {

enum class ChunkDrawAppendResult {
  Appended,
  WrongWorld,
  Empty,
  OutsideRenderWindow,
  OutsideFrustum
};

struct ChunkDrawAppendOptions {
  FunctionRef<bool(ChunkSectionCoord)> inside_render_window{};
  bool frustum_cull{true};
};

struct ChunkDrawAppendRange {
  usize count{};
  FunctionRef<const render::ChunkDrawItem&(usize)> item_at{};
};

struct ChunkDrawAppendSummary {
  usize appended{};
  usize wrong_world{};
  usize empty{};
  usize outside_render_window{};
  usize outside_frustum{};
};

struct BlockAttachedModelSectionRef {
  WorldID world_id{};
  ChunkSectionCoord coord{};
};

struct BlockAttachedModelSectionRange {
  usize count{};
  FunctionRef<BlockAttachedModelSectionRef(usize)> section_at{};
  FunctionRef<std::span<const BlockAttachedModelSectionEntry>(usize)> entries_at{};
};

struct BlockAttachedModelAppendSummary {
  usize models_appended{};
  usize sections_appended{};
  usize wrong_world{};
  usize empty{};
  usize outside_render_window{};
  usize outside_frustum{};
};

struct ClientEntityFrameCallbacks {
  ReplicatedEntityFrameCallbacks replicated_entities{};
};

struct ClientEntityFrameInput {
  std::span<const ReplicatedEntityMirror> replicated_entities{};
  std::span<const DroppedItemMirror> dropped_items{};
  WorldID world_id{};
  std::optional<EntityNetID> local_entity_id{};
  std::optional<PlayerID> local_player_id{};
  render::CameraSnapshot first_person_camera{};
  bool show_local_player_body{true};
  std::optional<player::ItemStack> visible_held_stack{};
  HeldItemAnimationPose held_item_pose{};
  FirstPersonSneakVisualState* first_person_sneak_visual{};
  BlockContainerAnimationState* block_container_animation{};
  std::optional<protocol::ContainerBlockSource> active_container_block_source{};
  std::optional<BlockOutlineTarget> block_outline_target{};
  protocol::BlockBreakProgress block_break{};
  Seconds animation_time_seconds{};
  bool require_local_entity_for_world_draws{};
  BlockAttachedModelSectionRange block_attached_sections{};
  ChunkDrawAppendOptions block_attached_section_options{};
  ClientEntityFrameCallbacks callbacks{};
};

struct ClientEntityFrameResult {
  usize replicated_entities_appended{};
  usize dropped_items_appended{};
  usize block_attached_models_appended{};
  usize outline_draws_appended{};
  usize break_overlay_draws_appended{};
  bool held_item_appended{};
  f32 first_person_sneak_progress{};
};

[[nodiscard]] ChunkDrawAppendResult append_chunk_draw_item(
    render::FramePacket& frame,
    const render::ChunkDrawItem& item,
    const ChunkDrawAppendOptions& options = {});

[[nodiscard]] ChunkDrawAppendSummary append_chunk_draw_items(
    render::FramePacket& frame,
    ChunkDrawAppendRange items,
    const ChunkDrawAppendOptions& options = {});

[[nodiscard]] ChunkDrawAppendSummary append_chunk_draw_items(
    render::FramePacket& frame,
    std::span<const render::ChunkDrawItem> items,
    const ChunkDrawAppendOptions& options = {});

[[nodiscard]] BlockAttachedModelAppendSummary
append_block_attached_model_draws_for_visible_sections(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    BlockAttachedModelSectionRange sections,
    const BlockContainerAnimationState* container_animation = nullptr,
    Seconds animation_time_seconds = 0.0F,
    const ChunkDrawAppendOptions& options = {});

[[nodiscard]] ClientEntityFrameResult append_client_entity_frame_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const ClientEntityFrameInput& input);

} // namespace arv::client
