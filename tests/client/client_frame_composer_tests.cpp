#include "arv/client/client_frame_composer.hpp"
#include "arv/physics/collision_query.hpp"

#include <gtest/gtest.h>

#include <span>
#include <vector>

namespace {

[[nodiscard]] arv::render::CameraSnapshot make_forward_camera() {
  arv::render::CameraSnapshot camera;
  camera.position = {1.0F, 2.0F, 3.0F};
  camera.forward = {0.0F, 0.0F, 1.0F};
  camera.right = {1.0F, 0.0F, 0.0F};
  camera.up = {0.0F, 1.0F, 0.0F};
  return camera;
}

[[nodiscard]] arv::render::ChunkDrawItem make_chunk_draw_item(
    arv::WorldID world_id,
    arv::ChunkSectionCoord coord,
    bool opaque,
    bool transparent) {
  arv::render::ChunkDrawItem item;
  item.key = {world_id, coord};
  item.has_opaque_mesh = opaque;
  item.has_transparent_mesh = transparent;
  return item;
}

class FlatGroundQuery final : public arv::physics::ICollisionQuery {
public:
  explicit FlatGroundQuery(bool has_ground = true) : has_ground_(has_ground) {
    ground_body_.boxes.push_back({{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}});
  }

  [[nodiscard]] bool is_block_loaded(arv::BlockPos) const noexcept override {
    return true;
  }

  [[nodiscard]] arv::voxel::BlockVariantID block_variant(
      arv::BlockPos pos) const noexcept override {
    return has_ground_ && pos.value.y == 0
               ? arv::voxel::BlockVariantID{1U}
               : arv::voxel::AirBlockVariantID;
  }

  [[nodiscard]] const arv::voxel::BlockBody* block_body(
      arv::BlockPos pos) const noexcept override {
    return has_ground_ && pos.value.y == 0 ? &ground_body_ : nullptr;
  }

  [[nodiscard]] bool missing_blocks_are_solid() const noexcept override {
    return false;
  }

private:
  bool has_ground_{true};
  arv::voxel::BlockBody ground_body_{};
};

} // namespace

