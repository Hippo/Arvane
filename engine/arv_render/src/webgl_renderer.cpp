#include "arv/render/webgl_renderer.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace arv::render {
namespace {

struct WebGLUiVertex {
  Vec2f position{};
  Vec2f uv{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  f32 page{};
};

struct WebGLParticleInstanceData {
  Vec3f position{};
  f32 size{1.0F};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  Vec2f uv_min{};
  Vec2f uv_max{1.0F, 1.0F};
  u32 atlas_page{};
  f32 rotation_radians{};
};

inline constexpr usize MaxWebGLEntityShaderBones = 128U;

[[nodiscard]] u32 full_mipmap_max_level(u32 width, u32 height) noexcept {
  u32 size = std::max(width, height);
  u32 level = 0U;
  while (size > 1U) {
    size /= 2U;
    ++level;
  }
  return level;
}

[[nodiscard]] const EntityModelAsset* find_entity_model(
    std::span<const EntityModelAsset> models,
    EntityModelID id) noexcept {
  for (const EntityModelAsset& model : models) {
    if (model.id == id) {
      return &model;
    }
  }
  return nullptr;
}

[[nodiscard]] const EntityAnimationAsset* find_entity_animation(
    std::span<const EntityAnimationAsset> animations,
    EntityAnimationID id) noexcept {
  for (const EntityAnimationAsset& animation : animations) {
    if (animation.id == id) {
      return &animation;
    }
  }
  return nullptr;
}

void apply_bone_rotation_overrides(
    const EntityModelAsset& model,
    std::span<Vec3f> bone_rotations,
    const std::vector<EntityBoneRotationOverride>& overrides) noexcept {
  for (const EntityBoneRotationOverride& bone_override : overrides) {
    if (bone_override.bone_name.empty()) {
      continue;
    }
    for (usize bone_index = 0;
         bone_index < model.bones.size() && bone_index < bone_rotations.size();
         ++bone_index) {
      if (model.bones[bone_index].name != bone_override.bone_name) {
        continue;
      }
      bone_rotations[bone_index] =
          bone_override.replace
              ? bone_override.rotation_degrees
              : bone_rotations[bone_index] + bone_override.rotation_degrees;
      break;
    }
  }
}

void append_ui_quad_vertices(
    std::vector<WebGLUiVertex>& vertices,
    const std::array<Vec2f, 4>& positions,
    const std::array<Vec2f, 4>& uvs,
    Vec4f color,
    u32 page) {
  const auto push = [&](usize index) {
    vertices.push_back({positions[index], uvs[index], color, static_cast<f32>(page)});
  };
  push(0U);
  push(1U);
  push(2U);
  push(0U);
  push(2U);
  push(3U);
}

void append_ui_glyph_vertices(
    std::vector<WebGLUiVertex>& vertices,
    const UIGlyphDrawItem& glyph) {
  Vec2f min = glyph.min;
  Vec2f max = glyph.max;
  Vec2f uv_min = glyph.uv_min;
  Vec2f uv_max = glyph.uv_max;
  if (glyph.clip) {
    const Vec2f original_min = min;
    const Vec2f original_max = max;
    min.x = std::max(min.x, glyph.clip_min.x);
    min.y = std::max(min.y, glyph.clip_min.y);
    max.x = std::min(max.x, glyph.clip_max.x);
    max.y = std::min(max.y, glyph.clip_max.y);
    if (max.x <= min.x || max.y <= min.y ||
        original_max.x <= original_min.x || original_max.y <= original_min.y) {
      return;
    }
    const f32 left_t = (min.x - original_min.x) / (original_max.x - original_min.x);
    const f32 right_t = (max.x - original_min.x) / (original_max.x - original_min.x);
    const f32 top_t = (min.y - original_min.y) / (original_max.y - original_min.y);
    const f32 bottom_t = (max.y - original_min.y) / (original_max.y - original_min.y);
    const Vec2f original_uv_min = uv_min;
    const Vec2f original_uv_max = uv_max;
    uv_min.x = original_uv_min.x + (original_uv_max.x - original_uv_min.x) * left_t;
    uv_max.x = original_uv_min.x + (original_uv_max.x - original_uv_min.x) * right_t;
    uv_min.y = original_uv_min.y + (original_uv_max.y - original_uv_min.y) * top_t;
    uv_max.y = original_uv_min.y + (original_uv_max.y - original_uv_min.y) * bottom_t;
  }

  const std::array<Vec2f, 4> positions{{
      min,
      {max.x, min.y},
      max,
      {min.x, max.y},
  }};
  const std::array<Vec2f, 4> uvs{{
      uv_min,
      {uv_max.x, uv_min.y},
      uv_max,
      {uv_min.x, uv_max.y},
  }};
  append_ui_quad_vertices(vertices, positions, uvs, glyph.color, glyph.page);
}

void append_ui_axis_quad_vertices(
    std::vector<WebGLUiVertex>& vertices,
    Vec2f min,
    Vec2f max,
    Vec4f color) {
  if (max.x <= min.x || max.y <= min.y || color.w <= 0.0F) {
    return;
  }
  const std::array<Vec2f, 4> positions{{
      min,
      {max.x, min.y},
      max,
      {min.x, max.y},
  }};
  constexpr std::array<Vec2f, 4> uvs{{
      {0.0F, 0.0F},
      {1.0F, 0.0F},
      {1.0F, 1.0F},
      {0.0F, 1.0F},
  }};
  append_ui_quad_vertices(vertices, positions, uvs, color, 0U);
}

void append_ui_rect_vertices(
    std::vector<WebGLUiVertex>& vertices,
    const UIRectDrawItem& rect) {
  Vec2f min = rect.min;
  Vec2f max = rect.max;
  if (rect.clip) {
    min.x = std::max(min.x, rect.clip_min.x);
    min.y = std::max(min.y, rect.clip_min.y);
    max.x = std::min(max.x, rect.clip_max.x);
    max.y = std::min(max.y, rect.clip_max.y);
  }
  append_ui_axis_quad_vertices(vertices, min, max, rect.color);

  const f32 border = std::max(rect.border_width, 0.0F);
  if (border <= 0.0F || rect.border_color.w <= 0.0F ||
      max.x <= min.x || max.y <= min.y) {
    return;
  }
  append_ui_axis_quad_vertices(vertices, min, {max.x, min.y + border}, rect.border_color);
  append_ui_axis_quad_vertices(vertices, {min.x, max.y - border}, max, rect.border_color);
  append_ui_axis_quad_vertices(vertices, min, {min.x + border, max.y}, rect.border_color);
  append_ui_axis_quad_vertices(vertices, {max.x - border, min.y}, max, rect.border_color);
}

void draw_ui_vertices(
    const std::vector<WebGLUiVertex>& vertices,
    const WebGLUiRenderer& renderer,
    i32 viewport_width,
    i32 viewport_height,
    GLint mode,
    GLuint texture) {
  if (vertices.empty() || renderer.program == 0U || renderer.vao == 0U ||
      renderer.vbo == 0U) {
    return;
  }
  if (mode == 1 && texture == 0U) {
    return;
  }

  glUseProgram(renderer.program);
  glUniform2f(
      renderer.viewport_location,
      static_cast<f32>(std::max(viewport_width, 1)),
      static_cast<f32>(std::max(viewport_height, 1)));
  glUniform1i(renderer.mode_location, mode);
  glUniform1i(renderer.atlas_location, 0);
  if (mode == 1) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  }
  glBindVertexArray(renderer.vao);
  glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(WebGLUiVertex)),
      vertices.data(),
      GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
  if (mode == 1) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  }
}

} // namespace

