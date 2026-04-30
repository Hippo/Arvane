#version 330 core
in vec3 v_color;
in vec2 v_uv;
flat in uint v_material_class;
flat in uint v_animation_id;
flat in uint v_material_flags;
uniform float u_alpha;
uniform float u_animation_time_seconds;
uniform sampler2D u_terrain_atlas;
uniform vec2 u_atlas_size;
out vec4 frag_color;

const uint VertexMaterialClassWater = 3u;
const uint VertexAnimationWaterStill = 1u;
const uint VertexAnimationWaterFlow = 2u;
const uint VertexAnimationWaterSurface = 3u;
const uint VertexMaterialFlagTintFromFluid = 2u;

vec3 fluid_tint_for_vertex(uint material_class) {
  // TODO(materials): resolve this through material/fluid/biome tint data.
  return material_class == VertexMaterialClassWater ? vec3(0.42, 0.66, 0.88) : vec3(1.0);
}

bool is_water_animation(uint animation_id) {
  return animation_id == VertexAnimationWaterStill ||
         animation_id == VertexAnimationWaterFlow ||
         animation_id == VertexAnimationWaterSurface;
}

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_terrain_atlas, v_uv / atlas_size);
  if ((v_material_flags & VertexMaterialFlagTintFromFluid) != 0u) {
    texel.rgb *= fluid_tint_for_vertex(v_material_class);
  }
  if (v_material_class == VertexMaterialClassWater && is_water_animation(v_animation_id)) {
    float speed = v_animation_id == VertexAnimationWaterFlow ? 2.8 : 1.2;
    float phase = dot(v_uv, vec2(0.07, 0.11)) + u_animation_time_seconds * speed;
    texel.rgb *= 0.96 + sin(phase) * 0.04;
  }
  frag_color = vec4(texel.rgb * v_color, texel.a * u_alpha);
}