TEST(ClientFrameComposerTests, AppendsSharedEntityFrameLayersInOrder) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.model_by_item.emplace(
      arv::player::ItemID{1U},
      arv::render::EntityModelID{20U});
  assets.model_by_item.emplace(
      arv::player::ItemID{2U},
      arv::render::EntityModelID{21U});
  assets.outline_model_by_variant.resize(3U);
  assets.outline_model_by_variant[2U] = arv::render::EntityModelID{30U};
  assets.break_overlay_model_by_variant.resize(3U);
  assets.break_overlay_model_by_variant[1U][4U] =
      arv::render::EntityModelID{40U};
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{50U};

  std::vector<arv::client::ReplicatedEntityMirror> replicated(1U);
  replicated.front().entity_id = arv::EntityNetID{7U};
  replicated.front().entity_type = arv::EntityTypeID{4U};
  replicated.front().world_id = arv::WorldID{5U};
  replicated.front().animations.push_back({"sneak_idle", 1.0F});

  const std::vector<arv::client::DroppedItemMirror> dropped{
      {.world_id = arv::WorldID{5U},
       .render_position = {3.0F, 4.0F, 5.0F},
       .stack = {arv::player::ItemID{2U}, 3U}}};

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  frame.camera = make_forward_camera();

  arv::client::FirstPersonSneakVisualState sneak;
  arv::client::BlockContainerAnimationState block_animation;
  const arv::protocol::ContainerBlockSource active_source{
      arv::WorldID{5U},
      arv::BlockPos{{4, 5, 6}}};
  const std::vector<arv::client::BlockAttachedModelSectionEntry>
      block_attached_entries{
          {arv::SectionLocalPos{{4, 5, 6}}, arv::voxel::BlockVariantID{2U}}};
  const auto block_attached_section_at =
      [](arv::usize) -> arv::client::BlockAttachedModelSectionRef {
    return {
        .world_id = arv::WorldID{5U},
        .coord = arv::ChunkSectionCoord{{0, 0, 0}}};
  };
  const auto block_attached_entries_at =
      [&](arv::usize)
          -> std::span<const arv::client::BlockAttachedModelSectionEntry> {
    return block_attached_entries;
  };

  arv::protocol::BlockBreakProgress block_break;
  block_break.active = true;
  block_break.block = arv::BlockPos{{6, 7, 8}};
  block_break.variant = arv::voxel::BlockVariantID{1U};
  block_break.stage = 4U;

  const arv::client::ClientEntityFrameResult result =
      arv::client::append_client_entity_frame_draws(
          frame,
          assets,
          {.replicated_entities =
               std::span<const arv::client::ReplicatedEntityMirror>{replicated},
           .dropped_items =
               std::span<const arv::client::DroppedItemMirror>{dropped},
           .world_id = arv::WorldID{5U},
           .local_entity_id = arv::EntityNetID{7U},
           .first_person_camera = frame.camera,
           .visible_held_stack =
               arv::player::ItemStack{arv::player::ItemID{1U}, 1U},
           .first_person_sneak_visual = &sneak,
           .block_container_animation = &block_animation,
           .active_container_block_source = active_source,
           .block_outline_target =
               arv::client::BlockOutlineTarget{
                   .block = arv::BlockPos{{9, 10, 11}},
                   .variant = arv::voxel::BlockVariantID{2U}},
           .block_break = block_break,
           .animation_time_seconds = 2.0F,
           .block_attached_sections =
               {.count = 1U,
                .section_at =
                    arv::FunctionRef<arv::client::BlockAttachedModelSectionRef(
                        arv::usize)>{block_attached_section_at},
                .entries_at =
                    arv::FunctionRef<std::span<
                        const arv::client::BlockAttachedModelSectionEntry>(
                        arv::usize)>{block_attached_entries_at}},
           .block_attached_section_options = {.frustum_cull = false},
           .callbacks =
               {}});

  EXPECT_EQ(result.replicated_entities_appended, 1U);
  EXPECT_EQ(result.dropped_items_appended, 1U);
  EXPECT_EQ(result.block_attached_models_appended, 1U);
  EXPECT_EQ(result.outline_draws_appended, 1U);
  EXPECT_EQ(result.break_overlay_draws_appended, 1U);
  EXPECT_TRUE(result.held_item_appended);
  EXPECT_FLOAT_EQ(result.first_person_sneak_progress, 1.0F);
  ASSERT_EQ(frame.entities.size(), 6U);
  EXPECT_EQ(frame.entities[0].model_id, (arv::render::EntityModelID{10U}));
  EXPECT_EQ(frame.entities[1].model_id, (arv::render::EntityModelID{21U}));
  EXPECT_EQ(frame.entities[2].model_id, (arv::render::EntityModelID{50U}));
  EXPECT_EQ(frame.entities[3].model_id, (arv::render::EntityModelID{30U}));
  EXPECT_EQ(frame.entities[4].model_id, (arv::render::EntityModelID{40U}));
  EXPECT_EQ(frame.entities[5].model_id, (arv::render::EntityModelID{20U}));
}

