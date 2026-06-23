#include "arv/render/render_commands.hpp"

namespace arv::render {

RenderCommandQueue::RenderCommandQueue(IRenderCommandTarget& target) : target_(target) {
  publish_metrics_snapshot_on_owner();
}

RenderCommandQueue::RenderCommandQueue(
    IRenderCommandTarget& target,
    IRenderCommandOwnerThread& owner_thread) : target_(target),
                                               owner_thread_(&owner_thread) {
  publish_metrics_snapshot_on_owner();
}

void RenderCommandQueue::attach_owner_thread(IRenderCommandOwnerThread& owner_thread) noexcept {
  owned_owner_thread_.reset();
  owner_thread_ = &owner_thread;
}

void RenderCommandQueue::clear_owner_thread() noexcept {
  owned_owner_thread_.reset();
  owner_thread_ = nullptr;
}

usize RenderCommandQueue::owner_thread_hash() const noexcept {
  return owner_thread_ == nullptr ? 0U : owner_thread_->owner_thread_hash();
}

bool RenderCommandQueue::is_owner_thread() const noexcept {
  return owner_thread_ != nullptr && owner_thread_->is_owner_thread();
}

usize RenderCommandQueue::pending_command_count() const noexcept {
  return pending_command_count_.load(std::memory_order_acquire);
}

void RenderCommandQueue::flush() {
  call_on_owner([](IRenderCommandTarget& target) {
    (void)target;
  });
}

void RenderCommandQueue::refresh_metrics_snapshot() {
  call_on_owner([](IRenderCommandTarget& target) {
    (void)target;
  });
}

RenderMetricsSnapshot RenderCommandQueue::metrics_snapshot() {
  return call_on_owner([](IRenderCommandTarget& target) {
    return target.metrics_snapshot();
  });
}

RenderMetricsSnapshot RenderCommandQueue::cached_metrics_snapshot() const {
  RenderMetricsSnapshot snapshot;
  snapshot.pending_mesh_upload_count =
      metrics_snapshot_.pending_mesh_upload_count.load(std::memory_order_acquire);
  snapshot.mesh_uploads_last_frame =
      metrics_snapshot_.mesh_uploads_last_frame.load(std::memory_order_acquire);
  snapshot.mesh_upload_time_ms_last_frame =
      metrics_snapshot_.mesh_upload_time_ms_last_frame.load(std::memory_order_acquire);
  snapshot.mesh_upload_discards_last_frame =
      metrics_snapshot_.mesh_upload_discards_last_frame.load(std::memory_order_acquire);
  snapshot.draw_calls_last_frame =
      metrics_snapshot_.draw_calls_last_frame.load(std::memory_order_acquire);
  snapshot.chunk_draw_calls_last_frame =
      metrics_snapshot_.chunk_draw_calls_last_frame.load(std::memory_order_acquire);
  snapshot.ui_draw_calls_last_frame =
      metrics_snapshot_.ui_draw_calls_last_frame.load(std::memory_order_acquire);
  snapshot.vertices_submitted_last_frame =
      metrics_snapshot_.vertices_submitted_last_frame.load(std::memory_order_acquire);
  snapshot.triangles_submitted_last_frame =
      metrics_snapshot_.triangles_submitted_last_frame.load(std::memory_order_acquire);
  snapshot.gpu_buffer_uploads_last_frame =
      metrics_snapshot_.gpu_buffer_uploads_last_frame.load(std::memory_order_acquire);
  snapshot.gpu_buffer_upload_bytes_last_frame =
      metrics_snapshot_.gpu_buffer_upload_bytes_last_frame.load(std::memory_order_acquire);
  snapshot.texture_uploads_last_frame =
      metrics_snapshot_.texture_uploads_last_frame.load(std::memory_order_acquire);
  snapshot.texture_upload_bytes_last_frame =
      metrics_snapshot_.texture_upload_bytes_last_frame.load(std::memory_order_acquire);
  snapshot.gpu_profile_context_initialized =
      metrics_snapshot_.gpu_profile_context_initialized.load(std::memory_order_acquire);
  return snapshot;
}

void RenderCommandQueue::resize(u32 width, u32 height) {
  dispatch([width, height](IRenderCommandTarget& target) {
    target.resize(width, height);
  });
}

void RenderCommandQueue::submit_frame(FramePacket packet) {
  dispatch([packet = std::move(packet)](IRenderCommandTarget& target) mutable {
    target.submit_frame(std::move(packet));
  });
}

void RenderCommandQueue::render() {
  dispatch([](IRenderCommandTarget& target) {
    target.render();
  });
}

void RenderCommandQueue::begin_shutdown() {
  call_on_owner([](IRenderCommandTarget& target) {
    target.begin_shutdown();
  });
}

void RenderCommandQueue::set_chunk_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_chunk_shader_sources(std::move(vertex_source), std::move(fragment_source));
      });
}

void RenderCommandQueue::set_entity_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_entity_shader_sources(std::move(vertex_source), std::move(fragment_source));
      });
}

