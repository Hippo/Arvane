#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in int v_texture_slot;
flat in uint v_atlas_page;
uniform sampler2DArray u_textures[5];
out vec4 frag_color;

vec4 sample_ui_texture(int slot, vec2 uv, uint page) {
  if (slot == 1) {
    return texture(u_textures[1], vec3(uv, float(page)));
  }
  if (slot == 2) {
    return texture(u_textures[2], vec3(uv, float(page)));
  }
  if (slot == 3) {
    return texture(u_textures[3], vec3(uv, float(page)));
  }
  if (slot == 4) {
    return texture(u_textures[4], vec3(uv, float(page)));
  }
  return texture(u_textures[0], vec3(uv, float(page)));
}

void main() {
  frag_color = sample_ui_texture(v_texture_slot, v_uv, v_atlas_page) * v_color;
}