TEST(ClientFrameComposerTests, AppendsContactShadowsForVisibleGameplayCasters) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.model_by_item.emplace(
      arv::player::ItemID{2U},
      arv::render::EntityModelID{20U});
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{30U};

  std::vector<arv::client::ReplicatedEntityMirror> replicated(1U);
  replicated.front().entity_id = arv::EntityNetID{7U};
  replicated.front().entity_type = arv::EntityTypeID{4U};
  replicated.front().world_id = arv::WorldID{5U};
  replicated.front().render_position = {1.5F, 1.0F, 1.5F};

  const std::vector<arv::client::DroppedItemMirror> dropped{
      {.world_id = arv::WorldID{5U},
       .render_position = {3.0F, 1.0F, 3.0F},
       .stack = {arv::player::ItemID{2U}, 1U}}};

  const std::vector<arv::client::BlockAttachedModelSectionEntry>
      block_attached_entries{
          {arv::SectionLocalPos{{4, 5, 6}}, arv::voxel::BlockVariantID{2U}}};
  const auto block_attached_section_at =
      [](arv::usize) -> arv::client::BlockAttachedModelSectionRef {
    return {
        .world_id = arv::WorldID{5U},
        .coord = arv::ChunkSectionCoord{{0, 0, 0}}};
  };
  const auto block_attached_entries_at =
      [&](arv::usize)
          -> std::span<const arv::client::BlockAttachedModelSectionEntry> {
    return block_attached_entries;
  };
  FlatGroundQuery query;
  const auto replicated_bounds =
      [](const arv::client::ReplicatedEntityMirror& mirror) -> arv::Aabb3f {
    return {
        {mirror.render_position.x - 0.4F,
         mirror.render_position.y,
         mirror.render_position.z - 0.5F},
        {mirror.render_position.x + 0.4F,
         mirror.render_position.y + 1.6F,
         mirror.render_position.z + 0.5F}};
  };

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  frame.camera = make_forward_camera();

  const arv::client::ClientEntityFrameResult result =
      arv::client::append_client_entity_frame_draws(
          frame,
          assets,
          {.replicated_entities =
               std::span<const arv::client::ReplicatedEntityMirror>{replicated},
           .dropped_items =
               std::span<const arv::client::DroppedItemMirror>{dropped},
           .world_id = arv::WorldID{5U},
           .first_person_camera = frame.camera,
           .block_attached_sections =
               {.count = 1U,
                .section_at =
                    arv::FunctionRef<arv::client::BlockAttachedModelSectionRef(
                        arv::usize)>{block_attached_section_at},
                .entries_at =
                    arv::FunctionRef<std::span<
                        const arv::client::BlockAttachedModelSectionEntry>(
                        arv::usize)>{block_attached_entries_at}},
           .block_attached_section_options = {.frustum_cull = false},
           .contact_shadows =
               {.enabled = true,
                .collision_query = &query,
                .replicated_entity_bounds =
                    arv::FunctionRef<arv::Aabb3f(
                        const arv::client::ReplicatedEntityMirror&)>{
                        replicated_bounds}},
           .callbacks =
               {}});

  EXPECT_EQ(result.replicated_entities_appended, 1U);
  EXPECT_EQ(result.dropped_items_appended, 1U);
  EXPECT_EQ(result.block_attached_models_appended, 1U);
  EXPECT_EQ(result.contact_shadows_appended, 3U);
  ASSERT_EQ(frame.contact_shadows.size(), 3U);
  EXPECT_EQ(frame.contact_shadows[0].world_id, (arv::WorldID{5U}));
  EXPECT_NEAR(frame.contact_shadows[0].center.y, 1.015F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[0].half_extents.x, 0.4F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[0].half_extents.y, 0.5F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[0].alpha, 0.32F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[1].center.y, 1.015F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[1].alpha, 0.22F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[2].center.y, 5.015F, 0.0001F);
  EXPECT_NEAR(frame.contact_shadows[2].alpha, 0.18F, 0.0001F);
}

