#pragma once

#include "arv/client/block_container_animation.hpp"
#include "arv/client/block_outline_target.hpp"
#include "arv/client/dropped_item_mirror.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/replicated_entity_mirror.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/entity_updates.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/render/render_types.hpp"
#include "arv/voxel/storage.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arv::client {

struct FirstPersonHeldItemPose {
  Vec3f body_position{};
  Quatf body_rotation{};
  Vec3f item_position{};
  Quatf item_rotation{};
  f32 item_scale{1.0F};
  render::EntityBoneRotationOverride hand_rotation_override{};
};

struct FirstPersonSneakVisualState {
  bool initialized{};
  Seconds updated_at{};
  f32 progress{};
};

struct ReplicatedEntityDrawInput {
  EntityTypeID entity_type{};
  Vec3f render_position{};
  Quatf render_rotation{};
  std::span<const protocol::EntityAnimationPlayback> animations{};
  Vec3f head_rotation_degrees{};
  bool has_owning_player{};
  render::EntityModelID model_id{};
  render::TextureRegionHandle player_skin_region{};
  bool first_person{};
  render::CameraSnapshot first_person_camera{};
  f32 first_person_sneak_progress{};
  bool apply_held_item_arm_pose{};
  HeldItemAnimationPose held_item_pose{};
};

struct PlayerEntityModelSelectionInput {
  render::EntityModelID base_model_id{};
  bool slim_model{};
  bool first_person{};
  std::optional<render::TextureRegionHandle> ready_player_skin_region{};
};

struct PlayerEntityModelSelection {
  render::EntityModelID model_id{};
  render::TextureRegionHandle player_skin_region{};
};

struct PlayerEntitySkinInfo {
  bool slim_model{};
  std::string_view skin_hash{};
};

struct ReplicatedEntityFrameCallbacks {
  FunctionRef<std::optional<PlayerEntitySkinInfo>(PlayerID)> player_skin_info{};
  FunctionRef<std::optional<render::TextureRegionHandle>(std::string_view)>
      ready_player_skin{};
};

struct ReplicatedEntityFrameInput {
  std::span<const ReplicatedEntityMirror> mirrors{};
  WorldID world_id{};
  std::optional<EntityNetID> local_entity_id{};
  std::optional<PlayerID> local_player_id{};
  render::CameraSnapshot first_person_camera{};
  bool show_local_player_body{true};
  bool apply_local_held_item_arm_pose{};
  HeldItemAnimationPose held_item_pose{};
  FirstPersonSneakVisualState* first_person_sneak_visual{};
  f32 first_person_sneak_progress{};
  Seconds animation_time_seconds{};
  ReplicatedEntityFrameCallbacks callbacks{};
};

struct ReplicatedEntityFrameResult {
  usize appended{};
  f32 first_person_sneak_progress{};
};

struct BlockAttachedModelSectionEntry {
  SectionLocalPos local{};
  voxel::BlockVariantID variant_id{};
};

void append_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    Vec3f position,
    Quatf rotation,
    f32 scale);

void append_dropped_item_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    std::span<const DroppedItemMirror> dropped_items,
    WorldID world_id,
    Seconds animation_time_seconds);

[[nodiscard]] Vec3f first_person_player_body_position(
    const render::CameraSnapshot& camera,
    f32 sneak_progress = 0.0F) noexcept;

[[nodiscard]] Quatf first_person_player_body_rotation(
    const render::CameraSnapshot& camera) noexcept;

[[nodiscard]] f32 first_person_sneak_target_progress(
    std::span<const protocol::EntityAnimationPlayback> animations) noexcept;

[[nodiscard]] f32 update_first_person_sneak_visual_progress(
    FirstPersonSneakVisualState& state,
    std::span<const protocol::EntityAnimationPlayback> animations,
    Seconds now_seconds) noexcept;

[[nodiscard]] FirstPersonHeldItemPose first_person_held_item_pose(
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {},
    f32 sneak_progress = 0.0F) noexcept;

void apply_first_person_held_item_arm_pose(
    render::EntityDrawItem& player_draw,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {});

[[nodiscard]] PlayerEntityModelSelection select_player_entity_model(
    const EntityRenderAssets& entity_render_assets,
    const PlayerEntityModelSelectionInput& input) noexcept;

[[nodiscard]] render::EntityDrawItem make_replicated_entity_draw_item(
    const EntityRenderAssets& entity_render_assets,
    const ReplicatedEntityDrawInput& input);

[[nodiscard]] ReplicatedEntityFrameResult append_replicated_entity_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const ReplicatedEntityFrameInput& input);

void append_held_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {},
    f32 sneak_progress = 0.0F);

[[nodiscard]] bool block_variant_has_attached_model(
    const EntityRenderAssets& entity_render_assets,
    voxel::BlockVariantID variant_id) noexcept;

void collect_block_attached_model_section_entries(
    const EntityRenderAssets& entity_render_assets,
    std::span<const voxel::BlockVariantID> blocks,
    std::vector<BlockAttachedModelSectionEntry>& out);

void append_block_attached_model_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    voxel::BlockVariantID variant_id,
    BlockPos block,
    const BlockContainerAnimationState* container_animation = nullptr,
    Seconds animation_time_seconds = 0.0F);

[[nodiscard]] usize append_block_attached_model_draws_for_section_entries(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    ChunkSectionCoord coord,
    std::span<const BlockAttachedModelSectionEntry> entries,
    const BlockContainerAnimationState* container_animation = nullptr,
    Seconds animation_time_seconds = 0.0F);

void append_block_outline_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const BlockOutlineTarget& target);

void append_block_break_overlay_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const protocol::BlockBreakProgress& block_break);

} // namespace arv::client
