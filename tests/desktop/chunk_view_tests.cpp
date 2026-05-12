#include "chunk_view.hpp"
#include "desktop_render_builders.hpp"

#include "arv/render/render_commands.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class NullRenderTarget final : public arv::render::IRenderCommandTarget {
public:
  void resize(arv::u32, arv::u32) override {
  }

  void submit_frame(arv::render::FramePacket) override {
  }

  void render() override {
  }

  void begin_shutdown() override {
  }

  void set_chunk_shader_sources(std::string, std::string) override {
  }

  void set_entity_shader_sources(std::string, std::string) override {
  }

  void set_ui_shader_sources(std::string, std::string) override {
  }

  void set_sky_shader_sources(std::string, std::string) override {
  }

  void set_chunk_texture_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_entity_texture_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_ui_font_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_ui_item_atlas(arv::render::TextureArrayUploadData) override {
  }

  void configure_player_skin_atlas(arv::render::PlayerSkinAtlasConfig) override {
  }

  [[nodiscard]] std::optional<arv::render::TextureRegionHandle>
  upload_player_skin_texture(std::string, arv::render::TextureUploadData) override {
    return std::nullopt;
  }

  [[nodiscard]] std::optional<arv::render::PlayerSkinTextureSnapshot>
  player_skin_texture_snapshot(
      std::string_view,
      arv::render::TextureRegionHandle) const override {
    return std::nullopt;
  }

  [[nodiscard]] bool mark_player_skin_texture_used(
      arv::render::TextureRegionHandle,
      bool) override {
    return false;
  }

  void set_entity_model(arv::render::EntityModelAsset) override {
  }

  void set_entity_animation(arv::render::EntityAnimationAsset) override {
  }

  void clear_entity_models() override {
  }

  void clear_entity_animations() override {
  }

  void set_chunk_mesh_upload_budget(arv::u32) override {
  }

  void enqueue_chunk_mesh_upload(arv::render::ChunkMeshUpload upload) override {
    uploaded_chunk_meshes.push_back({upload.mesh.world_id, upload.mesh.coord});
    if (accept_uploads_immediately) {
      upload_receipts.push_back(
          {uploaded_chunk_meshes.back(),
           upload.mesh.source_version,
           upload.mesh.source_content_version,
           upload.mesh.source_light_version,
           true});
    }
  }

  void remove_chunk_mesh(arv::render::ChunkMeshKey key) override {
    removed_chunk_meshes.push_back(key);
  }

  [[nodiscard]] std::vector<arv::render::ChunkMeshUploadReceipt>
  drain_chunk_mesh_upload_receipts() override {
    std::vector<arv::render::ChunkMeshUploadReceipt> receipts;
    receipts.swap(upload_receipts);
    return receipts;
  }

  [[nodiscard]] arv::render::RenderMetricsSnapshot metrics_snapshot() const override {
    return {};
  }

  std::vector<arv::render::ChunkMeshKey> removed_chunk_meshes;
  std::vector<arv::render::ChunkMeshKey> uploaded_chunk_meshes;
  std::vector<arv::render::ChunkMeshUploadReceipt> upload_receipts;
  bool accept_uploads_immediately{};
};

[[nodiscard]] arv::render::RenderView make_test_view() {
  arv::render::RenderView view;
  view.world_id = arv::WorldID{1};
  view.position = {8.0F, 8.0F, -32.0F};
  view.forward = {0.0F, 0.0F, 1.0F};
  view.right = {1.0F, 0.0F, 0.0F};
  view.up = {0.0F, 1.0F, 0.0F};
  view.near_plane = 0.1F;
  view.far_plane = 128.0F;
  view.fov_y_degrees = 90.0F;
  view.viewport_width = 100;
  view.viewport_height = 100;
  return view;
}

[[nodiscard]] arv::render::ChunkDrawItem draw_item(
    arv::ChunkSectionCoord coord,
    bool opaque,
    bool transparent) {
  arv::render::ChunkDrawItem item;
  item.key = {arv::WorldID{1}, coord};
  item.expected_version = 1;
  item.expected_light_version = 1;
  item.has_opaque_mesh = opaque;
  item.has_transparent_mesh = transparent;
  return item;
}

} // namespace