TEST(ClientFrameComposerTests, ContactShadowsSkipDisabledNoQueryAndNoGroundCases) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.model_by_item.emplace(
      arv::player::ItemID{2U},
      arv::render::EntityModelID{20U});

  std::vector<arv::client::ReplicatedEntityMirror> replicated(1U);
  replicated.front().entity_type = arv::EntityTypeID{4U};
  replicated.front().world_id = arv::WorldID{5U};
  replicated.front().render_position = {1.5F, 1.0F, 1.5F};
  const std::vector<arv::client::DroppedItemMirror> dropped{
      {.world_id = arv::WorldID{5U},
       .render_position = {3.0F, 1.0F, 3.0F},
       .stack = {arv::player::ItemID{2U}, 1U}}};

  const auto append_with_options =
      [&](arv::client::ContactShadowOptions options) {
    arv::render::FramePacket frame;
    frame.world_view.world_id = arv::WorldID{5U};
    frame.camera = make_forward_camera();
    const arv::client::ClientEntityFrameResult result =
        arv::client::append_client_entity_frame_draws(
            frame,
            assets,
            {.replicated_entities =
                 std::span<const arv::client::ReplicatedEntityMirror>{replicated},
             .dropped_items =
                 std::span<const arv::client::DroppedItemMirror>{dropped},
             .world_id = arv::WorldID{5U},
             .first_person_camera = frame.camera,
             .contact_shadows = options,
             .callbacks =
                 {}});
    EXPECT_EQ(result.contact_shadows_appended, 0U);
    EXPECT_TRUE(frame.contact_shadows.empty());
  };

  FlatGroundQuery query;
  append_with_options({.enabled = false, .collision_query = &query});
  append_with_options({.enabled = true});
  FlatGroundQuery no_ground(false);
  append_with_options({.enabled = true, .collision_query = &no_ground});
}

TEST(ClientFrameComposerTests, ContactShadowsHonorPerFrameCap) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.model_by_item.emplace(
      arv::player::ItemID{2U},
      arv::render::EntityModelID{20U});

  std::vector<arv::client::ReplicatedEntityMirror> replicated(1U);
  replicated.front().entity_type = arv::EntityTypeID{4U};
  replicated.front().world_id = arv::WorldID{5U};
  replicated.front().render_position = {1.5F, 1.0F, 1.5F};
  const std::vector<arv::client::DroppedItemMirror> dropped{
      {.world_id = arv::WorldID{5U},
       .render_position = {3.0F, 1.0F, 3.0F},
       .stack = {arv::player::ItemID{2U}, 1U}}};
  FlatGroundQuery query;

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  frame.camera = make_forward_camera();
  const arv::client::ClientEntityFrameResult result =
      arv::client::append_client_entity_frame_draws(
          frame,
          assets,
          {.replicated_entities =
               std::span<const arv::client::ReplicatedEntityMirror>{replicated},
           .dropped_items =
               std::span<const arv::client::DroppedItemMirror>{dropped},
           .world_id = arv::WorldID{5U},
           .first_person_camera = frame.camera,
           .contact_shadows =
               {.enabled = true,
                .collision_query = &query,
                .max_shadows_per_frame = 1U},
           .callbacks =
               {}});

  EXPECT_EQ(result.contact_shadows_appended, 1U);
  ASSERT_EQ(frame.contact_shadows.size(), 1U);
  EXPECT_NEAR(frame.contact_shadows.front().alpha, 0.32F, 0.0001F);
}

