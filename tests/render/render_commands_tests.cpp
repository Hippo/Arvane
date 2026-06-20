#include "arv/render/render_commands.hpp"
#include "arv/sched/owner_thread.hpp"

#include <gtest/gtest.h>

#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::usize current_thread_hash() noexcept {
  const arv::usize hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
  return hash == 0U ? 1U : hash;
}

struct RunningRenderThread {
  RunningRenderThread() {
    EXPECT_TRUE(thread.start());
  }

  ~RunningRenderThread() {
    thread.request_stop();
    thread.join();
  }

  arv::sched::RenderThread thread;
};

class RecordingRenderTarget final : public arv::render::IRenderCommandTarget {
public:
  void resize(arv::u32 width, arv::u32 height) override {
    record("resize:" + std::to_string(width) + "x" + std::to_string(height));
  }

  void submit_frame(arv::render::FramePacket /*packet*/) override {
    record("submit_frame");
  }

  void render() override {
    record("render");
    metrics.draw_calls_last_frame = 7U;
    metrics.chunk_draw_calls_last_frame = 5U;
  }

  void begin_shutdown() override {
    record("shutdown");
  }

  void set_chunk_shader_sources(
      std::string /*vertex_source*/,
      std::string /*fragment_source*/) override {
    record("set_chunk_shader_sources");
  }

  void set_entity_shader_sources(
      std::string /*vertex_source*/,
      std::string /*fragment_source*/) override {
    record("set_entity_shader_sources");
  }

  void set_ui_shader_sources(
      std::string /*vertex_source*/,
      std::string /*fragment_source*/) override {
    record("set_ui_shader_sources");
  }

  void set_sky_shader_sources(
      std::string /*vertex_source*/,
      std::string /*fragment_source*/) override {
    record("set_sky_shader_sources");
  }

  void set_particle_shader_sources(
      std::string /*vertex_source*/,
      std::string /*fragment_source*/) override {
    record("set_particle_shader_sources");
  }

  void set_chunk_texture_atlas(arv::render::TextureArrayUploadData /*upload*/) override {
    record("set_chunk_texture_atlas");
  }

  void set_entity_texture_atlas(arv::render::TextureArrayUploadData /*upload*/) override {
    record("set_entity_texture_atlas");
  }

  void set_particle_texture_atlas(arv::render::TextureArrayUploadData /*upload*/) override {
    record("set_particle_texture_atlas");
  }

  void set_ui_font_atlas(arv::render::TextureArrayUploadData /*upload*/) override {
    record("set_ui_font_atlas");
  }

  void set_ui_item_atlas(arv::render::TextureArrayUploadData /*upload*/) override {
    record("set_ui_item_atlas");
  }

  void configure_player_skin_atlas(arv::render::PlayerSkinAtlasConfig /*config*/) override {
    record("configure_player_skin_atlas");
  }

  std::optional<arv::render::TextureRegionHandle> upload_player_skin_texture(
      std::string skin_hash,
      arv::render::TextureUploadData /*upload*/) override {
    record("upload_player_skin_texture:" + skin_hash);
    return uploaded_skin_handle;
  }

  std::optional<arv::render::PlayerSkinTextureSnapshot> player_skin_texture_snapshot(
      std::string_view /*skin_hash*/,
      arv::render::TextureRegionHandle /*handle*/) const override {
    return skin_snapshot;
  }

  bool mark_player_skin_texture_used(
      arv::render::TextureRegionHandle /*handle*/,
      bool /*pinned*/) override {
    record("mark_player_skin_texture_used");
    return mark_used_result;
  }

  void set_entity_model(arv::render::EntityModelAsset /*model*/) override {
    record("set_entity_model");
  }

  void set_entity_animation(arv::render::EntityAnimationAsset /*animation*/) override {
    record("set_entity_animation");
  }

  void clear_entity_models() override {
    record("clear_entity_models");
  }

  void clear_entity_animations() override {
    record("clear_entity_animations");
  }

  void set_chunk_mesh_upload_budget(arv::u32 max_uploads_per_frame) override {
    record("set_chunk_mesh_upload_budget:" + std::to_string(max_uploads_per_frame));
  }

  void enqueue_chunk_mesh_upload(arv::render::ChunkMeshUpload /*upload*/) override {
    record("enqueue_chunk_mesh_upload");
    ++metrics.pending_mesh_upload_count;
  }

  void remove_chunk_mesh(arv::render::ChunkMeshKey /*key*/) override {
    record("remove_chunk_mesh");
  }

  std::vector<arv::render::ChunkMeshUploadReceipt>
  drain_chunk_mesh_upload_receipts() override {
    record("drain_chunk_mesh_upload_receipts");
    return receipts;
  }

  std::vector<arv::render::RenderCommandDiagnostic> drain_diagnostics() override {
    record("drain_diagnostics");
    std::vector<arv::render::RenderCommandDiagnostic> drained;
    drained.swap(diagnostics);
    return drained;
  }

  arv::render::RenderMetricsSnapshot metrics_snapshot() const override {
    return metrics;
  }

