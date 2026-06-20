#include "arv/render/webgl_shader.hpp"

#include <GLES3/gl3.h>

#include <array>
#include <utility>

namespace arv::render {
namespace {

static constexpr const char* sky_vertex_shader_source = R"(#version 300 es
precision highp float;
out vec3 v_view_ray;

uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;

void main() {
  vec2 ndc = vec2(
      (gl_VertexID == 2 || gl_VertexID == 3) ? 1.0 : -1.0,
      (gl_VertexID == 1 || gl_VertexID == 3) ? -1.0 : 1.0);
  float focal_spread = tan(radians(u_camera_fov_y_degrees) * 0.5);
  v_view_ray =
      u_camera_forward +
      u_camera_right * (ndc.x * max(u_camera_aspect, 0.01) * focal_spread) +
      u_camera_up * (ndc.y * focal_spread);
  gl_Position = vec4(ndc, 1.0, 1.0);
}
)";
static constexpr const char* sky_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
in vec3 v_view_ray;
out vec4 out_color;

uniform vec3 u_sun_direction;
uniform vec3 u_moon_direction;
uniform vec3 u_daylight_color;
uniform vec3 u_ambient_color;
uniform vec3 u_fog_color;
uniform float u_daylight_strength;
uniform float u_moonlight_strength;
uniform float u_star_visibility;
uniform float u_day_fraction;
uniform float u_cloud_time_seconds;
uniform float u_cirrus_amount;
uniform float u_cumulus_amount;
uniform float u_cloud_intensity;
uniform float u_exposure;

const float PI = 3.14159265359;
const float Br = 0.0025;
const float Bm = 0.0003;
const float MieG = 0.9800;
const vec3 Wavelength = vec3(0.650, 0.570, 0.475);
const float EffectEpsilon = 0.001;

float hash(float n) {
  return fract(sin(n) * 43758.5453123);
}

float noise(vec3 x) {
  vec3 f = fract(x);
  vec3 p = floor(x);
  float n = dot(p, vec3(1.0, 157.0, 113.0));
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(n +   0.0), hash(n +   1.0), f.x),
                 mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
             mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                 mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

const mat3 FbmMatrix = mat3(
   0.00,  1.60,  1.20,
  -1.60,  0.72, -0.96,
  -1.20, -0.96,  1.28
);

float fbm(vec3 p) {
  float f = 0.0;
  f += noise(p) * 0.500; p = FbmMatrix * p * 1.10;
  f += noise(p) * 0.250; p = FbmMatrix * p * 1.20;
  f += noise(p) * 0.166; p = FbmMatrix * p * 1.30;
  f += noise(p) * 0.083; p = FbmMatrix * p * 1.40;
  f += noise(p) * 0.042;
  return f;
}

vec3 view_direction() {
  return normalize(v_view_ray);
}

vec3 scattering_color(vec3 view_dir, float sun_height, float day_amount) {
  vec3 kr = Br / pow(Wavelength, vec3(4.0));
  vec3 km = Bm / pow(Wavelength, vec3(0.84));
  float horizon = max(view_dir.y, 0.0);
  float mu = dot(view_dir, u_sun_direction);
  float rayleigh = 3.0 / (8.0 * PI) * (1.0 + mu * mu);
  vec3 mie = (kr + km * (1.0 - MieG * MieG) /
      (2.0 + MieG * MieG) /
      pow(max(1.0 + MieG * MieG - 2.0 * MieG * mu, 0.001), 1.5)) / (Br + Bm);

  float low_horizon = exp(-horizon * 16.0) + 0.1;
  vec3 day_extinction =
      exp(-exp(-((horizon + sun_height * 4.0) *
                  low_horizon / 80.0) / Br) *
          low_horizon * kr / Br) *
      exp(-horizon * exp(-horizon * 8.0) * 4.0) *
      exp(-horizon * 2.0) * 4.0;

  vec3 sky = rayleigh * mie * day_extinction * u_daylight_color;
  vec3 zenith_tint = mix(u_fog_color, vec3(0.11, 0.34, 0.90) * u_daylight_color,
                         pow(clamp(horizon, 0.0, 1.0), 0.35));
  sky = mix(zenith_tint * u_daylight_strength, sky, 0.65);

  vec3 night = vec3(0.010, 0.016, 0.045) +
      u_ambient_color * 0.55 +
      vec3(0.08, 0.10, 0.15) * u_moonlight_strength *
          smoothstep(-0.08, 0.45, u_moon_direction.y);
  return mix(night, sky, day_amount);
}

float star_field(vec3 view_dir) {
  vec3 cell = floor(view_dir * 300.0);
  float seed = dot(cell, vec3(17.0, 59.0, 113.0));
  float star = smoothstep(0.9925, 1.0, hash(seed));
  float sparkle = 0.78 + 0.22 * sin(u_cloud_time_seconds * 0.45 + hash(seed + 19.0) * 6.28318);
  float scale = mix(0.55, 1.25, hash(seed + 11.0));
  return star * sparkle * scale;
}

vec3 apply_clouds(vec3 color, vec3 view_dir, float day_amount) {
  float cirrus_amount = clamp(u_cirrus_amount, 0.0, 1.0);
  float cumulus_amount = clamp(u_cumulus_amount, 0.0, 1.0);
  float cloud_intensity = clamp(u_cloud_intensity, 0.0, 3.0);
  if (cloud_intensity <= EffectEpsilon ||
      (cirrus_amount <= EffectEpsilon && cumulus_amount <= EffectEpsilon)) {
    return color;
  }

  float height_mask = smoothstep(-0.03, 0.16, view_dir.y);
  if (height_mask <= EffectEpsilon) {
    return color;
  }

  vec3 plane = view_dir / max(view_dir.y, 0.080);

  float cirrus = 0.0;
  if (cirrus_amount > EffectEpsilon) {
    vec3 cirrus_wind = vec3(u_cloud_time_seconds * 0.010, 0.0, u_cloud_time_seconds * 0.004);
    float cirrus_base = fbm(plane * 1.15 + cirrus_wind);
    float cirrus_detail = fbm(plane * 3.40 + cirrus_wind * 1.7 + vec3(0.0, 8.0, 0.0));
    cirrus = smoothstep(0.50, 0.78, cirrus_base + cirrus_detail * 0.18) *
        cirrus_amount * 0.55;
  }

  float cumulus = 0.0;
  if (cumulus_amount > EffectEpsilon) {
    vec3 cumulus_wind = vec3(u_cloud_time_seconds * 0.026, 4.2, u_cloud_time_seconds * 0.016);
    float cumulus_base = fbm(plane * 0.55 + cumulus_wind);
    float cumulus_detail = fbm(plane * 2.10 + cumulus_wind * 1.3 + vec3(7.0, 0.0, 0.0));
    float cumulus_shape = cumulus_base * 0.82 + cumulus_detail * 0.18;
    cumulus = smoothstep(0.48 - cumulus_amount * 0.22, 0.76, cumulus_shape) *
        cumulus_amount;
  }

  float density = clamp((cirrus + cumulus) * cloud_intensity, 0.0, 1.0) * height_mask;
  float sun_lit = smoothstep(0.15, 0.95, dot(view_dir, u_sun_direction)) *
      day_amount;
  float moon_lit = smoothstep(-0.02, 0.42, u_moon_direction.y) *
      u_moonlight_strength * (1.0 - day_amount);
  vec3 cloud_shadow = mix(
      vec3(0.035, 0.040, 0.065) + u_ambient_color * 0.65,
      u_fog_color * 0.92 + u_daylight_color * 0.15,
      day_amount);
  vec3 cloud_bright = mix(
      vec3(0.10, 0.12, 0.18) + u_ambient_color * 0.50,
      u_daylight_color * 0.92 + u_fog_color * 0.70,
      day_amount);
  vec3 cloud_light = mix(
      cloud_shadow,
      cloud_bright,
      clamp(day_amount * 0.38 + sun_lit * 0.35 + moon_lit * 1.60, 0.0, 0.85));
  float opacity = density * (0.56 - day_amount * 0.16 + day_amount * 0.42);
  return mix(color, cloud_light, clamp(opacity, 0.0, 0.88));
}

vec3 apply_moon(vec3 color, vec3 view_dir, float day_amount) {
  if (u_moon_direction.y <= -0.02) {
    return color;
  }

  float moon_dot = dot(view_dir, u_moon_direction);
  float core = smoothstep(0.99955, 0.99988, moon_dot);
  float rim = smoothstep(0.99840, 0.99960, moon_dot) * (1.0 - core) * 0.65;
  float glow = smoothstep(0.98600, 0.99920, moon_dot) * 0.18;
  float horizon = smoothstep(-0.02, 0.10, u_moon_direction.y) *
      smoothstep(-0.02, 0.08, view_dir.y);
  float night_visibility = (1.0 - smoothstep(0.02, 0.30, u_sun_direction.y)) *
      (1.0 - day_amount * 0.35);
  vec3 moon_color = mix(vec3(0.78, 0.84, 1.0), vec3(1.0, 0.94, 0.82), 0.65);
  float strength = (core * 1.65 + rim * 0.55 + glow) *
      horizon * night_visibility * (0.55 + u_moonlight_strength * 3.0);
  return color + moon_color * strength;
}

void main() {
  vec3 view_dir = view_direction();
  float sun_height = u_sun_direction.y;
  float day_amount = smoothstep(-0.08, 0.16, sun_height);
  float above_horizon = smoothstep(-0.02, 0.10, view_dir.y);

  vec3 color = scattering_color(view_dir, sun_height, day_amount);
  float star_visibility = max(u_star_visibility, 0.0);
  if (star_visibility > EffectEpsilon && above_horizon > EffectEpsilon) {
    color += vec3(0.70, 0.78, 1.0) *
        star_field(view_dir) * star_visibility * above_horizon;
  }
  color = apply_clouds(color, view_dir, day_amount);
  color = apply_moon(color, view_dir, day_amount);

  color = mix(u_fog_color * 0.75, color, smoothstep(-0.10, 0.18, view_dir.y));
  color += hash(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) + u_day_fraction * 31.0) * 0.006;
  color = vec3(1.0) - exp(-max(color, vec3(0.0)) * max(u_exposure, 0.01));
  out_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)";