void configure_default_webgl_state() {
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glClearDepthf(1.0F);
}

void clear_webgl_frame(f32 red, f32 green, f32 blue, f32 alpha) {
  glClearColor(red, green, blue, alpha);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void resize_webgl_viewport(i32 width, i32 height) {
  glViewport(0, 0, width, height);
}

void delete_webgl_texture(u32 texture) {
  if (texture == 0U) {
    return;
  }
  GLuint gl_texture = texture;
  glDeleteTextures(1, &gl_texture);
}

void destroy_webgl_indexed_mesh(WebGLIndexedMesh& mesh) {
  if (mesh.ebo != 0U) {
    const GLuint buffer = mesh.ebo;
    glDeleteBuffers(1, &buffer);
    mesh.ebo = 0U;
  }
  if (mesh.vbo != 0U) {
    const GLuint buffer = mesh.vbo;
    glDeleteBuffers(1, &buffer);
    mesh.vbo = 0U;
  }
  if (mesh.vao != 0U) {
    const GLuint vertex_array = mesh.vao;
    glDeleteVertexArrays(1, &vertex_array);
    mesh.vao = 0U;
  }
  mesh.index_count = 0;
}

void destroy_webgl_array_mesh(WebGLArrayMesh& mesh) {
  if (mesh.vbo != 0U) {
    const GLuint buffer = mesh.vbo;
    glDeleteBuffers(1, &buffer);
    mesh.vbo = 0U;
  }
  if (mesh.vao != 0U) {
    const GLuint vertex_array = mesh.vao;
    glDeleteVertexArrays(1, &vertex_array);
    mesh.vao = 0U;
  }
  mesh.vertex_count = 0;
}

void destroy_webgl_ui_renderer(WebGLUiRenderer& renderer) {
  if (renderer.vbo != 0U) {
    const GLuint buffer = renderer.vbo;
    glDeleteBuffers(1, &buffer);
    renderer.vbo = 0U;
  }
  if (renderer.vao != 0U) {
    const GLuint vertex_array = renderer.vao;
    glDeleteVertexArrays(1, &vertex_array);
    renderer.vao = 0U;
  }
  renderer.program = 0U;
  renderer.viewport_location = -1;
  renderer.atlas_location = -1;
  renderer.mode_location = -1;
}

void destroy_webgl_sky_renderer(WebGLSkyRenderer& renderer) {
  if (renderer.vao != 0U) {
    const GLuint vertex_array = renderer.vao;
    glDeleteVertexArrays(1, &vertex_array);
    renderer.vao = 0U;
  }
  renderer = {};
}

void destroy_webgl_particle_renderer(WebGLParticleRenderer& renderer) {
  if (renderer.vbo != 0U) {
    const GLuint buffer = renderer.vbo;
    glDeleteBuffers(1, &buffer);
    renderer.vbo = 0U;
  }
  if (renderer.vao != 0U) {
    const GLuint vertex_array = renderer.vao;
    glDeleteVertexArrays(1, &vertex_array);
    renderer.vao = 0U;
  }
  renderer = {};
}

WebGLUiRenderer create_webgl_ui_renderer(u32 program) {
  WebGLUiRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.viewport_location = glGetUniformLocation(program, "u_viewport");
  renderer.atlas_location = glGetUniformLocation(program, "u_atlas");
  renderer.mode_location = glGetUniformLocation(program, "u_mode");

  GLuint vao{};
  GLuint vbo{};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(
      0U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLUiVertex),
      reinterpret_cast<const void*>(offsetof(WebGLUiVertex, position)));
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLUiVertex),
      reinterpret_cast<const void*>(offsetof(WebGLUiVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribPointer(
      2U,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLUiVertex),
      reinterpret_cast<const void*>(offsetof(WebGLUiVertex, color)));
  glEnableVertexAttribArray(3U);
  glVertexAttribPointer(
      3U,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLUiVertex),
      reinterpret_cast<const void*>(offsetof(WebGLUiVertex, page)));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);

  renderer.vao = vao;
  renderer.vbo = vbo;
  return renderer;
}

