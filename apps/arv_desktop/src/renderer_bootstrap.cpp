#include "renderer_bootstrap.hpp"

#include <utility>

namespace arv::desktop {

bool configure_renderer(
    render::OpenGLRenderer& renderer,
    asset::ResourceManager& resources,
    const io::EngineConfig& config) {
  auto chunk_vertex_shader =
      resources.load_text({"arvane:shader/world/chunk.vert"}, asset::AssetKind::ShaderSource);
  if (!chunk_vertex_shader) {
    arv::core::error("assets", chunk_vertex_shader.error().message);
    return false;
  }
  auto chunk_fragment_shader =
      resources.load_text({"arvane:shader/world/chunk.frag"}, asset::AssetKind::ShaderSource);
  if (!chunk_fragment_shader) {
    arv::core::error("assets", chunk_fragment_shader.error().message);
    return false;
  }
  renderer.set_chunk_shader_sources(
      std::move(chunk_vertex_shader.value()),
      std::move(chunk_fragment_shader.value()));

  auto sky_vertex_shader =
      resources.load_text({"arvane:shader/world/sky.vert"}, asset::AssetKind::ShaderSource);
  if (!sky_vertex_shader) {
    arv::core::error("assets", sky_vertex_shader.error().message);
    return false;
  }
  auto sky_fragment_shader =
      resources.load_text({"arvane:shader/world/sky.frag"}, asset::AssetKind::ShaderSource);
  if (!sky_fragment_shader) {
    arv::core::error("assets", sky_fragment_shader.error().message);
    return false;
  }
  renderer.set_sky_shader_sources(
      std::move(sky_vertex_shader.value()),
      std::move(sky_fragment_shader.value()));

  auto entity_vertex_shader =
      resources.load_text({"arvane:shader/entity/entity.vert"}, asset::AssetKind::ShaderSource);
  if (!entity_vertex_shader) {
    arv::core::error("assets", entity_vertex_shader.error().message);
    return false;
  }
  auto entity_fragment_shader =
      resources.load_text({"arvane:shader/entity/entity.frag"}, asset::AssetKind::ShaderSource);
  if (!entity_fragment_shader) {
    arv::core::error("assets", entity_fragment_shader.error().message);
    return false;
  }
  renderer.set_entity_shader_sources(
      std::move(entity_vertex_shader.value()),
      std::move(entity_fragment_shader.value()));

  auto ui_vertex_shader =
      resources.load_text({"arvane:shader/ui/ui.vert"}, asset::AssetKind::ShaderSource);
  if (!ui_vertex_shader) {
    arv::core::error("assets", ui_vertex_shader.error().message);
    return false;
  }
  auto ui_fragment_shader =
      resources.load_text({"arvane:shader/ui/ui.frag"}, asset::AssetKind::ShaderSource);
  if (!ui_fragment_shader) {
    arv::core::error("assets", ui_fragment_shader.error().message);
    return false;
  }
  renderer.set_ui_shader_sources(
      std::move(ui_vertex_shader.value()),
      std::move(ui_fragment_shader.value()));
  renderer.set_chunk_mesh_upload_budget(config.render.max_chunk_mesh_uploads_per_frame);
  return true;
}

} // namespace arv::desktop
