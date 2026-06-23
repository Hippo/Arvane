#pragma once

#include "arv/core/types.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace arv::sched {
class OwnerThread;
}

namespace arv::render {

struct RenderMetricsSnapshot {
  usize pending_mesh_upload_count{};
  u32 mesh_uploads_last_frame{};
  double mesh_upload_time_ms_last_frame{};
  u32 mesh_upload_discards_last_frame{};
  u32 draw_calls_last_frame{};
  u32 chunk_draw_calls_last_frame{};
  u32 ui_draw_calls_last_frame{};
  u64 vertices_submitted_last_frame{};
  u64 triangles_submitted_last_frame{};
  u32 gpu_buffer_uploads_last_frame{};
  u64 gpu_buffer_upload_bytes_last_frame{};
  u32 texture_uploads_last_frame{};
  u64 texture_upload_bytes_last_frame{};
  bool gpu_profile_context_initialized{};
};

struct RenderCommandDiagnostic {
  std::string message{};
};

struct PlayerSkinTextureSnapshot {
  AtlasRegion region{};
  u16 atlas_width{};
  u16 atlas_height{};
  u16 image_width{};
  u16 image_height{};
};

class IRenderCommandTarget {
public:
  virtual ~IRenderCommandTarget() = default;

  virtual void resize(u32 width, u32 height) = 0;
  virtual void submit_frame(FramePacket packet) = 0;
  virtual void render() = 0;
  virtual void begin_shutdown() = 0;

  virtual void set_chunk_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;
  virtual void set_entity_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;
  virtual void set_ui_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;
  virtual void set_sky_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;
  virtual void set_particle_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;
  virtual void set_contact_shadow_shader_sources(
      std::string vertex_source,
      std::string fragment_source) = 0;

  virtual void set_chunk_texture_atlas(TextureArrayUploadData upload) = 0;
  virtual void set_entity_texture_atlas(TextureArrayUploadData upload) = 0;
  virtual void set_particle_texture_atlas(TextureArrayUploadData upload) = 0;
  virtual void set_ui_font_atlas(TextureArrayUploadData upload) = 0;
  virtual void set_ui_item_atlas(TextureArrayUploadData upload) = 0;

  virtual void configure_player_skin_atlas(PlayerSkinAtlasConfig config) = 0;
  [[nodiscard]] virtual std::optional<TextureRegionHandle> upload_player_skin_texture(
      std::string skin_hash,
      TextureUploadData upload) = 0;
  [[nodiscard]] virtual std::optional<PlayerSkinTextureSnapshot> player_skin_texture_snapshot(
      std::string_view skin_hash,
      TextureRegionHandle handle) const = 0;
  [[nodiscard]] virtual bool mark_player_skin_texture_used(
      TextureRegionHandle handle,
      bool pinned) = 0;

  virtual void set_entity_model(EntityModelAsset model) = 0;
  virtual void set_entity_animation(EntityAnimationAsset animation) = 0;
  virtual void clear_entity_models() = 0;
  virtual void clear_entity_animations() = 0;

  virtual void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) = 0;
  virtual void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) = 0;
  virtual void remove_chunk_mesh(ChunkMeshKey key) = 0;
  [[nodiscard]] virtual std::vector<ChunkMeshUploadReceipt>
  drain_chunk_mesh_upload_receipts() = 0;

  [[nodiscard]] virtual std::vector<RenderCommandDiagnostic>
  drain_diagnostics() = 0;
  [[nodiscard]] virtual RenderMetricsSnapshot metrics_snapshot() const = 0;
};

class IRenderCommandOwnerThread {
public:
  virtual ~IRenderCommandOwnerThread() = default;

  [[nodiscard]] virtual usize owner_thread_hash() const noexcept = 0;
  [[nodiscard]] virtual bool is_owner_thread() const noexcept = 0;
  [[nodiscard]] virtual bool post(std::function<void()> command) = 0;
  virtual void call(std::function<void()> command) = 0;
};