TEST(ClientFrameComposerTests, AppendBlockAttachedVisibleSectionsSummarizesResults) {
  arv::client::EntityRenderAssets assets;
  assets.block_attached_model_by_variant.resize(3U);
  assets.block_attached_model_by_variant[2U] =
      arv::render::EntityModelID{22U};

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  const std::vector<arv::client::BlockAttachedModelSectionRef> sections{
      {arv::WorldID{5U}, arv::ChunkSectionCoord{{1, 0, 0}}},
      {arv::WorldID{6U}, arv::ChunkSectionCoord{{2, 0, 0}}},
      {arv::WorldID{5U}, arv::ChunkSectionCoord{{12, 0, 0}}},
      {arv::WorldID{5U}, arv::ChunkSectionCoord{{3, 0, 0}}},
      {arv::WorldID{5U}, arv::ChunkSectionCoord{{4, 0, 0}}}};
  const std::vector<std::vector<arv::client::BlockAttachedModelSectionEntry>>
      entries{
          {{arv::SectionLocalPos{{1, 0, 0}}, arv::voxel::BlockVariantID{2U}}},
          {{arv::SectionLocalPos{{2, 0, 0}}, arv::voxel::BlockVariantID{2U}}},
          {{arv::SectionLocalPos{{3, 0, 0}}, arv::voxel::BlockVariantID{2U}}},
          {},
          {{arv::SectionLocalPos{{4, 0, 0}}, arv::voxel::BlockVariantID{2U}},
           {arv::SectionLocalPos{{5, 0, 0}}, arv::voxel::BlockVariantID{2U}}}};
  std::vector<arv::usize> entries_requested;

  const auto section_at =
      [&](arv::usize index) -> arv::client::BlockAttachedModelSectionRef {
    return sections[index];
  };
  const auto entries_at =
      [&](arv::usize index)
          -> std::span<const arv::client::BlockAttachedModelSectionEntry> {
    entries_requested.push_back(index);
    return entries[index];
  };
  const auto inside_window =
      [](arv::ChunkSectionCoord coord) {
    return coord.value.x < 10;
  };

  const arv::client::BlockAttachedModelAppendSummary summary =
      arv::client::append_block_attached_model_draws_for_visible_sections(
          frame,
          assets,
          {.count = sections.size(),
           .section_at =
               arv::FunctionRef<arv::client::BlockAttachedModelSectionRef(
                   arv::usize)>{section_at},
           .entries_at =
               arv::FunctionRef<std::span<
                   const arv::client::BlockAttachedModelSectionEntry>(
                   arv::usize)>{entries_at}},
          nullptr,
          0.0F,
          {.inside_render_window =
               arv::FunctionRef<bool(arv::ChunkSectionCoord)>{inside_window},
           .frustum_cull = false});

  EXPECT_EQ(summary.models_appended, 3U);
  EXPECT_EQ(summary.sections_appended, 2U);
  EXPECT_EQ(summary.wrong_world, 1U);
  EXPECT_EQ(summary.empty, 1U);
  EXPECT_EQ(summary.outside_render_window, 1U);
  EXPECT_EQ(summary.outside_frustum, 0U);
  const std::vector<arv::usize> expected_entries_requested{0U, 3U, 4U};
  EXPECT_EQ(entries_requested, expected_entries_requested);
  ASSERT_EQ(frame.entities.size(), 3U);
  EXPECT_EQ(frame.entities[0].model_id, (arv::render::EntityModelID{22U}));
  EXPECT_EQ(frame.entities[1].model_id, (arv::render::EntityModelID{22U}));
  EXPECT_EQ(frame.entities[2].model_id, (arv::render::EntityModelID{22U}));
}

TEST(ClientFrameComposerTests, AppendChunkDrawItemAppendsVisibleMeshes) {
  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  arv::render::ChunkDrawItem item;
  item.key = {arv::WorldID{5U}, arv::ChunkSectionCoord{{1, 2, 3}}};
  item.has_opaque_mesh = true;
  item.has_transparent_mesh = true;

  arv::client::ChunkDrawAppendOptions options;
  options.frustum_cull = false;
  const arv::client::ChunkDrawAppendResult result =
      arv::client::append_chunk_draw_item(frame, item, options);

  EXPECT_EQ(result, arv::client::ChunkDrawAppendResult::Appended);
  ASSERT_EQ(frame.opaque_chunks.size(), 1U);
  ASSERT_EQ(frame.transparent_chunks.size(), 1U);
  EXPECT_EQ(frame.opaque_chunks.front().key, item.key);
  EXPECT_EQ(frame.transparent_chunks.front().key, item.key);
}