WebGLSkyRenderer create_webgl_sky_renderer(u32 program) {
  WebGLSkyRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.camera_forward_location = glGetUniformLocation(program, "u_camera_forward");
  renderer.camera_right_location = glGetUniformLocation(program, "u_camera_right");
  renderer.camera_up_location = glGetUniformLocation(program, "u_camera_up");
  renderer.camera_aspect_location = glGetUniformLocation(program, "u_camera_aspect");
  renderer.camera_fov_y_location =
      glGetUniformLocation(program, "u_camera_fov_y_degrees");
  renderer.sun_direction_location = glGetUniformLocation(program, "u_sun_direction");
  renderer.moon_direction_location = glGetUniformLocation(program, "u_moon_direction");
  renderer.daylight_color_location = glGetUniformLocation(program, "u_daylight_color");
  renderer.ambient_color_location = glGetUniformLocation(program, "u_ambient_color");
  renderer.fog_color_location = glGetUniformLocation(program, "u_fog_color");
  renderer.daylight_strength_location =
      glGetUniformLocation(program, "u_daylight_strength");
  renderer.moonlight_strength_location =
      glGetUniformLocation(program, "u_moonlight_strength");
  renderer.star_visibility_location = glGetUniformLocation(program, "u_star_visibility");
  renderer.day_fraction_location = glGetUniformLocation(program, "u_day_fraction");
  renderer.cloud_time_location = glGetUniformLocation(program, "u_cloud_time_seconds");
  renderer.cirrus_amount_location = glGetUniformLocation(program, "u_cirrus_amount");
  renderer.cumulus_amount_location = glGetUniformLocation(program, "u_cumulus_amount");
  renderer.cloud_intensity_location = glGetUniformLocation(program, "u_cloud_intensity");
  renderer.exposure_location = glGetUniformLocation(program, "u_exposure");

  GLuint vao{};
  glGenVertexArrays(1, &vao);
  renderer.vao = vao;
  return renderer;
}

WebGLChunkRenderer create_webgl_chunk_renderer(u32 program) {
  WebGLChunkRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.mvp_location = glGetUniformLocation(program, "u_mvp");
  renderer.origin_location = glGetUniformLocation(program, "u_chunk_origin");
  renderer.atlas_location = glGetUniformLocation(program, "u_chunk_atlas");
  renderer.atlas_inv_size_location = glGetUniformLocation(program, "u_atlas_inv_size");
  renderer.alpha_location = glGetUniformLocation(program, "u_alpha");
  renderer.animation_time_location =
      glGetUniformLocation(program, "u_animation_time_seconds");
  return renderer;
}

WebGLColorRenderer create_webgl_color_renderer(u32 program) {
  WebGLColorRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.mvp_location = glGetUniformLocation(program, "u_mvp");
  return renderer;
}

WebGLEntityRenderer create_webgl_entity_renderer(u32 program) {
  WebGLEntityRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.mvp_location = glGetUniformLocation(program, "u_mvp");
  renderer.position_location = glGetUniformLocation(program, "u_instance_position");
  renderer.rotation_location = glGetUniformLocation(program, "u_instance_rotation");
  renderer.scale_location = glGetUniformLocation(program, "u_instance_scale");
  renderer.atlas_location = glGetUniformLocation(program, "u_entity_atlas");
  renderer.atlas_size_location = glGetUniformLocation(program, "u_atlas_size");
  renderer.player_skin_atlas_location =
      glGetUniformLocation(program, "u_player_skin_atlas");
  renderer.player_skin_atlas_size_location =
      glGetUniformLocation(program, "u_player_skin_atlas_size");
  renderer.player_skin_region_location =
      glGetUniformLocation(program, "u_player_skin_region");
  renderer.player_skin_page_location = glGetUniformLocation(program, "u_player_skin_page");
  renderer.bone_count_location = glGetUniformLocation(program, "u_bone_count");
  renderer.bone_pivots_location = glGetUniformLocation(program, "u_bone_pivots[0]");
  renderer.bone_rotations_location = glGetUniformLocation(program, "u_bone_rotations[0]");
  renderer.bone_positions_location = glGetUniformLocation(program, "u_bone_positions[0]");
  return renderer;
}

