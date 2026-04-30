#version 330 core
layout(location = 0) in uint a_pos_ao;
layout(location = 1) in uint a_uv;
layout(location = 2) in uint a_light_face_material_hints;

uniform vec3 u_chunk_origin;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;
uniform float u_camera_near_plane;
uniform float u_camera_far_plane;
uniform float u_animation_time_seconds;

out vec3 v_color;
out vec2 v_uv;
flat out uint v_material_class;
flat out uint v_animation_id;
flat out uint v_material_flags;

const uint ChunkLightMask = 15u;
const uint ChunkFaceMask = 7u;
const uint ChunkMaterialMask = 15u;
const uint ChunkMaterialFlagsMask = 31u;

const uint ChunkBlockLightRShift = 0u;
const uint ChunkBlockLightGShift = 4u;
const uint ChunkBlockLightBShift = 8u;
const uint ChunkSkyLightShift = 12u;
const uint ChunkFaceShift = 16u;
const uint ChunkMaterialClassShift = 19u;
const uint ChunkAnimationIdShift = 23u;
const uint ChunkMaterialFlagsShift = 27u;

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
  uint bx = a_pos_ao & 31u;
  uint by = (a_pos_ao >> 5u) & 31u;
  uint bz = (a_pos_ao >> 10u) & 31u;
  uint mx = (a_pos_ao >> 15u) & 31u;
  uint my = (a_pos_ao >> 20u) & 31u;
  uint mz = (a_pos_ao >> 25u) & 31u;
  uint ao = (a_pos_ao >> 30u) & 3u;

  uint red = (a_light_face_material_hints >> ChunkBlockLightRShift) & ChunkLightMask;
  uint green = (a_light_face_material_hints >> ChunkBlockLightGShift) & ChunkLightMask;
  uint blue = (a_light_face_material_hints >> ChunkBlockLightBShift) & ChunkLightMask;
  uint sky = (a_light_face_material_hints >> ChunkSkyLightShift) & ChunkLightMask;
  uint face = (a_light_face_material_hints >> ChunkFaceShift) & ChunkFaceMask;
  uint material_class = (a_light_face_material_hints >> ChunkMaterialClassShift) & ChunkMaterialMask;
  uint animation_id = (a_light_face_material_hints >> ChunkAnimationIdShift) & ChunkMaterialMask;
  uint material_flags =
      (a_light_face_material_hints >> ChunkMaterialFlagsShift) & ChunkMaterialFlagsMask;
  uint atlas_u = a_uv & 65535u;
  uint atlas_v = (a_uv >> 16u) & 65535u;

  vec3 world = u_chunk_origin + vec3(bx, by, bz) + vec3(mx, my, mz) / 16.0;
  if (is_water_animation(animation_id) && animation_id == VertexAnimationWaterSurface) {
    float wave_phase = dot(world.xz, vec2(1.73, 1.19)) + u_animation_time_seconds * 1.4;
    world.y += sin(wave_phase) * 0.025;
  }

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

  float face_shade = face == 2u ? 1.0 : (face == 3u ? 0.55 : 0.78);
  float min_ao_shade = face == 2u ? 0.86 : 0.70;
  bool disable_ao = (material_flags & VertexMaterialFlagDisableAo) != 0u;
  float ao_shade = disable_ao ? 1.0 : min_ao_shade + float(ao) / 3.0 * (1.0 - min_ao_shade);
  float sky_shade = 0.2 + float(sky) / 15.0 * 0.8;
  vec3 block_light = vec3(red, green, blue) / 15.0;
  vec3 sky_light = vec3(sky_shade);
  v_color = clamp(sky_light + block_light * 0.35, vec3(0.0), vec3(1.0)) * face_shade * ao_shade;
  v_uv = vec2(float(atlas_u), float(atlas_v));
  v_material_class = material_class;
  v_animation_id = animation_id;
  v_material_flags = material_flags;
  gl_Position = vec4(view_x * focal / aspect, view_y * focal, clip_z, view_z);
}
