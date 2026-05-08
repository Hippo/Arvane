#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_texture_slot;
layout(location = 4) in uint a_atlas_page;
layout(location = 5) in vec2 a_rect_size;
layout(location = 6) in vec4 a_border_color;
layout(location = 7) in float a_radius;
layout(location = 8) in float a_border_width;

uniform vec2 u_viewport_size;

out vec2 v_uv;
out vec4 v_color;
flat out int v_texture_slot;
flat out uint v_atlas_page;
flat out vec2 v_rect_size;
flat out vec4 v_border_color;
flat out float v_radius;
flat out float v_border_width;

void main() {
  vec2 viewport = max(u_viewport_size, vec2(1.0, 1.0));
  vec2 ndc = vec2(
      (a_position.x / viewport.x) * 2.0 - 1.0,
      1.0 - (a_position.y / viewport.y) * 2.0);
  v_uv = a_uv;
  v_color = a_color;
  v_texture_slot = int(a_texture_slot + 0.5);
  v_atlas_page = a_atlas_page;
  v_rect_size = a_rect_size;
  v_border_color = a_border_color;
  v_radius = a_radius;
  v_border_width = a_border_width;
  gl_Position = vec4(ndc, 0.0, 1.0);
}