static constexpr const char* chunk_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in uint a_pos_ao;
layout(location = 1) in uint a_uv;
layout(location = 2) in uint a_light_face_material;
layout(location = 3) in uint a_atlas_page;
uniform mat4 u_mvp;
uniform vec3 u_chunk_origin;
uniform vec2 u_atlas_inv_size;
uniform float u_animation_time_seconds;
out vec2 v_uv;
out float v_light;
flat out uint v_atlas_page;
flat out uint v_material_class;
flat out uint v_animation_id;
flat out uint v_material_flags;
const uint VertexAnimationWaterStill = 1u;
const uint VertexAnimationWaterFlow = 2u;
const uint VertexAnimationWaterSurface = 3u;
const uint VertexMaterialFlagDisableAo = 8u;
bool is_water_animation(uint animation_id) {
  return animation_id == VertexAnimationWaterStill ||
         animation_id == VertexAnimationWaterFlow ||
         animation_id == VertexAnimationWaterSurface;
}
void main() {
  uint block_x = a_pos_ao & 31u;
  uint block_y = (a_pos_ao >> 5u) & 31u;
  uint block_z = (a_pos_ao >> 10u) & 31u;
  uint model_x = (a_pos_ao >> 15u) & 31u;
  uint model_y = (a_pos_ao >> 20u) & 31u;
  uint model_z = (a_pos_ao >> 25u) & 31u;
  uint ao = (a_pos_ao >> 30u) & 3u;
  vec3 local_pos = vec3(block_x, block_y, block_z) +
      vec3(model_x, model_y, model_z) * (1.0 / 16.0);
  uint animation_id = (a_light_face_material >> 23u) & 15u;
  if (is_water_animation(animation_id) && animation_id == VertexAnimationWaterSurface) {
    float wave_phase =
        dot((u_chunk_origin + local_pos).xz, vec2(1.73, 1.19)) +
        u_animation_time_seconds * 1.4;
    local_pos.y += sin(wave_phase) * 0.025;
  }
  uint atlas_u = a_uv & 65535u;
  uint atlas_v = (a_uv >> 16u) & 65535u;
  v_uv = (vec2(float(atlas_u), float(atlas_v)) + vec2(0.5)) * u_atlas_inv_size;
  v_atlas_page = a_atlas_page;
  v_material_class = (a_light_face_material >> 19u) & 15u;
  v_animation_id = animation_id;
  v_material_flags = (a_light_face_material >> 27u) & 31u;

  float block_r = float((a_light_face_material >> 0u) & 15u);
  float block_g = float((a_light_face_material >> 4u) & 15u);
  float block_b = float((a_light_face_material >> 8u) & 15u);
  float sky = float((a_light_face_material >> 12u) & 15u);
  float light = max(max(max(block_r, block_g), block_b), sky) / 15.0;
  bool disable_ao = (v_material_flags & VertexMaterialFlagDisableAo) != 0u;
  float ao_factor = disable_ao ? 1.0 : mix(0.58, 1.0, float(ao) / 3.0);
  v_light = clamp(light, 0.18, 1.0) * ao_factor;
  gl_Position = u_mvp * vec4(u_chunk_origin + local_pos, 1.0);
}
)";
static constexpr const char* chunk_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_chunk_atlas;
uniform float u_alpha;
uniform float u_animation_time_seconds;
in vec2 v_uv;
in float v_light;
flat in uint v_atlas_page;
flat in uint v_material_class;
flat in uint v_animation_id;
flat in uint v_material_flags;
out vec4 out_color;
const uint VertexMaterialClassCutout = 1u;
const uint VertexMaterialClassWater = 3u;
const uint VertexMaterialClassFoliage = 5u;
const uint VertexAnimationWaterStill = 1u;
const uint VertexAnimationWaterFlow = 2u;
const uint VertexAnimationWaterSurface = 3u;
const uint VertexMaterialFlagTintFromBiome = 1u;
const uint VertexMaterialFlagTintFromFluid = 2u;
vec3 biome_tint_for_vertex(uint material_class) {
  return material_class == VertexMaterialClassFoliage ? vec3(0.42, 0.68, 0.28) : vec3(1.0);
}
vec3 fluid_tint_for_vertex(uint material_class) {
  return material_class == VertexMaterialClassWater ? vec3(0.42, 0.66, 0.88) : vec3(1.0);
}
bool is_water_animation(uint animation_id) {
  return animation_id == VertexAnimationWaterStill ||
         animation_id == VertexAnimationWaterFlow ||
         animation_id == VertexAnimationWaterSurface;
}
void main() {
  vec4 texel = texture(u_chunk_atlas, vec3(v_uv, float(v_atlas_page)));
  bool water = v_material_class == VertexMaterialClassWater;
  if (texel.a < (water ? 0.01 : 0.1)) {
    discard;
  }
  if ((v_material_flags & VertexMaterialFlagTintFromBiome) != 0u) {
    texel.rgb *= biome_tint_for_vertex(v_material_class);
  }
  if ((v_material_flags & VertexMaterialFlagTintFromFluid) != 0u) {
    texel.rgb *= fluid_tint_for_vertex(v_material_class);
  }
  float alpha = water ? max(texel.a * u_alpha, 0.42) : texel.a * u_alpha;
  float shimmer = water && is_water_animation(v_animation_id)
      ? sin(u_animation_time_seconds * 1.7 + v_uv.x * 96.0 + v_uv.y * 64.0) * 0.035
      : 0.0;
  float lit = water ? clamp(max(v_light, 0.72) + shimmer, 0.0, 1.0) : v_light;
  out_color = vec4(texel.rgb * lit, alpha);
}
)";
static constexpr const char* color_vertex_shader_source = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;
void main() {
  v_color = a_color;
  gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";
static constexpr const char* color_fragment_shader_source = R"(#version 300 es
precision highp float;
in vec3 v_color;
out vec4 out_color;
void main() {
  out_color = vec4(v_color, 1.0);
}
)";
static constexpr const char* entity_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_color;
layout(location = 3) in uint a_bone_index;
layout(location = 4) in uint a_flags;
layout(location = 5) in uint a_texture_domain;
layout(location = 6) in uint a_atlas_page;
uniform mat4 u_mvp;
uniform vec3 u_instance_position;
uniform vec4 u_instance_rotation;
uniform float u_instance_scale;
uniform int u_bone_count;
uniform vec3 u_bone_pivots[128];
uniform vec3 u_bone_rotations[128];
uniform vec3 u_bone_positions[128];
uniform vec4 u_player_skin_region;
uniform int u_player_skin_page;
out vec2 v_uv;
out vec3 v_color;
flat out uint v_flags;
flat out uint v_texture_domain;
flat out uint v_atlas_page;
out vec4 v_player_skin_region;

