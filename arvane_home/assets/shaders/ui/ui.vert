#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

uniform vec2 u_viewport_size;

out vec2 v_uv;
out vec4 v_color;

void main() {
  vec2 viewport = max(u_viewport_size, vec2(1.0, 1.0));
  vec2 ndc = vec2(
      (a_position.x / viewport.x) * 2.0 - 1.0,
      1.0 - (a_position.y / viewport.y) * 2.0);
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = vec4(ndc, 0.0, 1.0);
}
