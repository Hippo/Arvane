#include "web_render_host.hpp"

#include "web_application.hpp"
#include "web_platform.hpp"

#include "arv/render/render_commands.hpp"
#include "arv/render/webgl_render_commands.hpp"

#include <algorithm>
#include <utility>

namespace arv::web {

struct WebRenderHost::Backend final {
  Backend()
      : target(std::make_unique<render::WebGLRenderCommandTarget>()),
        queue(std::make_unique<render::RenderCommandQueue>(*target)) {
  }

  ~Backend() {
    if (queue != nullptr) {
      queue->begin_shutdown();
    }
    queue.reset();
    target.reset();
  }

  std::unique_ptr<render::WebGLRenderCommandTarget> target;
  std::unique_ptr<render::RenderCommandQueue> queue;
};

WebRenderHost::WebRenderHost() : backend_(std::make_unique<Backend>()) {}

WebRenderHost::~WebRenderHost() = default;

void WebRenderHost::resize(i32 width, i32 height) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    return;
  }
  backend_->queue->resize(
      static_cast<u32>(std::max(width, 1)),
      static_cast<u32>(std::max(height, 1)));
}

void WebRenderHost::clear_frame(f32 red, f32 green, f32 blue, f32 alpha) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    return;
  }
  render::FramePacket packet;
  packet.clear_color = {red, green, blue, alpha};
  backend_->queue->submit_frame(std::move(packet));
  backend_->queue->render();
  (void)report_diagnostics();
}

bool WebRenderHost::set_chunk_texture_atlas(render::TextureArrayUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  backend_->queue->set_chunk_texture_atlas(std::move(upload));
  return report_diagnostics();
}

bool WebRenderHost::set_entity_texture_atlas(render::TextureArrayUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  backend_->queue->set_entity_texture_atlas(std::move(upload));
  return report_diagnostics();
}

bool WebRenderHost::set_particle_texture_atlas(render::TextureArrayUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  backend_->queue->set_particle_texture_atlas(std::move(upload));
  return report_diagnostics();
}

bool WebRenderHost::set_ui_item_atlas(render::TextureArrayUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  backend_->queue->set_ui_item_atlas(std::move(upload));
  return report_diagnostics();
}

bool WebRenderHost::set_ui_font_atlas(render::TextureArrayUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  backend_->queue->set_ui_font_atlas(std::move(upload));
  return report_diagnostics();
}

void WebRenderHost::configure_player_skin_atlas(
    render::PlayerSkinAtlasConfig config) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->configure_player_skin_atlas(config);
  }
}

std::optional<render::TextureRegionHandle> WebRenderHost::upload_player_skin_texture(
    std::string skin_hash,
    render::TextureUploadData upload) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    set_status("WebGL render command host is not ready");
    return std::nullopt;
  }
  std::optional<render::TextureRegionHandle> handle =
      backend_->queue->upload_player_skin_texture(std::move(skin_hash), std::move(upload));
  if (!report_diagnostics()) {
    return std::nullopt;
  }
  return handle;
}

bool WebRenderHost::player_skin_texture_ready(
    std::string_view skin_hash,
    render::TextureRegionHandle handle) const {
  return backend_ != nullptr && backend_->queue != nullptr &&
         backend_->queue->player_skin_texture_snapshot(skin_hash, handle).has_value();
}

bool WebRenderHost::mark_player_skin_texture_used(
    render::TextureRegionHandle handle,
    bool pinned) {
  return backend_ != nullptr && backend_->queue != nullptr &&
         backend_->queue->mark_player_skin_texture_used(handle, pinned);
}

void WebRenderHost::set_fallback_color_vertices(
    std::vector<WebFallbackColorVertex> vertices) {
  if (backend_ != nullptr && backend_->target != nullptr) {
    std::vector<render::WebGLColorVertex> backend_vertices;
    backend_vertices.reserve(vertices.size());
    for (const WebFallbackColorVertex& vertex : vertices) {
      backend_vertices.push_back({vertex.position, vertex.color});
    }
    backend_->target->set_fallback_color_vertices(std::move(backend_vertices));
  }
}

void WebRenderHost::clear_fallback_color_vertices() {
  if (backend_ != nullptr && backend_->target != nullptr) {
    backend_->target->clear_fallback_color_vertices();
  }
}

void WebRenderHost::clear_entity_models() {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->clear_entity_models();
  }
}

void WebRenderHost::set_entity_model(render::EntityModelAsset model) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->set_entity_model(std::move(model));
  }
}

void WebRenderHost::clear_entity_animations() {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->clear_entity_animations();
  }
}

void WebRenderHost::set_entity_animation(render::EntityAnimationAsset animation) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->set_entity_animation(std::move(animation));
  }
}

void WebRenderHost::set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->set_chunk_mesh_upload_budget(max_uploads_per_frame);
  }
}

void WebRenderHost::enqueue_chunk_mesh_upload(render::ChunkMeshUpload upload) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->enqueue_chunk_mesh_upload(std::move(upload));
  }
}

void WebRenderHost::remove_chunk_mesh(render::ChunkMeshKey key) {
  if (backend_ != nullptr && backend_->queue != nullptr) {
    backend_->queue->remove_chunk_mesh(key);
  }
}