WebGLParticleRenderer create_webgl_particle_renderer(u32 program) {
  WebGLParticleRenderer renderer;
  renderer.program = program;
  if (program == 0U) {
    return renderer;
  }

  renderer.camera_position_location = glGetUniformLocation(program, "u_camera_position");
  renderer.camera_forward_location = glGetUniformLocation(program, "u_camera_forward");
  renderer.camera_right_location = glGetUniformLocation(program, "u_camera_right");
  renderer.camera_up_location = glGetUniformLocation(program, "u_camera_up");
  renderer.camera_aspect_location = glGetUniformLocation(program, "u_camera_aspect");
  renderer.camera_fov_y_location =
      glGetUniformLocation(program, "u_camera_fov_y_degrees");
  renderer.camera_near_location = glGetUniformLocation(program, "u_camera_near_plane");
  renderer.camera_far_location = glGetUniformLocation(program, "u_camera_far_plane");
  renderer.atlas_size_location = glGetUniformLocation(program, "u_atlas_size");
  renderer.atlas_location = glGetUniformLocation(program, "u_particle_atlas");

  GLuint vao{};
  GLuint vbo{};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(
      0U,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, position)));
  glVertexAttribDivisor(0U, 1U);
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, size)));
  glVertexAttribDivisor(1U, 1U);
  glEnableVertexAttribArray(2U);
  glVertexAttribPointer(
      2U,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, color)));
  glVertexAttribDivisor(2U, 1U);
  glEnableVertexAttribArray(3U);
  glVertexAttribPointer(
      3U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, uv_min)));
  glVertexAttribDivisor(3U, 1U);
  glEnableVertexAttribArray(4U);
  glVertexAttribPointer(
      4U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, uv_max)));
  glVertexAttribDivisor(4U, 1U);
  glEnableVertexAttribArray(5U);
  glVertexAttribIPointer(
      5U,
      1,
      GL_UNSIGNED_INT,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, atlas_page)));
  glVertexAttribDivisor(5U, 1U);
  glEnableVertexAttribArray(6U);
  glVertexAttribPointer(
      6U,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(WebGLParticleInstanceData),
      reinterpret_cast<const void*>(offsetof(WebGLParticleInstanceData, rotation_radians)));
  glVertexAttribDivisor(6U, 1U);

  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
  renderer.vao = vao;
  renderer.vbo = vbo;
  return renderer;
}

void reset_webgl_draw_bindings() {
  glBindVertexArray(0U);
  glUseProgram(0U);
}

void begin_webgl_translucent_pass() {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
}

void end_webgl_translucent_pass() {
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
}

