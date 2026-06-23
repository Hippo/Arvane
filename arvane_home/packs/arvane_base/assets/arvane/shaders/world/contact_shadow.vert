#version 330 core
layout(location = 0) in vec3 a_center;
layout(location = 1) in vec2 a_half_extents;
layout(location = 2) in float a_alpha;

uniform mat4 u_view_projection;

out vec2 v_unit;
out float v_alpha;

const vec2 kCorners[6] = vec2[](
  vec2(-1.0, -1.0),
  vec2( 1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0,  1.0),
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0)
);

void main() {
  vec2 unit = kCorners[gl_VertexID];
  vec3 world = a_center + vec3(unit.x * a_half_extents.x, 0.0, unit.y * a_half_extents.y);
  v_unit = unit;
  v_alpha = clamp(a_alpha, 0.0, 1.0);
  gl_Position = u_view_projection * vec4(world, 1.0);
}
