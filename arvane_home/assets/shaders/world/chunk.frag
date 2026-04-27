#version 330 core
in vec3 v_color;
in vec2 v_uv;
uniform float u_alpha;
uniform sampler2D u_terrain_atlas;
uniform vec2 u_atlas_size;
out vec4 frag_color;

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_terrain_atlas, v_uv / atlas_size);
  frag_color = vec4(texel.rgb * v_color, texel.a * u_alpha);
}