void draw_webgl_ui_frame(
    const FramePacket& frame,
    const WebGLUiRenderer& renderer,
    i32 viewport_width,
    i32 viewport_height,
    WebGLUiTextures textures) {
  u8 max_layer = 0U;
  for (const UIRectDrawItem& rect : frame.ui_rects) {
    max_layer = std::max(max_layer, rect.layer);
  }
  for (const UITexturedQuadDrawItem& quad : frame.ui_textured_quads) {
    max_layer = std::max(max_layer, quad.layer);
  }
  for (const UIGlyphDrawItem& glyph : frame.ui_glyphs) {
    max_layer = std::max(max_layer, glyph.layer);
  }

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  std::vector<WebGLUiVertex> color_vertices;
  std::vector<WebGLUiVertex> font_vertices;
  std::vector<WebGLUiVertex> terrain_vertices;
  std::vector<WebGLUiVertex> item_vertices;
  color_vertices.reserve(frame.ui_rects.size() * 30U);
  font_vertices.reserve(frame.ui_glyphs.size() * 6U);
  terrain_vertices.reserve(frame.ui_textured_quads.size() * 6U);
  item_vertices.reserve(frame.ui_textured_quads.size() * 6U);

  for (u32 layer = 0U; layer <= static_cast<u32>(max_layer); ++layer) {
    color_vertices.clear();
    font_vertices.clear();
    terrain_vertices.clear();
    item_vertices.clear();

    const u8 current_layer = static_cast<u8>(layer);
    for (const UIRectDrawItem& rect : frame.ui_rects) {
      if (rect.layer == current_layer) {
        append_ui_rect_vertices(color_vertices, rect);
      }
    }
    for (const UITexturedQuadDrawItem& quad : frame.ui_textured_quads) {
      if (quad.layer != current_layer) {
        continue;
      }
      switch (quad.source) {
        case UiTextureSource::TerrainAtlas:
          append_ui_quad_vertices(
              terrain_vertices,
              quad.positions,
              quad.uvs,
              quad.color,
              quad.page);
          break;
        case UiTextureSource::ItemAtlas:
          append_ui_quad_vertices(
              item_vertices,
              quad.positions,
              quad.uvs,
              quad.color,
              quad.page);
          break;
        case UiTextureSource::White:
          append_ui_quad_vertices(
              color_vertices,
              quad.positions,
              quad.uvs,
              quad.color,
              0U);
          break;
        case UiTextureSource::FontAtlas:
          append_ui_quad_vertices(
              font_vertices,
              quad.positions,
              quad.uvs,
              quad.color,
              quad.page);
          break;
        case UiTextureSource::PlayerSkinAtlas:
          break;
      }
    }
    for (const UIGlyphDrawItem& glyph : frame.ui_glyphs) {
      if (glyph.layer == current_layer) {
        append_ui_glyph_vertices(font_vertices, glyph);
      }
    }

    draw_ui_vertices(color_vertices, renderer, viewport_width, viewport_height, 0, 0U);
    draw_ui_vertices(
        terrain_vertices,
        renderer,
        viewport_width,
        viewport_height,
        1,
        textures.terrain_texture);
    draw_ui_vertices(
        item_vertices,
        renderer,
        viewport_width,
        viewport_height,
        1,
        textures.item_texture);
    draw_ui_vertices(
        font_vertices,
        renderer,
        viewport_width,
        viewport_height,
        1,
        textures.font_texture);
  }
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void draw_webgl_sky(
    const WebGLSkyRenderer& renderer,
    const SkyUniforms& sky,
    Vec3f camera_forward,
    Vec3f camera_right,
    Vec3f camera_up,
    f32 camera_aspect,
    f32 camera_fov_y_degrees) {
  if (renderer.program == 0U || renderer.vao == 0U || !sky.enabled) {
    return;
  }

  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(renderer.program);
  glUniform3f(
      renderer.camera_forward_location,
      camera_forward.x,
      camera_forward.y,
      camera_forward.z);
  glUniform3f(
      renderer.camera_right_location,
      camera_right.x,
      camera_right.y,
      camera_right.z);
  glUniform3f(
      renderer.camera_up_location,
      camera_up.x,
      camera_up.y,
      camera_up.z);
  glUniform1f(renderer.camera_aspect_location, camera_aspect);
  glUniform1f(renderer.camera_fov_y_location, camera_fov_y_degrees);
  glUniform3f(
      renderer.sun_direction_location,
      sky.sun_direction.x,
      sky.sun_direction.y,
      sky.sun_direction.z);
  glUniform3f(
      renderer.moon_direction_location,
      sky.moon_direction.x,
      sky.moon_direction.y,
      sky.moon_direction.z);
  glUniform3f(
      renderer.daylight_color_location,
      sky.daylight_color.x,
      sky.daylight_color.y,
      sky.daylight_color.z);
  glUniform3f(
      renderer.ambient_color_location,
      sky.ambient_color.x,
      sky.ambient_color.y,
      sky.ambient_color.z);
  glUniform3f(
      renderer.fog_color_location,
      sky.fog_color.x,
      sky.fog_color.y,
      sky.fog_color.z);
  glUniform1f(renderer.daylight_strength_location, sky.daylight_strength);
  glUniform1f(renderer.moonlight_strength_location, sky.moonlight_strength);
  glUniform1f(renderer.star_visibility_location, sky.star_visibility);
  glUniform1f(renderer.day_fraction_location, sky.day_fraction);
  glUniform1f(renderer.cloud_time_location, sky.cloud_time_seconds);
  glUniform1f(renderer.cirrus_amount_location, sky.cirrus_amount);
  glUniform1f(renderer.cumulus_amount_location, sky.cumulus_amount);
  glUniform1f(renderer.cloud_intensity_location, sky.cloud_intensity);
  glUniform1f(renderer.exposure_location, sky.exposure);

  glBindVertexArray(renderer.vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0U);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
}

void draw_webgl_chunk_meshes(
    const WebGLChunkRenderer& renderer,
    std::span<const WebGLChunkSectionMesh> chunks,
    WebGLTextureArrayView atlas,
    const Mat4f& mvp,
    f32 animation_time_seconds,
    f32 alpha,
    WebGLChunkMeshLayer layer) {
  if (renderer.program == 0U || atlas.texture == 0U || chunks.empty()) {
    return;
  }

  glUseProgram(renderer.program);
  glUniformMatrix4fv(renderer.mvp_location, 1, GL_FALSE, mvp.values.data());
  glUniform1i(renderer.atlas_location, 0);
  glUniform2f(
      renderer.atlas_inv_size_location,
      1.0F / static_cast<f32>(std::max(atlas.width, 1U)),
      1.0F / static_cast<f32>(std::max(atlas.height, 1U)));
  glUniform1f(renderer.animation_time_location, animation_time_seconds);
  glUniform1f(renderer.alpha_location, alpha);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.texture);

  for (const WebGLChunkSectionMesh& chunk : chunks) {
    const WebGLIndexedMesh& mesh = layer == WebGLChunkMeshLayer::Opaque
                                      ? chunk.opaque
                                      : chunk.transparent;
    if (mesh.index_count == 0 || mesh.vao == 0U) {
      continue;
    }
    const Vec3f origin{
        static_cast<f32>(chunk.coord.value.x * ChunkSize),
        static_cast<f32>(chunk.coord.value.y * ChunkSize),
        static_cast<f32>(chunk.coord.value.z * ChunkSize)};
    glUniform3f(renderer.origin_location, origin.x, origin.y, origin.z);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
  }
}

void draw_webgl_color_mesh(
    const WebGLColorRenderer& renderer,
    const WebGLArrayMesh& mesh,
    const Mat4f& mvp) {
  if (renderer.program == 0U || mesh.vertex_count == 0 || mesh.vao == 0U) {
    return;
  }

  glUseProgram(renderer.program);
  glUniformMatrix4fv(renderer.mvp_location, 1, GL_FALSE, mvp.values.data());
  glBindVertexArray(mesh.vao);
  glDrawArrays(GL_TRIANGLES, 0, mesh.vertex_count);
}