void RenderCommandQueue::set_ui_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_ui_shader_sources(std::move(vertex_source), std::move(fragment_source));
      });
}

void RenderCommandQueue::set_sky_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_sky_shader_sources(std::move(vertex_source), std::move(fragment_source));
      });
}

void RenderCommandQueue::set_particle_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_particle_shader_sources(std::move(vertex_source), std::move(fragment_source));
      });
}

void RenderCommandQueue::set_contact_shadow_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  dispatch(
      [vertex_source = std::move(vertex_source),
       fragment_source = std::move(fragment_source)](IRenderCommandTarget& target) mutable {
        target.set_contact_shadow_shader_sources(
            std::move(vertex_source),
            std::move(fragment_source));
      });
}

void RenderCommandQueue::set_chunk_texture_atlas(TextureArrayUploadData upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.set_chunk_texture_atlas(std::move(upload));
  });
}

void RenderCommandQueue::set_entity_texture_atlas(TextureArrayUploadData upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.set_entity_texture_atlas(std::move(upload));
  });
}

void RenderCommandQueue::set_particle_texture_atlas(TextureArrayUploadData upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.set_particle_texture_atlas(std::move(upload));
  });
}

void RenderCommandQueue::set_ui_font_atlas(TextureArrayUploadData upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.set_ui_font_atlas(std::move(upload));
  });
}

void RenderCommandQueue::set_ui_item_atlas(TextureArrayUploadData upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.set_ui_item_atlas(std::move(upload));
  });
}

void RenderCommandQueue::configure_player_skin_atlas(PlayerSkinAtlasConfig config) {
  dispatch([config](IRenderCommandTarget& target) {
    target.configure_player_skin_atlas(config);
  });
}

std::optional<TextureRegionHandle> RenderCommandQueue::upload_player_skin_texture(
    std::string skin_hash,
    TextureUploadData upload) {
  return call_on_owner(
      [skin_hash = std::move(skin_hash),
       upload = std::move(upload)](IRenderCommandTarget& target) mutable {
        return target.upload_player_skin_texture(std::move(skin_hash), std::move(upload));
      });
}

std::optional<PlayerSkinTextureSnapshot> RenderCommandQueue::player_skin_texture_snapshot(
    std::string_view skin_hash,
    TextureRegionHandle handle) {
  return call_on_owner(
      [skin_hash = std::string{skin_hash}, handle](IRenderCommandTarget& target) {
        return target.player_skin_texture_snapshot(skin_hash, handle);
      });
}

bool RenderCommandQueue::mark_player_skin_texture_used(
    TextureRegionHandle handle,
    bool pinned) {
  return call_on_owner([handle, pinned](IRenderCommandTarget& target) {
    return target.mark_player_skin_texture_used(handle, pinned);
  });
}

void RenderCommandQueue::set_entity_model(EntityModelAsset model) {
  dispatch([model = std::move(model)](IRenderCommandTarget& target) mutable {
    target.set_entity_model(std::move(model));
  });
}

void RenderCommandQueue::set_entity_animation(EntityAnimationAsset animation) {
  dispatch([animation = std::move(animation)](IRenderCommandTarget& target) mutable {
    target.set_entity_animation(std::move(animation));
  });
}

void RenderCommandQueue::clear_entity_models() {
  dispatch([](IRenderCommandTarget& target) {
    target.clear_entity_models();
  });
}

void RenderCommandQueue::clear_entity_animations() {
  dispatch([](IRenderCommandTarget& target) {
    target.clear_entity_animations();
  });
}

void RenderCommandQueue::set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) {
  dispatch([max_uploads_per_frame](IRenderCommandTarget& target) {
    target.set_chunk_mesh_upload_budget(max_uploads_per_frame);
  });
}

void RenderCommandQueue::enqueue_chunk_mesh_upload(ChunkMeshUpload upload) {
  dispatch([upload = std::move(upload)](IRenderCommandTarget& target) mutable {
    target.enqueue_chunk_mesh_upload(std::move(upload));
  });
}

void RenderCommandQueue::remove_chunk_mesh(ChunkMeshKey key) {
  dispatch([key](IRenderCommandTarget& target) {
    target.remove_chunk_mesh(key);
  });
}

std::vector<ChunkMeshUploadReceipt> RenderCommandQueue::drain_chunk_mesh_upload_receipts() {
  return call_on_owner([](IRenderCommandTarget& target) {
    return target.drain_chunk_mesh_upload_receipts();
  });
}

std::vector<RenderCommandDiagnostic> RenderCommandQueue::drain_diagnostics() {
  return call_on_owner([](IRenderCommandTarget& target) {
    return target.drain_diagnostics();
  });
}

usize RenderCommandQueue::pending_mesh_upload_count() {
  return cached_metrics_snapshot().pending_mesh_upload_count;
}

u32 RenderCommandQueue::mesh_uploads_last_frame() {
  return cached_metrics_snapshot().mesh_uploads_last_frame;
}