class RenderCommandQueue final {
public:
  explicit RenderCommandQueue(IRenderCommandTarget& target);
  RenderCommandQueue(IRenderCommandTarget& target, IRenderCommandOwnerThread& owner_thread);
  RenderCommandQueue(IRenderCommandTarget& target, sched::OwnerThread& owner_thread);

  void attach_owner_thread(IRenderCommandOwnerThread& owner_thread) noexcept;
  void attach_owner_thread(sched::OwnerThread& owner_thread);
  void clear_owner_thread() noexcept;
  [[nodiscard]] usize owner_thread_hash() const noexcept;
  [[nodiscard]] bool is_owner_thread() const noexcept;
  [[nodiscard]] usize pending_command_count() const noexcept;

  void flush();
  void refresh_metrics_snapshot();
  [[nodiscard]] RenderMetricsSnapshot metrics_snapshot();
  [[nodiscard]] RenderMetricsSnapshot cached_metrics_snapshot() const;

  void resize(u32 width, u32 height);
  void submit_frame(FramePacket packet);
  void render();
  void begin_shutdown();

  void set_chunk_shader_sources(
      std::string vertex_source,
      std::string fragment_source);
  void set_entity_shader_sources(
      std::string vertex_source,
      std::string fragment_source);
  void set_ui_shader_sources(
      std::string vertex_source,
      std::string fragment_source);
  void set_sky_shader_sources(
      std::string vertex_source,
      std::string fragment_source);
  void set_particle_shader_sources(
      std::string vertex_source,
      std::string fragment_source);
  void set_contact_shadow_shader_sources(
      std::string vertex_source,
      std::string fragment_source);

  void set_chunk_texture_atlas(TextureArrayUploadData upload);
  void set_entity_texture_atlas(TextureArrayUploadData upload);
  void set_particle_texture_atlas(TextureArrayUploadData upload);
  void set_ui_font_atlas(TextureArrayUploadData upload);
  void set_ui_item_atlas(TextureArrayUploadData upload);

  void configure_player_skin_atlas(PlayerSkinAtlasConfig config);
  [[nodiscard]] std::optional<TextureRegionHandle> upload_player_skin_texture(
      std::string skin_hash,
      TextureUploadData upload);
  [[nodiscard]] std::optional<PlayerSkinTextureSnapshot> player_skin_texture_snapshot(
      std::string_view skin_hash,
      TextureRegionHandle handle);
  [[nodiscard]] bool mark_player_skin_texture_used(
      TextureRegionHandle handle,
      bool pinned);

  void set_entity_model(EntityModelAsset model);
  void set_entity_animation(EntityAnimationAsset animation);
  void clear_entity_models();
  void clear_entity_animations();

  void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame);
  void enqueue_chunk_mesh_upload(ChunkMeshUpload upload);
  void remove_chunk_mesh(ChunkMeshKey key);
  [[nodiscard]] std::vector<ChunkMeshUploadReceipt> drain_chunk_mesh_upload_receipts();

  [[nodiscard]] std::vector<RenderCommandDiagnostic> drain_diagnostics();
  [[nodiscard]] usize pending_mesh_upload_count();
  [[nodiscard]] u32 mesh_uploads_last_frame();
  [[nodiscard]] double mesh_upload_time_ms_last_frame();
  [[nodiscard]] u32 mesh_upload_discards_last_frame();
  [[nodiscard]] u32 draw_calls_last_frame();
  [[nodiscard]] u32 chunk_draw_calls_last_frame();
  [[nodiscard]] u32 ui_draw_calls_last_frame();
  [[nodiscard]] u64 vertices_submitted_last_frame();
  [[nodiscard]] u64 triangles_submitted_last_frame();
  [[nodiscard]] u32 gpu_buffer_uploads_last_frame();
  [[nodiscard]] u64 gpu_buffer_upload_bytes_last_frame();
  [[nodiscard]] u32 texture_uploads_last_frame();
  [[nodiscard]] u64 texture_upload_bytes_last_frame();
  [[nodiscard]] bool gpu_profile_context_initialized();