  std::vector<std::string> operations;
  std::vector<arv::usize> operation_thread_hashes;
  arv::render::RenderMetricsSnapshot metrics;
  std::optional<arv::render::TextureRegionHandle> uploaded_skin_handle;
  std::optional<arv::render::PlayerSkinTextureSnapshot> skin_snapshot;
  std::vector<arv::render::ChunkMeshUploadReceipt> receipts;
  std::vector<arv::render::RenderCommandDiagnostic> diagnostics;
  bool mark_used_result{true};

private:
  void record(std::string operation) {
    operations.emplace_back(std::move(operation));
    operation_thread_hashes.emplace_back(current_thread_hash());
  }
};

} // namespace

TEST(RenderCommandQueueTests, QueuesCommandsThroughRenderThreadInOrder) {
  const arv::usize main_hash = current_thread_hash();
  RecordingRenderTarget target;
  RunningRenderThread render_thread;
  arv::render::RenderCommandQueue commands{target, render_thread.thread};

  std::thread producer([&commands] {
    commands.resize(640U, 480U);
    commands.set_chunk_mesh_upload_budget(3U);
    commands.remove_chunk_mesh({arv::WorldID{1U}, arv::ChunkSectionCoord{{2, 3, 4}}});
  });
  producer.join();

  commands.flush();

  ASSERT_EQ(target.operations.size(), 3U);
  EXPECT_EQ(target.operations[0], "resize:640x480");
  EXPECT_EQ(target.operations[1], "set_chunk_mesh_upload_budget:3");
  EXPECT_EQ(target.operations[2], "remove_chunk_mesh");
  ASSERT_EQ(target.operation_thread_hashes.size(), 3U);
  EXPECT_NE(target.operation_thread_hashes[0], main_hash);
  EXPECT_EQ(target.operation_thread_hashes[0], render_thread.thread.owner_thread_hash());
  EXPECT_EQ(commands.pending_command_count(), 0U);
}

TEST(RenderCommandQueueTests, SyncReturnValuesAreServedByRenderThread) {
  RecordingRenderTarget target;
  target.metrics.pending_mesh_upload_count = 42U;
  target.uploaded_skin_handle = arv::render::TextureRegionHandle{17U, 2U};
  RunningRenderThread render_thread;
  arv::render::RenderCommandQueue commands{target, render_thread.thread};

  EXPECT_EQ(commands.pending_mesh_upload_count(), 42U);

  const auto skin_upload = commands.upload_player_skin_texture("skin-a", {});
  ASSERT_TRUE(skin_upload.has_value());
  commands.flush();
  ASSERT_FALSE(target.operation_thread_hashes.empty());
  EXPECT_EQ(target.operation_thread_hashes.back(), render_thread.thread.owner_thread_hash());
}

TEST(RenderCommandQueueTests, DrainsDiagnosticsThroughRenderThread) {
  RecordingRenderTarget target;
  target.diagnostics.push_back({.message = "texture upload failed"});
  RunningRenderThread render_thread;
  arv::render::RenderCommandQueue commands{target, render_thread.thread};

  std::vector<arv::render::RenderCommandDiagnostic> diagnostics =
      commands.drain_diagnostics();

  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics[0].message, "texture upload failed");
  EXPECT_TRUE(commands.drain_diagnostics().empty());
  ASSERT_FALSE(target.operation_thread_hashes.empty());
  EXPECT_EQ(target.operation_thread_hashes.back(), render_thread.thread.owner_thread_hash());
}

TEST(RenderCommandQueueTests, FlushesQueuedRenderCommandAndRefreshesMetrics) {
  RecordingRenderTarget target;
  RunningRenderThread render_thread;
  arv::render::RenderCommandQueue commands{target, render_thread.thread};

  commands.render();
  commands.flush();

  EXPECT_EQ(commands.draw_calls_last_frame(), 7U);
  EXPECT_EQ(commands.chunk_draw_calls_last_frame(), 5U);
}

TEST(RenderCommandQueueTests, ShutdownRunsAfterAlreadyQueuedCommands) {
  RecordingRenderTarget target;
  RunningRenderThread render_thread;
  arv::render::RenderCommandQueue commands{target, render_thread.thread};

  commands.resize(800U, 600U);
  commands.render();
  commands.begin_shutdown();

  ASSERT_EQ(target.operations.size(), 3U);
  EXPECT_EQ(target.operations[0], "resize:800x600");
  EXPECT_EQ(target.operations[1], "render");
  EXPECT_EQ(target.operations[2], "shutdown");
}

TEST(RenderCommandQueueTests, RejectsCommandsAfterRenderThreadStops) {
  RecordingRenderTarget target;
  arv::sched::RenderThread render_thread;
  ASSERT_TRUE(render_thread.start());
  arv::render::RenderCommandQueue commands{target, render_thread};

  render_thread.request_stop();
  render_thread.join();

  EXPECT_THROW(commands.resize(320U, 200U), std::runtime_error);
  EXPECT_THROW((void)commands.metrics_snapshot(), std::runtime_error);
  EXPECT_NO_THROW((void)commands.pending_mesh_upload_count());
}
