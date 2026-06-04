#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in uint v_atlas_page;

uniform sampler2DArray u_particle_atlas;
uniform vec2 u_atlas_size;

out vec4 frag_color;

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_particle_atlas, vec3(v_uv / atlas_size, float(v_atlas_page)));
  if (texel.a < 0.01) {
    discard;
  }
  frag_color = texel * v_color;
}