std::vector<render::ChunkMeshUploadReceipt>
WebRenderHost::drain_chunk_mesh_upload_receipts() {
  return backend_ == nullptr || backend_->queue == nullptr
             ? std::vector<render::ChunkMeshUploadReceipt>{}
             : backend_->queue->drain_chunk_mesh_upload_receipts();
}

void WebRenderHost::submit_and_render(render::FramePacket packet) {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    return;
  }
  backend_->queue->submit_frame(std::move(packet));
  backend_->queue->render();
  (void)report_diagnostics();
}

bool WebRenderHost::report_diagnostics() {
  if (backend_ == nullptr || backend_->queue == nullptr) {
    return false;
  }

  bool ok = true;
  for (const render::RenderCommandDiagnostic& diagnostic :
       backend_->queue->drain_diagnostics()) {
    if (diagnostic.message.empty()) {
      continue;
    }
    set_status("WebGL render: " + diagnostic.message);
    ok = false;
  }
  return ok;
}

bool initialize_web_render_host() {
  if (g_app.render_host != nullptr) {
    return true;
  }
  g_app.render_host = std::make_unique<WebRenderHost>();
  set_status("WebGL render command host ready");
  return true;
}

void shutdown_web_render_host() {
  g_app.render_host.reset();
}

bool web_render_host_ready() noexcept {
  return g_app.render_host != nullptr;
}

void resize_web_render_host(i32 width, i32 height) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->resize(width, height);
  }
}

void clear_web_render_frame(f32 red, f32 green, f32 blue, f32 alpha) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->clear_frame(red, green, blue, alpha);
  }
}

bool set_web_chunk_texture_atlas(render::TextureArrayUploadData upload) {
  if (g_app.render_host == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  return g_app.render_host->set_chunk_texture_atlas(std::move(upload));
}

bool set_web_entity_texture_atlas(render::TextureArrayUploadData upload) {
  if (g_app.render_host == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  return g_app.render_host->set_entity_texture_atlas(std::move(upload));
}

bool set_web_particle_texture_atlas(render::TextureArrayUploadData upload) {
  if (g_app.render_host == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  return g_app.render_host->set_particle_texture_atlas(std::move(upload));
}

bool set_web_ui_item_atlas(render::TextureArrayUploadData upload) {
  if (g_app.render_host == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  return g_app.render_host->set_ui_item_atlas(std::move(upload));
}

bool set_web_ui_font_atlas(render::TextureArrayUploadData upload) {
  if (g_app.render_host == nullptr) {
    set_status("WebGL render command host is not ready");
    return false;
  }
  return g_app.render_host->set_ui_font_atlas(std::move(upload));
}

void configure_web_player_skin_atlas(render::PlayerSkinAtlasConfig config) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->configure_player_skin_atlas(config);
  }
}

std::optional<render::TextureRegionHandle> upload_web_player_skin_texture_to_render_host(
    std::string skin_hash,
    render::TextureUploadData upload) {
  return g_app.render_host == nullptr
             ? std::nullopt
             : g_app.render_host->upload_player_skin_texture(
                   std::move(skin_hash),
                   std::move(upload));
}

bool web_player_skin_texture_ready(
    std::string_view skin_hash,
    render::TextureRegionHandle handle) noexcept {
  return g_app.render_host != nullptr &&
         g_app.render_host->player_skin_texture_ready(skin_hash, handle);
}

bool mark_web_player_skin_texture_used(
    render::TextureRegionHandle handle,
    bool pinned) noexcept {
  return g_app.render_host != nullptr &&
         g_app.render_host->mark_player_skin_texture_used(handle, pinned);
}

void set_web_fallback_color_vertices(
    std::vector<WebFallbackColorVertex> vertices) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->set_fallback_color_vertices(std::move(vertices));
  }
}

void clear_web_fallback_color_vertices() {
  if (g_app.render_host != nullptr) {
    g_app.render_host->clear_fallback_color_vertices();
  }
}

void clear_web_entity_models() {
  if (g_app.render_host != nullptr) {
    g_app.render_host->clear_entity_models();
  }
}

void set_web_entity_model(render::EntityModelAsset model) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->set_entity_model(std::move(model));
  }
}

void clear_web_entity_animations() {
  if (g_app.render_host != nullptr) {
    g_app.render_host->clear_entity_animations();
  }
}

void set_web_entity_animation(render::EntityAnimationAsset animation) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->set_entity_animation(std::move(animation));
  }
}

void set_web_chunk_mesh_upload_budget(u32 max_uploads_per_frame) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->set_chunk_mesh_upload_budget(max_uploads_per_frame);
  }
}

void enqueue_web_chunk_mesh_upload(render::ChunkMeshUpload upload) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->enqueue_chunk_mesh_upload(std::move(upload));
  }
}

void remove_web_chunk_mesh(render::ChunkMeshKey key) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->remove_chunk_mesh(key);
  }
}

std::vector<render::ChunkMeshUploadReceipt> drain_web_chunk_mesh_upload_receipts() {
  return g_app.render_host == nullptr
             ? std::vector<render::ChunkMeshUploadReceipt>{}
             : g_app.render_host->drain_chunk_mesh_upload_receipts();
}

void submit_and_render_web_frame(render::FramePacket packet) {
  if (g_app.render_host != nullptr) {
    g_app.render_host->submit_and_render(std::move(packet));
  }
}

} // namespace arv::web
