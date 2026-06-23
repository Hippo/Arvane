#version 330 core
in vec2 v_unit;
in float v_alpha;
out vec4 frag_color;

void main() {
  float radius_sq = dot(v_unit, v_unit);
  if (radius_sq >= 1.0) {
    discard;
  }
  float feather = 1.0 - smoothstep(0.10, 1.0, radius_sq);
  frag_color = vec4(0.0, 0.0, 0.0, v_alpha * feather);
}
