#version 330 core
out vec2 v_ndc;

const vec2 kQuad[4] = vec2[](
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0, -1.0)
);

void main() {
  v_ndc = kQuad[gl_VertexID];
  gl_Position = vec4(v_ndc, 0.0, 1.0);
}
