#version 330 core
in vec2 v_uv;
in vec3 v_color;

uniform sampler2D u_entity_atlas;
uniform vec2 u_atlas_size;
uniform vec3 u_daylight_color;
uniform float u_daylight_strength;
uniform vec3 u_ambient_color;

out vec4 frag_color;

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_entity_atlas, v_uv / atlas_size);
  if (texel.a < 0.1) {
    discard;
  }
  vec3 world_light = clamp(
      u_ambient_color + u_daylight_color * u_daylight_strength,
      vec3(0.36),
      vec3(1.0));
  vec3 soft_entity_light = mix(vec3(1.0), world_light, 0.45);
  frag_color = vec4(texel.rgb * v_color * soft_entity_light, texel.a);
}