vec3 rotate_euler_degrees(vec3 value, vec3 degrees) {
  vec3 r = radians(degrees);
  float cx = cos(r.x);
  float sx = sin(r.x);
  float cy = cos(r.y);
  float sy = sin(r.y);
  float cz = cos(r.z);
  float sz = sin(r.z);

  value = vec3(value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx);
  value = vec3(value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy);
  value = vec3(value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z);
  return value;
}

vec3 rotate_quat(vec3 value, vec4 rotation) {
  vec3 qv = rotation.xyz;
  vec3 t = cross(qv, value) * 2.0;
  return value + t * rotation.w + cross(qv, t);
}

void main() {
  vec3 model_position = a_position;
  int bone_index = int(a_bone_index);
  if (bone_index >= 0 && bone_index < u_bone_count) {
    vec3 pivot = u_bone_pivots[bone_index];
    model_position =
        pivot +
        rotate_euler_degrees(model_position - pivot, u_bone_rotations[bone_index]) +
        u_bone_positions[bone_index];
  }

  vec3 scaled = model_position * (u_instance_scale / 16.0);
  vec3 world = u_instance_position + rotate_quat(scaled, u_instance_rotation);
  v_uv = a_uv;
  v_color = a_color;
  v_flags = a_flags;
  v_texture_domain = a_texture_domain;
  v_atlas_page = a_texture_domain == 2u ? uint(u_player_skin_page) : a_atlas_page;
  v_player_skin_region = u_player_skin_region;
  gl_Position = u_mvp * vec4(world, 1.0);
}
)";
static constexpr const char* entity_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_entity_atlas;
uniform sampler2DArray u_player_skin_atlas;
uniform vec2 u_atlas_size;
uniform vec2 u_player_skin_atlas_size;
in vec2 v_uv;
in vec3 v_color;
flat in uint v_flags;
flat in uint v_texture_domain;
flat in uint v_atlas_page;
in vec4 v_player_skin_region;
out vec4 out_color;