void draw_webgl_entity_models(
    const WebGLEntityRenderer& renderer,
    std::span<const EntityDrawItem> draws,
    const HashMap<EntityModelID, WebGLIndexedMesh>& gpu_models,
    std::span<const EntityModelAsset> models,
    std::span<const EntityAnimationAsset> animations,
    WebGLTextureArrayView entity_atlas,
    WebGLTextureArrayView player_skin_atlas,
    const Mat4f& mvp,
    Vec3f eye,
    bool translucent,
    FunctionRef<std::optional<WebGLAtlasRegion>(TextureRegionHandle)>
        resolve_player_skin_region) {
  if (renderer.program == 0U || entity_atlas.texture == 0U || gpu_models.empty() ||
      draws.empty()) {
    return;
  }

  std::vector<const EntityDrawItem*> draw_order;
  draw_order.reserve(draws.size());
  for (const EntityDrawItem& draw : draws) {
    draw_order.push_back(&draw);
  }
  std::sort(
      draw_order.begin(),
      draw_order.end(),
      [&](const EntityDrawItem* lhs, const EntityDrawItem* rhs) {
        if (translucent) {
          return distance_squared(lhs->position, eye) >
                 distance_squared(rhs->position, eye);
        }
        return lhs->model_id < rhs->model_id;
      });

  glUseProgram(renderer.program);
  glUniformMatrix4fv(renderer.mvp_location, 1, GL_FALSE, mvp.values.data());
  glUniform1i(renderer.atlas_location, 0);
  glUniform2f(
      renderer.atlas_size_location,
      static_cast<f32>(std::max(entity_atlas.width, 1U)),
      static_cast<f32>(std::max(entity_atlas.height, 1U)));
  glUniform1i(renderer.player_skin_atlas_location, 1);
  glUniform2f(
      renderer.player_skin_atlas_size_location,
      static_cast<f32>(std::max(player_skin_atlas.width, 1U)),
      static_cast<f32>(std::max(player_skin_atlas.height, 1U)));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, entity_atlas.texture);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      player_skin_atlas.texture != 0U ? player_skin_atlas.texture : entity_atlas.texture);

  std::array<Vec3f, MaxWebGLEntityShaderBones> bone_pivots{};
  std::array<Vec3f, MaxWebGLEntityShaderBones> bone_rotations{};
  std::array<Vec3f, MaxWebGLEntityShaderBones> bone_positions{};
  for (const EntityDrawItem* draw : draw_order) {
    const EntityModelAsset* model = find_entity_model(models, draw->model_id);
    const auto gpu = gpu_models.find(draw->model_id);
    if (model == nullptr ||
        gpu == gpu_models.end() ||
        gpu->second.index_count == 0 ||
        model->translucent != translucent ||
        model->bones.size() > MaxWebGLEntityShaderBones) {
      continue;
    }

    const EntityAnimationAsset* animation =
        find_entity_animation(animations, draw->animation_id);
    for (usize bone_index = 0; bone_index < model->bones.size(); ++bone_index) {
      bone_pivots[bone_index] = model->bones[bone_index].pivot;
    }
    std::span<Vec3f> bone_rotation_span{
        bone_rotations.data(),
        model->bones.size()};
    std::span<Vec3f> bone_position_span{
        bone_positions.data(),
        model->bones.size()};
    sample_entity_model_animations(
        *model,
        animation,
        draw->animations,
        bone_rotation_span,
        bone_position_span);
    apply_bone_rotation_overrides(
        *model,
        bone_rotation_span,
        draw->bone_rotation_overrides);

    const GLsizei bone_count = static_cast<GLsizei>(model->bones.size());
    glUniform1i(renderer.bone_count_location, bone_count);
    if (bone_count > 0) {
      glUniform3fv(
          renderer.bone_pivots_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(bone_pivots.data()));
      glUniform3fv(
          renderer.bone_rotations_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(bone_rotations.data()));
      glUniform3fv(
          renderer.bone_positions_location,
          bone_count,
          reinterpret_cast<const GLfloat*>(bone_positions.data()));
    }
    glUniform3f(
        renderer.position_location,
        draw->position.x,
        draw->position.y,
        draw->position.z);
    glUniform4f(
        renderer.rotation_location,
        draw->rotation.x,
        draw->rotation.y,
        draw->rotation.z,
        draw->rotation.w);
    glUniform1f(renderer.scale_location, draw->scale);

    Vec4f player_skin_region{};
    u32 player_skin_page = 0U;
    if (draw->player_skin_region.valid() && resolve_player_skin_region) {
      if (const std::optional<WebGLAtlasRegion> region =
              resolve_player_skin_region(draw->player_skin_region);
          region.has_value()) {
        player_skin_region = {
            static_cast<f32>(region->x),
            static_cast<f32>(region->y),
            static_cast<f32>(region->width),
            static_cast<f32>(region->height)};
        player_skin_page = region->page;
      }
    }
    glUniform4f(
        renderer.player_skin_region_location,
        player_skin_region.x,
        player_skin_region.y,
        player_skin_region.z,
        player_skin_region.w);
    glUniform1i(
        renderer.player_skin_page_location,
        static_cast<GLint>(player_skin_page));
    glBindVertexArray(gpu->second.vao);
    glDrawElements(GL_TRIANGLES, gpu->second.index_count, GL_UNSIGNED_INT, nullptr);
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
}