TEST(ClientFrameComposerTests, AppendChunkDrawItemReportsCullReason) {
  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  arv::client::ChunkDrawAppendOptions options;
  options.frustum_cull = false;

  arv::render::ChunkDrawItem wrong_world;
  wrong_world.key = {arv::WorldID{6U}, arv::ChunkSectionCoord{{1, 2, 3}}};
  wrong_world.has_opaque_mesh = true;
  EXPECT_EQ(
      arv::client::append_chunk_draw_item(frame, wrong_world, options),
      arv::client::ChunkDrawAppendResult::WrongWorld);

  arv::render::ChunkDrawItem empty;
  empty.key = {arv::WorldID{5U}, arv::ChunkSectionCoord{{1, 2, 3}}};
  EXPECT_EQ(
      arv::client::append_chunk_draw_item(frame, empty, options),
      arv::client::ChunkDrawAppendResult::Empty);

  const auto outside_window =
      [](arv::ChunkSectionCoord) {
    return false;
  };
  options.inside_render_window =
      arv::FunctionRef<bool(arv::ChunkSectionCoord)>{outside_window};
  arv::render::ChunkDrawItem outside;
  outside.key = {arv::WorldID{5U}, arv::ChunkSectionCoord{{4, 5, 6}}};
  outside.has_transparent_mesh = true;
  EXPECT_EQ(
      arv::client::append_chunk_draw_item(frame, outside, options),
      arv::client::ChunkDrawAppendResult::OutsideRenderWindow);
  EXPECT_TRUE(frame.opaque_chunks.empty());
  EXPECT_TRUE(frame.transparent_chunks.empty());
}

TEST(ClientFrameComposerTests, AppendChunkDrawItemsSummarizesBatchResults) {
  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  std::vector<arv::render::ChunkDrawItem> items;
  items.push_back(make_chunk_draw_item(
      arv::WorldID{5U},
      arv::ChunkSectionCoord{{1, 0, 0}},
      true,
      false));
  items.push_back(make_chunk_draw_item(
      arv::WorldID{5U},
      arv::ChunkSectionCoord{{2, 0, 0}},
      false,
      true));
  items.push_back(make_chunk_draw_item(
      arv::WorldID{6U},
      arv::ChunkSectionCoord{{3, 0, 0}},
      true,
      false));
  items.push_back(make_chunk_draw_item(
      arv::WorldID{5U},
      arv::ChunkSectionCoord{{4, 0, 0}},
      false,
      false));
  items.push_back(make_chunk_draw_item(
      arv::WorldID{5U},
      arv::ChunkSectionCoord{{12, 0, 0}},
      true,
      false));

  const auto inside_window =
      [](arv::ChunkSectionCoord coord) {
    return coord.value.x < 10;
  };
  arv::client::ChunkDrawAppendOptions options;
  options.frustum_cull = false;
  options.inside_render_window =
      arv::FunctionRef<bool(arv::ChunkSectionCoord)>{inside_window};

  const arv::client::ChunkDrawAppendSummary summary =
      arv::client::append_chunk_draw_items(
          frame,
          std::span<const arv::render::ChunkDrawItem>{items},
          options);

  EXPECT_EQ(summary.appended, 2U);
  EXPECT_EQ(summary.wrong_world, 1U);
  EXPECT_EQ(summary.empty, 1U);
  EXPECT_EQ(summary.outside_render_window, 1U);
  EXPECT_EQ(summary.outside_frustum, 0U);
  ASSERT_EQ(frame.opaque_chunks.size(), 1U);
  ASSERT_EQ(frame.transparent_chunks.size(), 1U);
  EXPECT_EQ(frame.opaque_chunks.front().key.coord, (arv::ChunkSectionCoord{{1, 0, 0}}));
  EXPECT_EQ(frame.transparent_chunks.front().key.coord, (arv::ChunkSectionCoord{{2, 0, 0}}));
}