vec4 sample_entity_texel() {
  if (v_texture_domain == 2u &&
      v_player_skin_region.z > 0.0 &&
      v_player_skin_region.w > 0.0) {
    vec2 skin_atlas_size = max(u_player_skin_atlas_size, vec2(1.0, 1.0));
    vec2 skin_uv =
        (v_player_skin_region.xy + v_uv * v_player_skin_region.zw) / skin_atlas_size;
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
  vec3 light = vec3(0.86, 0.88, 0.92);
  out_color = vec4(texel.rgb * v_color * light, texel.a);
}
)";
static constexpr const char* particle_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec3 a_instance_position;
layout(location = 1) in float a_instance_size;
layout(location = 2) in vec4 a_instance_color;
layout(location = 3) in vec2 a_instance_uv_min;
layout(location = 4) in vec2 a_instance_uv_max;
layout(location = 5) in uint a_instance_atlas_page;
layout(location = 6) in float a_instance_rotation;

uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;
uniform float u_camera_near_plane;
uniform float u_camera_far_plane;

out vec2 v_uv;
out vec4 v_color;
flat out uint v_atlas_page;

void main() {
  vec2 corners[4] = vec2[4](
      vec2(-0.5, -0.5),
      vec2(0.5, -0.5),
      vec2(-0.5, 0.5),
      vec2(0.5, 0.5));
  vec2 uvs[4] = vec2[4](
      vec2(a_instance_uv_min.x, a_instance_uv_max.y),
      a_instance_uv_max,
      a_instance_uv_min,
      vec2(a_instance_uv_max.x, a_instance_uv_min.y));

  vec2 corner = corners[gl_VertexID & 3] * a_instance_size;
  float c = cos(a_instance_rotation);
  float s = sin(a_instance_rotation);
  corner = vec2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);

  vec3 world = a_instance_position +
      u_camera_right * corner.x +
      u_camera_up * corner.y;
  vec3 relative = world - u_camera_position;
  float view_x = dot(relative, u_camera_right);
  float view_y = dot(relative, u_camera_up);
  float view_z = dot(relative, u_camera_forward);

  float aspect = max(u_camera_aspect, 0.01);
  float focal = 1.0 / tan(radians(u_camera_fov_y_degrees) * 0.5);
  float near_plane = max(u_camera_near_plane, 0.001);
  float far_plane = max(u_camera_far_plane, near_plane + 1.0);
  float clip_z = ((far_plane + near_plane) / (far_plane - near_plane)) * view_z -
                 (2.0 * far_plane * near_plane) / (far_plane - near_plane);

  v_uv = uvs[gl_VertexID & 3];
  v_color = a_instance_color;
  v_atlas_page = a_instance_atlas_page;
  gl_Position = vec4(view_x * focal / aspect, view_y * focal, clip_z, view_z);
}
)";
static constexpr const char* particle_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
in vec2 v_uv;
in vec4 v_color;
flat in uint v_atlas_page;