usize draw_webgl_particles(
    const WebGLParticleRenderer& renderer,
    std::span<const ParticleDrawItem> draws,
    WebGLTextureArrayView atlas,
    const RenderView& view,
    u32 viewport_width,
    u32 viewport_height,
    ParticleRenderBlendMode blend_mode) {
  if (renderer.program == 0U || renderer.vao == 0U || renderer.vbo == 0U ||
      atlas.texture == 0U || draws.empty()) {
    return 0U;
  }

  std::vector<WebGLParticleInstanceData> instances;
  instances.reserve(draws.size());
  for (const ParticleDrawItem& draw : draws) {
    if (draw.blend_mode != blend_mode || draw.size <= 0.0F || draw.color.w <= 0.0F) {
      continue;
    }
    instances.push_back({
        draw.position,
        draw.size,
        draw.color,
        draw.uv_min,
        draw.uv_max,
        draw.atlas_page,
        draw.rotation_radians});
  }
  if (instances.empty()) {
    return 0U;
  }

  if (blend_mode == ParticleRenderBlendMode::Additive) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
  } else {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  const f32 aspect =
      static_cast<f32>(std::max(viewport_width, 1U)) /
      static_cast<f32>(std::max(viewport_height, 1U));
  glUseProgram(renderer.program);
  glUniform3f(
      renderer.camera_position_location,
      view.position.x,
      view.position.y,
      view.position.z);
  glUniform3f(
      renderer.camera_forward_location,
      view.forward.x,
      view.forward.y,
      view.forward.z);
  glUniform3f(
      renderer.camera_right_location,
      view.right.x,
      view.right.y,
      view.right.z);
  glUniform3f(
      renderer.camera_up_location,
      view.up.x,
      view.up.y,
      view.up.z);
  glUniform1f(renderer.camera_aspect_location, aspect);
  glUniform1f(renderer.camera_fov_y_location, view.fov_y_degrees);
  glUniform1f(renderer.camera_near_location, view.near_plane);
  glUniform1f(renderer.camera_far_location, view.far_plane);
  glUniform2f(
      renderer.atlas_size_location,
      static_cast<f32>(std::max(atlas.width, 1U)),
      static_cast<f32>(std::max(atlas.height, 1U)));
  glUniform1i(renderer.atlas_location, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, atlas.texture);

  glBindVertexArray(renderer.vao);
  glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(instances.size() * sizeof(WebGLParticleInstanceData)),
      instances.data(),
      GL_DYNAMIC_DRAW);
  glDrawArraysInstanced(
      GL_TRIANGLE_STRIP,
      0,
      4,
      static_cast<GLsizei>(instances.size()));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  return instances.size();
}

WebGLArrayMesh upload_webgl_color_mesh(
    std::span<const WebGLColorVertex> vertices,
    WebGLBufferUsage usage) {
  WebGLArrayMesh mesh;
  mesh.vertex_count = static_cast<i32>(vertices.size());
  if (vertices.empty()) {
    return mesh;
  }

  std::vector<float> interleaved;
  interleaved.reserve(vertices.size() * 6U);
  for (const WebGLColorVertex& vertex : vertices) {
    interleaved.push_back(vertex.position.x);
    interleaved.push_back(vertex.position.y);
    interleaved.push_back(vertex.position.z);
    interleaved.push_back(vertex.color.x);
    interleaved.push_back(vertex.color.y);
    interleaved.push_back(vertex.color.z);
  }

  GLuint vao{};
  GLuint vbo{};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
      interleaved.data(),
      usage == WebGLBufferUsage::Dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(0U, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      3,
      GL_FLOAT,
      GL_FALSE,
      6 * sizeof(float),
      reinterpret_cast<const void*>(3U * sizeof(float)));
  glBindBuffer(GL_ARRAY_BUFFER, 0U);
  glBindVertexArray(0U);

  mesh.vao = vao;
  mesh.vbo = vbo;
  return mesh;
}

WebGLIndexedMesh upload_webgl_chunk_mesh(const CpuMeshBuffer& mesh) {
  WebGLIndexedMesh gpu;
  if (mesh.vertex_bytes.empty() || mesh.indices.empty()) {
    return gpu;
  }

  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.vertex_bytes.size()),
      mesh.vertex_bytes.data(),
      GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(u32)),
      mesh.indices.data(),
      GL_STATIC_DRAW);

  glEnableVertexAttribArray(0U);
  glVertexAttribIPointer(
      0U,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, pos_ao)));
  glEnableVertexAttribArray(1U);
  glVertexAttribIPointer(
      1U,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribIPointer(
      2U,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, light_face_material_hints)));
  glEnableVertexAttribArray(3U);
  glVertexAttribIPointer(
      3U,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, atlas_page)));
  glBindVertexArray(0U);

  gpu.vao = vao;
  gpu.vbo = vbo;
  gpu.ebo = ebo;
  gpu.index_count = static_cast<i32>(mesh.indices.size());
  return gpu;
}