TEST(ClientFrameComposerTests, AppendChunkDrawItemsSupportsIndexedRange) {
  struct WrappedChunkDraw {
    arv::render::ChunkDrawItem item;
  };

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  const std::vector<WrappedChunkDraw> items{
      {make_chunk_draw_item(
          arv::WorldID{5U},
          arv::ChunkSectionCoord{{1, 0, 0}},
          true,
          true)}};
  const auto item_at =
      [&](arv::usize index) -> const arv::render::ChunkDrawItem& {
    return items[index].item;
  };

  arv::client::ChunkDrawAppendOptions options;
  options.frustum_cull = false;
  const arv::client::ChunkDrawAppendSummary summary =
      arv::client::append_chunk_draw_items(
          frame,
          {.count = items.size(),
           .item_at =
               arv::FunctionRef<const arv::render::ChunkDrawItem&(arv::usize)>{
                   item_at}},
          options);

  EXPECT_EQ(summary.appended, 1U);
  EXPECT_EQ(frame.opaque_chunks.size(), 1U);
  EXPECT_EQ(frame.transparent_chunks.size(), 1U);
}

TEST(ClientFrameComposerTests, RequireLocalEntitySkipsWorldOnlyLayers) {
  arv::client::EntityRenderAssets assets;
  assets.model_by_type.emplace(
      arv::EntityTypeID{4U},
      arv::render::EntityModelID{10U});
  assets.model_by_item.emplace(
      arv::player::ItemID{1U},
      arv::render::EntityModelID{20U});

  std::vector<arv::client::ReplicatedEntityMirror> replicated(1U);
  replicated.front().entity_id = arv::EntityNetID{8U};
  replicated.front().entity_type = arv::EntityTypeID{4U};
  replicated.front().world_id = arv::WorldID{5U};

  const std::vector<arv::client::DroppedItemMirror> dropped{
      {.world_id = arv::WorldID{5U},
       .render_position = {3.0F, 4.0F, 5.0F},
       .stack = {arv::player::ItemID{1U}, 1U}}};

  arv::render::FramePacket frame;
  frame.world_view.world_id = arv::WorldID{5U};
  frame.camera = make_forward_camera();
  bool block_attached_range_called = false;
  const auto block_attached_section_at =
      [&](arv::usize) -> arv::client::BlockAttachedModelSectionRef {
    block_attached_range_called = true;
    return {
        .world_id = arv::WorldID{5U},
        .coord = arv::ChunkSectionCoord{{0, 0, 0}}};
  };
  const auto block_attached_entries_at =
      [&](arv::usize)
          -> std::span<const arv::client::BlockAttachedModelSectionEntry> {
    block_attached_range_called = true;
    return {};
  };

  const arv::client::ClientEntityFrameResult result =
      arv::client::append_client_entity_frame_draws(
          frame,
          assets,
          {.replicated_entities =
               std::span<const arv::client::ReplicatedEntityMirror>{replicated},
           .dropped_items =
               std::span<const arv::client::DroppedItemMirror>{dropped},
           .world_id = arv::WorldID{5U},
           .first_person_camera = frame.camera,
           .visible_held_stack =
               arv::player::ItemStack{arv::player::ItemID{1U}, 1U},
           .require_local_entity_for_world_draws = true,
           .block_attached_sections =
               {.count = 1U,
                .section_at =
                    arv::FunctionRef<arv::client::BlockAttachedModelSectionRef(
                        arv::usize)>{block_attached_section_at},
                .entries_at =
                    arv::FunctionRef<std::span<
                        const arv::client::BlockAttachedModelSectionEntry>(
                        arv::usize)>{block_attached_entries_at}},
           .callbacks =
               {}});

  EXPECT_FALSE(block_attached_range_called);
  EXPECT_EQ(result.replicated_entities_appended, 1U);
  EXPECT_EQ(result.dropped_items_appended, 0U);
  EXPECT_EQ(result.block_attached_models_appended, 0U);
  EXPECT_FALSE(result.held_item_appended);
  ASSERT_EQ(frame.entities.size(), 1U);
  EXPECT_EQ(frame.entities.front().model_id, (arv::render::EntityModelID{10U}));
}