private:
  using Command = std::function<void(IRenderCommandTarget&)>;

  struct AtomicMetricsSnapshot {
    std::atomic<usize> pending_mesh_upload_count{};
    std::atomic<u32> mesh_uploads_last_frame{};
    std::atomic<double> mesh_upload_time_ms_last_frame{};
    std::atomic<u32> mesh_upload_discards_last_frame{};
    std::atomic<u32> draw_calls_last_frame{};
    std::atomic<u32> chunk_draw_calls_last_frame{};
    std::atomic<u32> ui_draw_calls_last_frame{};
    std::atomic<u64> vertices_submitted_last_frame{};
    std::atomic<u64> triangles_submitted_last_frame{};
    std::atomic<u32> gpu_buffer_uploads_last_frame{};
    std::atomic<u64> gpu_buffer_upload_bytes_last_frame{};
    std::atomic<u32> texture_uploads_last_frame{};
    std::atomic<u64> texture_upload_bytes_last_frame{};
    std::atomic_bool gpu_profile_context_initialized{};
  };

  void dispatch(Command command);
  [[nodiscard]] bool try_enqueue(Command command);
  void publish_metrics_snapshot_on_owner();
  void store_metrics_snapshot(RenderMetricsSnapshot snapshot);

  template<typename Result, bool IsReference = std::is_reference_v<Result>>
  struct CallResultStorage {
    std::optional<Result> value;

    template<typename Work>
    void invoke(Work& work, IRenderCommandTarget& target) {
      value.emplace(std::invoke(work, target));
    }

    Result take() {
      return std::move(*value);
    }
  };

  template<typename Result>
  struct CallResultStorage<Result, true> {
    using Pointee = std::remove_reference_t<Result>;

    Pointee* value{};

    template<typename Work>
    void invoke(Work& work, IRenderCommandTarget& target) {
      value = std::addressof(std::invoke(work, target));
    }

    Result take() {
      return *value;
    }
  };

  template<typename Fn>
  auto call_on_owner(Fn&& fn)
      -> std::invoke_result_t<std::decay_t<Fn>&, IRenderCommandTarget&> {
    using Work = std::decay_t<Fn>;
    using Result = std::invoke_result_t<Work&, IRenderCommandTarget&>;

    Work work{std::forward<Fn>(fn)};
    if (owner_thread_ == nullptr || owner_thread_->is_owner_thread()) {
      if constexpr (std::is_void_v<Result>) {
        std::invoke(work, target_);
        publish_metrics_snapshot_on_owner();
        return;
      } else {
        Result result = std::invoke(work, target_);
        publish_metrics_snapshot_on_owner();
        return result;
      }
    }

    auto command_started = std::make_shared<std::atomic_bool>(false);
    auto work_ptr = std::make_shared<Work>(std::move(work));
    pending_command_count_.fetch_add(1U, std::memory_order_acq_rel);
    if constexpr (std::is_void_v<Result>) {
      try {
        owner_thread_->call([this, work_ptr, command_started]() mutable {
          command_started->store(true, std::memory_order_release);
          try {
            std::invoke(*work_ptr, target_);
            publish_metrics_snapshot_on_owner();
            pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
          } catch (...) {
            pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
            throw;
          }
        });
      } catch (...) {
        if (!command_started->load(std::memory_order_acquire)) {
          pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
        }
        throw;
      }
      return;
    } else {
      CallResultStorage<Result> storage;
      try {
        owner_thread_->call(
            [this, work_ptr, command_started, &storage]() mutable {
          command_started->store(true, std::memory_order_release);
          try {
            storage.invoke(*work_ptr, target_);
            publish_metrics_snapshot_on_owner();
            pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
          } catch (...) {
            pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
            throw;
          }
        });
      } catch (...) {
        if (!command_started->load(std::memory_order_acquire)) {
          pending_command_count_.fetch_sub(1U, std::memory_order_acq_rel);
        }
        throw;
      }
      return storage.take();
    }
  }

  IRenderCommandTarget& target_;
  std::unique_ptr<IRenderCommandOwnerThread> owned_owner_thread_{};
  IRenderCommandOwnerThread* owner_thread_{};
  std::atomic<usize> pending_command_count_{0U};
  AtomicMetricsSnapshot metrics_snapshot_{};
};

} // namespace arv::render
