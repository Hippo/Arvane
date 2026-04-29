#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in int v_texture_slot;
uniform sampler2D u_textures[4];
out vec4 frag_color;

vec4 sample_ui_texture(int slot, vec2 uv) {
  if (slot == 1) {
    return texture(u_textures[1], uv);
  }
  if (slot == 2) {
    return texture(u_textures[2], uv);
  }
  if (slot == 3) {
    return texture(u_textures[3], uv);
  }
  return texture(u_textures[0], uv);
}

void main() {
  frag_color = sample_ui_texture(v_texture_slot, v_uv) * v_color;
}
