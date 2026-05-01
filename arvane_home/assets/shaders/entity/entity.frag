#version 330 core
in vec2 v_uv;
in vec3 v_color;

uniform sampler2D u_entity_atlas;
uniform vec2 u_atlas_size;

out vec4 frag_color;

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_entity_atlas, v_uv / atlas_size);
  if (texel.a < 0.1) {
    discard;
  }
  frag_color = vec4(texel.rgb * v_color, texel.a);
}