uniform sampler2DArray u_particle_atlas;
uniform vec2 u_atlas_size;

out vec4 out_color;

void main() {
  vec2 atlas_size = max(u_atlas_size, vec2(1.0, 1.0));
  vec4 texel = texture(u_particle_atlas, vec3(v_uv / atlas_size, float(v_atlas_page)));
  if (texel.a < 0.01) {
    discard;
  }
  out_color = texel * v_color;
}
)";
static constexpr const char* ui_vertex_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_page;
uniform vec2 u_viewport;
out vec2 v_uv;
out vec4 v_color;
flat out int v_page;
void main() {
  vec2 viewport = max(u_viewport, vec2(1.0, 1.0));
  vec2 ndc = vec2(
      (a_position.x / viewport.x) * 2.0 - 1.0,
      1.0 - (a_position.y / viewport.y) * 2.0);
  v_uv = a_uv;
  v_color = a_color;
  v_page = int(a_page + 0.5);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)";
static constexpr const char* ui_fragment_shader_source = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2DArray;
uniform sampler2DArray u_atlas;
uniform int u_mode;
in vec2 v_uv;
in vec4 v_color;
flat in int v_page;
out vec4 out_color;
void main() {
  vec4 color = v_color;
  if (u_mode == 1) {
    color *= texture(u_atlas, vec3(v_uv, float(v_page)));
  }
  if (color.a <= 0.01) {
    discard;
  }
  out_color = color;
}
)";