TEST(ChunkViewTests, PopulateDrawListsFiltersByDistanceFrustumAndPass) {
  arv::desktop::ChunkViewState chunk_view;
  chunk_view.target_center = arv::ChunkSectionCoord{{0, 0, 0}};
  chunk_view.target_min_chunk_y = 0;
  chunk_view.target_max_chunk_y = 0;
  chunk_view.target_render_distance = 4;
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{0, 0, 0}},
      draw_item(arv::ChunkSectionCoord{{0, 0, 0}}, true, false));
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{1, 0, 0}},
      draw_item(arv::ChunkSectionCoord{{1, 0, 0}}, false, true));
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{10, 0, 0}},
      draw_item(arv::ChunkSectionCoord{{10, 0, 0}}, true, true));
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{0, 0, -4}},
      draw_item(arv::ChunkSectionCoord{{0, 0, -4}}, true, true));

  arv::render::FramePacket frame;
  frame.world_view = make_test_view();
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 4;

  EXPECT_EQ(arv::desktop::populate_chunk_draw_lists(frame, chunk_view, distances), 2U);
  ASSERT_EQ(frame.opaque_chunks.size(), 1U);
  EXPECT_EQ(frame.opaque_chunks[0].key.coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
  ASSERT_EQ(frame.transparent_chunks.size(), 1U);
  EXPECT_EQ(frame.transparent_chunks[0].key.coord, (arv::ChunkSectionCoord{{1, 0, 0}}));
  EXPECT_EQ(chunk_view.distance_culled_chunks_last_frame, 1U);
  EXPECT_EQ(chunk_view.frustum_culled_chunks_last_frame, 1U);
}

TEST(ChunkViewTests, RetargetKeepsResidentDrawItemsForLoadedChunkReuse) {
  arv::desktop::ChunkViewState chunk_view;
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{8, 0, 0}},
      draw_item(arv::ChunkSectionCoord{{8, 0, 0}}, true, false));

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 2;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 0;
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build({arv::voxel::make_air_definition()}).value();
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  EXPECT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  EXPECT_NE(
      chunk_view.draw_items.find(arv::ChunkSectionCoord{{8, 0, 0}}),
      chunk_view.draw_items.end());

  arv::render::FramePacket frame;
  frame.world_view = make_test_view();
  EXPECT_EQ(arv::desktop::populate_chunk_draw_lists(frame, chunk_view, distances), 0U);
  EXPECT_TRUE(frame.opaque_chunks.empty());
  EXPECT_EQ(chunk_view.distance_culled_chunks_last_frame, 1U);
}

TEST(ChunkViewTests, RetargetKeepsCandidateSetDeduplicatedOnMovement) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  for (arv::i32 z = -1; z <= 1; ++z) {
    for (arv::i32 x = -1; x <= 2; ++x) {
      (void)world.ensure_chunk_section({{x, 0, z}});
    }
  }

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 1;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 0;
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build({arv::voxel::make_air_definition()}).value();
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));
  EXPECT_EQ(chunk_view.mesh_candidate_set.size(), 9U);

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{1, 0, 0}},
      0,
      0,
      registry,
      render_data));
  EXPECT_EQ(chunk_view.mesh_candidate_set.size(), 9U);
  EXPECT_EQ(chunk_view.mesh_candidates.size(), chunk_view.mesh_candidate_set.size());
  EXPECT_NE(
      chunk_view.mesh_candidate_set.find(arv::ChunkSectionCoord{{2, 0, 0}}),
      chunk_view.mesh_candidate_set.end());
  EXPECT_EQ(
      chunk_view.mesh_candidate_set.find(arv::ChunkSectionCoord{{-1, 0, 0}}),
      chunk_view.mesh_candidate_set.end());
}

TEST(ChunkViewTests, SchedulesMultipleChunkSectionsPerMeshJobBatch) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  for (arv::i32 x = 0; x < 3; ++x) {
    (void)world.ensure_chunk_section({{x, 0, 0}});
  }
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build(
          {arv::voxel::make_air_definition(),
           arv::voxel::make_full_cube_definition("arvane:stone")})
          .value();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  for (arv::i32 x = 0; x < 3; ++x) {
    ASSERT_TRUE(world.set_block({{x * 16 + 1, 1, 1}}, stone));
  }

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  jobs.start();
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 2;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 2;
  pipeline.max_mesh_sections_per_job = 2;
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  EXPECT_EQ(chunk_view.pending_meshes.size(), 2U);
  ASSERT_EQ(chunk_view.pending_meshes[0].entries.size(), 2U);
  ASSERT_EQ(chunk_view.pending_meshes[1].entries.size(), 1U);
  EXPECT_EQ(chunk_view.mesh_in_flight.size(), 3U);
  EXPECT_EQ(chunk_view.mesh_sections_prepared_last_frame, 3U);
  jobs.join();
}

