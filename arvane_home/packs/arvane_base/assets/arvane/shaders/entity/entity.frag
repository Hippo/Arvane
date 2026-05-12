#version 330 core
in vec2 v_uv;
in vec3 v_color;
flat in uint v_flags;
flat in uint v_texture_domain;
flat in uint v_atlas_page;
in vec4 v_player_skin_region;

uniform sampler2DArray u_entity_atlas;
uniform sampler2DArray u_player_skin_atlas;
uniform vec2 u_atlas_size;
uniform vec2 u_player_skin_atlas_size;
uniform vec3 u_daylight_color;
uniform float u_daylight_strength;
uniform vec3 u_ambient_color;

out vec4 frag_color;

vec4 sample_entity_texel() {
  if (v_texture_domain == 2u && v_player_skin_region.z > 0.0 && v_player_skin_region.w > 0.0) {
    vec2 atlas_size = max(u_player_skin_atlas_size, vec2(1.0, 1.0));
    vec2 skin_uv = (v_player_skin_region.xy + v_uv * v_player_skin_region.zw) / atlas_size;
    return texture(u_player_skin_atlas, vec3(skin_uv, float(v_atlas_page)));
  }

  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  return texture(u_entity_atlas, vec3(v_uv / atlas_size, float(v_atlas_page)));
}

void main() {
  vec4 texel = sample_entity_texel();
  bool skin_base = (v_flags & 1u) != 0u;
  bool skin_decor = (v_flags & 2u) != 0u;
  if (skin_base && texel.a < 0.1) {
    texel = vec4(0.0, 0.0, 0.0, 1.0);
  } else if ((skin_decor || !skin_base) && texel.a < 0.1) {
    discard;
  }
  vec3 world_light = clamp(
      u_ambient_color + u_daylight_color * u_daylight_strength,
      vec3(0.36),
      vec3(1.0));
  vec3 soft_entity_light = mix(vec3(1.0), world_light, 0.45);
  frag_color = vec4(texel.rgb * v_color * soft_entity_light, texel.a);
}