[[nodiscard]] std::string shader_info_log(GLuint shader) {
  std::array<char, 512> log{};
  GLsizei length{};
  glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
  return std::string{log.data()};
}

[[nodiscard]] std::string program_info_log(GLuint program) {
  std::array<char, 512> log{};
  GLsizei length{};
  glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
  return std::string{log.data()};
}

[[nodiscard]] GLuint compile_shader(GLenum kind, const char* source, std::string& error) {
  const GLuint shader = glCreateShader(kind);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) {
    return shader;
  }

  error = std::string{"shader compile failed: "} + shader_info_log(shader);
  glDeleteShader(shader);
  return 0U;
}

} // namespace

WebGLProgramLinkResult link_webgl_program(
    std::string_view label,
    const char* vertex_source,
    const char* fragment_source) {
  WebGLProgramLinkResult result;

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, result.error);
  if (vertex_shader == 0U) {
    return result;
  }

  const GLuint fragment_shader =
      compile_shader(GL_FRAGMENT_SHADER, fragment_source, result.error);
  if (fragment_shader == 0U) {
    glDeleteShader(vertex_shader);
    return result;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    result.error = std::string{label} + " link failed: " + program_info_log(program);
    glDeleteProgram(program);
    return result;
  }

  result.program = program;
  return result;
}


WebGLProgramSetLinkResult link_default_webgl_programs() {
  WebGLProgramSetLinkResult result;

  const auto link_program = [&](
      std::string_view label,
      const char* vertex_source,
      const char* fragment_source) -> u32 {
    WebGLProgramLinkResult link_result =
        link_webgl_program(label, vertex_source, fragment_source);
    if (!link_result) {
      result.error = std::move(link_result.error);
      return 0U;
    }
    return link_result.program;
  };

  result.sky_program = link_program(
      "sky renderer",
      sky_vertex_shader_source,
      sky_fragment_shader_source);
  if (result.sky_program == 0U) {
    return result;
  }

  result.chunk_program = link_program(
      "chunk renderer",
      chunk_vertex_shader_source,
      chunk_fragment_shader_source);
  if (result.chunk_program == 0U) {
    return result;
  }

  result.color_program = link_program(
      "color renderer",
      color_vertex_shader_source,
      color_fragment_shader_source);
  if (result.color_program == 0U) {
    return result;
  }

  result.entity_program = link_program(
      "entity renderer",
      entity_vertex_shader_source,
      entity_fragment_shader_source);
  if (result.entity_program == 0U) {
    return result;
  }

  result.particle_program = link_program(
      "particle renderer",
      particle_vertex_shader_source,
      particle_fragment_shader_source);
  if (result.particle_program == 0U) {
    return result;
  }

  result.ui_program = link_program(
      "ui renderer",
      ui_vertex_shader_source,
      ui_fragment_shader_source);
  return result;
}


} // namespace arv::render