TEST(ChunkViewTests, MeshSchedulingRespectsSectionPreparationBudget) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  for (arv::i32 x = 0; x < 5; ++x) {
    (void)world.ensure_chunk_section({{x, 0, 0}});
  }
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build(
          {arv::voxel::make_air_definition(),
           arv::voxel::make_full_cube_definition("arvane:stone")})
          .value();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  for (arv::i32 x = 0; x < 5; ++x) {
    ASSERT_TRUE(world.set_block({{x * 16 + 1, 1, 1}}, stone));
  }

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  jobs.start();
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 5;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 4;
  pipeline.max_mesh_sections_per_job = 4;
  pipeline.max_mesh_sections_prepared_per_frame = 3;
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  ASSERT_EQ(chunk_view.pending_meshes.size(), 1U);
  EXPECT_EQ(chunk_view.pending_meshes[0].entries.size(), 3U);
  EXPECT_EQ(chunk_view.mesh_in_flight.size(), 3U);
  EXPECT_EQ(chunk_view.mesh_sections_prepared_last_frame, 3U);
  EXPECT_EQ(chunk_view.mesh_candidates.size(), 2U);
  jobs.join();
}

TEST(ChunkViewTests, CompletedMeshWaitsForUploadReceiptBeforeRemeshing) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  (void)world.ensure_chunk_section({{0, 0, 0}});
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build(
          {arv::voxel::make_air_definition(),
           arv::voxel::make_full_cube_definition("arvane:stone")})
          .value();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, stone));

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  jobs.start();
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 1;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 4;
  pipeline.max_mesh_sections_per_job = 4;
  pipeline.max_mesh_sections_prepared_per_frame = 4;
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));
  ASSERT_EQ(chunk_view.pending_meshes.size(), 1U);
  EXPECT_EQ(chunk_view.mesh_in_flight.size(), 1U);

  jobs.join();

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  EXPECT_EQ(target.uploaded_chunk_meshes.size(), 1U);
  EXPECT_TRUE(chunk_view.pending_meshes.empty());
  EXPECT_TRUE(chunk_view.mesh_in_flight.empty());
  EXPECT_TRUE(chunk_view.mesh_candidate_set.empty());

  chunk_view.mesh_candidates.push_back(arv::ChunkSectionCoord{{0, 0, 0}});
  chunk_view.mesh_candidate_set.insert(arv::ChunkSectionCoord{{0, 0, 0}});
  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));
  EXPECT_TRUE(chunk_view.pending_meshes.empty());
  EXPECT_TRUE(chunk_view.mesh_in_flight.empty());
  EXPECT_EQ(target.uploaded_chunk_meshes.size(), 1U);
}

TEST(ChunkViewTests, EmptySectionsPublishWithoutSchedulingMeshJob) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  (void)world.ensure_chunk_section({{0, 0, 0}});

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 1;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 4;
  pipeline.max_mesh_sections_per_job = 4;
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build({arv::voxel::make_air_definition()}).value();
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  EXPECT_TRUE(chunk_view.pending_meshes.empty());
  EXPECT_TRUE(chunk_view.mesh_in_flight.empty());
  EXPECT_EQ(chunk_view.mesh_sections_prepared_last_frame, 1U);
  EXPECT_EQ(chunk_view.empty_mesh_skips_last_frame, 1U);
  EXPECT_TRUE(chunk_view.draw_items.empty());
  ASSERT_EQ(target.removed_chunk_meshes.size(), 1U);
  EXPECT_EQ(target.removed_chunk_meshes[0].coord, (arv::ChunkSectionCoord{{0, 0, 0}}));

  const arv::world::WorldSearch search{world};
  const arv::voxel::ChunkSection* chunk = search.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_FALSE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::Geometry));
  EXPECT_FALSE(chunk->is_dirty(arv::voxel::ChunkDirtyFlags::LightMesh));
}

TEST(ChunkViewTests, EmptySectionsDropResidentDrawItems) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({0, 0});
  world.bind_owner_thread();
  (void)world.ensure_chunk_section({{0, 0, 0}});

  NullRenderTarget target;
  arv::render::RenderCommandQueue renderer{target};
  arv::sched::JobSystem jobs;
  arv::desktop::ChunkViewState chunk_view;
  chunk_view.draw_items.emplace(
      arv::ChunkSectionCoord{{0, 0, 0}},
      draw_item(arv::ChunkSectionCoord{{0, 0, 0}}, true, true));
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = 1;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = 4;
  const arv::voxel::BlockRegistry registry =
      arv::voxel::BlockRegistry::build({arv::voxel::make_air_definition()}).value();
  const arv::voxel::BlockRenderData render_data =
      arv::voxel::BlockRenderData::make_empty(registry.variant_count());

  ASSERT_TRUE(arv::desktop::update_chunk_view(
      world,
      renderer,
      jobs,
      chunk_view,
      distances,
      pipeline,
      arv::ChunkSectionCoord{{0, 0, 0}},
      0,
      0,
      registry,
      render_data));

  EXPECT_EQ(
      chunk_view.draw_items.find(arv::ChunkSectionCoord{{0, 0, 0}}),
      chunk_view.draw_items.end());
  ASSERT_EQ(target.removed_chunk_meshes.size(), 1U);
  EXPECT_EQ(target.removed_chunk_meshes[0].coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
}