WebGLIndexedMesh upload_webgl_entity_model_mesh(const EntityModelAsset& model) {
  WebGLIndexedMesh gpu;
  if (model.vertices.empty() || model.indices.empty()) {
    return gpu;
  }

  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(model.vertices.size() * sizeof(EntityModelMeshVertex)),
      model.vertices.data(),
      GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(model.indices.size() * sizeof(u32)),
      model.indices.data(),
      GL_STATIC_DRAW);

  glEnableVertexAttribArray(0U);
  glVertexAttribPointer(
      0U,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, position)));
  glEnableVertexAttribArray(1U);
  glVertexAttribPointer(
      1U,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, uv)));
  glEnableVertexAttribArray(2U);
  glVertexAttribPointer(
      2U,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, color)));
  glEnableVertexAttribArray(3U);
  glVertexAttribIPointer(
      3U,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, bone_index)));
  glEnableVertexAttribArray(4U);
  glVertexAttribIPointer(
      4U,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, flags)));
  glEnableVertexAttribArray(5U);
  glVertexAttribIPointer(
      5U,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, texture_domain)));
  glEnableVertexAttribArray(6U);
  glVertexAttribIPointer(
      6U,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, atlas_page)));

  glBindVertexArray(0U);
  gpu.vao = vao;
  gpu.vbo = vbo;
  gpu.ebo = ebo;
  gpu.index_count = static_cast<i32>(model.indices.size());
  return gpu;
}

WebGLTextureArrayLimits query_webgl_texture_array_limits(
    u32 fallback_texture_size,
    u32 fallback_array_layers) {
  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);
  return {
      max_texture_size > 0 ? static_cast<u32>(max_texture_size) : fallback_texture_size,
      max_array_layers > 0 ? static_cast<u32>(max_array_layers) : fallback_array_layers};
}

WebGLTextureArrayUploadResult allocate_empty_webgl_texture_array(
    std::string_view label,
    u32 width,
    u32 height,
    u32 layers,
    WebGLTextureArraySampling sampling) {
  WebGLTextureArrayUploadResult result;
  if (width == 0U || height == 0U || layers == 0U) {
    result.error = std::string{label} + " allocation was empty";
    return result;
  }

  const WebGLTextureArrayLimits limits = query_webgl_texture_array_limits(width, layers);
  if (width > limits.max_texture_size ||
      height > limits.max_texture_size ||
      layers > limits.max_array_layers) {
    result.error = std::string{label} + " exceeds WebGL texture-array limits";
    return result;
  }

  GLuint texture{};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  switch (sampling) {
    case WebGLTextureArraySampling::Linear:
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
      break;
    case WebGLTextureArraySampling::NearestMipmap:
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
      glTexParameteri(
          GL_TEXTURE_2D_ARRAY,
          GL_TEXTURE_MAX_LEVEL,
          static_cast<GLint>(full_mipmap_max_level(width, height)));
      break;
    case WebGLTextureArraySampling::Nearest:
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
      break;
  }

  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(width),
      static_cast<GLsizei>(height),
      static_cast<GLsizei>(layers),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      nullptr);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);

  result.texture = texture;
  result.width = width;
  result.height = height;
  result.layers = layers;
  return result;
}

WebGLTextureArrayUploadResult upload_webgl_texture_array(
    std::string_view label,
    const TextureArrayUploadData& upload,
    WebGLTextureArraySampling sampling) {
  WebGLTextureArrayUploadResult result;
  if (!upload.valid()) {
    result.error = std::string{label} + " upload was empty";
    return result;
  }

  const WebGLTextureArrayLimits limits =
      query_webgl_texture_array_limits(upload.width, upload.layers);
  if (upload.width > limits.max_texture_size ||
      upload.height > limits.max_texture_size ||
      upload.layers > limits.max_array_layers) {
    result.error = std::string{label} + " exceeds WebGL texture-array limits";
    return result;
  }

  GLuint texture{};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  switch (sampling) {
    case WebGLTextureArraySampling::Linear:
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
      break;
    case WebGLTextureArraySampling::NearestMipmap: {
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
      const u32 full_max_level = full_mipmap_max_level(upload.width, upload.height);
      const u32 max_level = upload.limit_mipmap_levels
                                ? std::min(upload.max_mipmap_level, full_max_level)
                                : full_max_level;
      glTexParameteri(
          GL_TEXTURE_2D_ARRAY,
          GL_TEXTURE_MAX_LEVEL,
          static_cast<GLint>(max_level));
      break;
    }
    case WebGLTextureArraySampling::Nearest:
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
      break;
  }

  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(upload.width),
      static_cast<GLsizei>(upload.height),
      static_cast<GLsizei>(upload.layers),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      upload.rgba_pixels.data());
  if (sampling == WebGLTextureArraySampling::NearestMipmap) {
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  }
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);

  result.texture = texture;
  result.width = upload.width;
  result.height = upload.height;
  result.layers = upload.layers;
  return result;
}

WebGLTextureArrayRegionUploadResult upload_webgl_texture_array_region(
    std::string_view label,
    u32 texture,
    u32 x,
    u32 y,
    u32 layer,
    u32 width,
    u32 height,
    std::span<const u8> rgba_pixels) {
  WebGLTextureArrayRegionUploadResult result;
  if (texture == 0U || width == 0U || height == 0U ||
      rgba_pixels.size() != static_cast<usize>(width) * static_cast<usize>(height) * 4U) {
    result.error = std::string{label} + " region upload was invalid";
    return result;
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      static_cast<GLint>(x),
      static_cast<GLint>(y),
      static_cast<GLint>(layer),
      static_cast<GLsizei>(width),
      static_cast<GLsizei>(height),
      1,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      rgba_pixels.data());
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0U);
  return result;
}

} // namespace arv::render