double RenderCommandQueue::mesh_upload_time_ms_last_frame() {
  return cached_metrics_snapshot().mesh_upload_time_ms_last_frame;
}

u32 RenderCommandQueue::mesh_upload_discards_last_frame() {
  return cached_metrics_snapshot().mesh_upload_discards_last_frame;
}

u32 RenderCommandQueue::draw_calls_last_frame() {
  return cached_metrics_snapshot().draw_calls_last_frame;
}

u32 RenderCommandQueue::chunk_draw_calls_last_frame() {
  return cached_metrics_snapshot().chunk_draw_calls_last_frame;
}

u32 RenderCommandQueue::ui_draw_calls_last_frame() {
  return cached_metrics_snapshot().ui_draw_calls_last_frame;
}

u64 RenderCommandQueue::vertices_submitted_last_frame() {
  return cached_metrics_snapshot().vertices_submitted_last_frame;
}

u64 RenderCommandQueue::triangles_submitted_last_frame() {
  return cached_metrics_snapshot().triangles_submitted_last_frame;
}

u32 RenderCommandQueue::gpu_buffer_uploads_last_frame() {
  return cached_metrics_snapshot().gpu_buffer_uploads_last_frame;
}

u64 RenderCommandQueue::gpu_buffer_upload_bytes_last_frame() {
  return cached_metrics_snapshot().gpu_buffer_upload_bytes_last_frame;
}

u32 RenderCommandQueue::texture_uploads_last_frame() {
  return cached_metrics_snapshot().texture_uploads_last_frame;
}

u64 RenderCommandQueue::texture_upload_bytes_last_frame() {
  return cached_metrics_snapshot().texture_upload_bytes_last_frame;
}

bool RenderCommandQueue::gpu_profile_context_initialized() {
  return cached_metrics_snapshot().gpu_profile_context_initialized;
}

void RenderCommandQueue::dispatch(Command command) {
  if (owner_thread_ == nullptr || owner_thread_->is_owner_thread()) {
    command(target_);
    publish_metrics_snapshot_on_owner();
    return;
  }

  if (!try_enqueue(std::move(command))) {
    throw std::runtime_error{"render command queue rejected command"};
  }
}

bool RenderCommandQueue::try_enqueue(Command command) {
  pending_command_count_.fetch_add(1U, std::memory_order_acq_rel);
  if (!owner_thread_->post([this, command = std::move(command)]() mutable {
        try {
          command(target_);
          publish_metrics_snapshot_on_owner();
          pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
        } catch (...) {
          pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
          throw;
        }
      })) {
    pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
    return false;
  }
  return true;
}

void RenderCommandQueue::publish_metrics_snapshot_on_owner() {
  store_metrics_snapshot(target_.metrics_snapshot());
}

void RenderCommandQueue::store_metrics_snapshot(RenderMetricsSnapshot snapshot) {
  metrics_snapshot_.pending_mesh_upload_count.store(
      snapshot.pending_mesh_upload_count,
      std::memory_order_release);
  metrics_snapshot_.mesh_uploads_last_frame.store(
      snapshot.mesh_uploads_last_frame,
      std::memory_order_release);
  metrics_snapshot_.mesh_upload_time_ms_last_frame.store(
      snapshot.mesh_upload_time_ms_last_frame,
      std::memory_order_release);
  metrics_snapshot_.mesh_upload_discards_last_frame.store(
      snapshot.mesh_upload_discards_last_frame,
      std::memory_order_release);
  metrics_snapshot_.draw_calls_last_frame.store(
      snapshot.draw_calls_last_frame,
      std::memory_order_release);
  metrics_snapshot_.chunk_draw_calls_last_frame.store(
      snapshot.chunk_draw_calls_last_frame,
      std::memory_order_release);
  metrics_snapshot_.ui_draw_calls_last_frame.store(
      snapshot.ui_draw_calls_last_frame,
      std::memory_order_release);
  metrics_snapshot_.vertices_submitted_last_frame.store(
      snapshot.vertices_submitted_last_frame,
      std::memory_order_release);
  metrics_snapshot_.triangles_submitted_last_frame.store(
      snapshot.triangles_submitted_last_frame,
      std::memory_order_release);
  metrics_snapshot_.gpu_buffer_uploads_last_frame.store(
      snapshot.gpu_buffer_uploads_last_frame,
      std::memory_order_release);
  metrics_snapshot_.gpu_buffer_upload_bytes_last_frame.store(
      snapshot.gpu_buffer_upload_bytes_last_frame,
      std::memory_order_release);
  metrics_snapshot_.texture_uploads_last_frame.store(
      snapshot.texture_uploads_last_frame,
      std::memory_order_release);
  metrics_snapshot_.texture_upload_bytes_last_frame.store(
      snapshot.texture_upload_bytes_last_frame,
      std::memory_order_release);
  metrics_snapshot_.gpu_profile_context_initialized.store(
      snapshot.gpu_profile_context_initialized,
      std::memory_order_release);
}

} // namespace arv::render
