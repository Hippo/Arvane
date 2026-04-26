#version 330 core
layout(location = 0) in uint a_pos_ao;
layout(location = 1) in uint a_uv;
layout(location = 2) in uint a_light_face_flags;

uniform vec3 u_chunk_origin;
uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;
uniform float u_camera_near_plane;
uniform float u_camera_far_plane;

out vec3 v_color;

void main() {
  uint bx = a_pos_ao & 31u;
  uint by = (a_pos_ao >> 5u) & 31u;
  uint bz = (a_pos_ao >> 10u) & 31u;
  uint mx = (a_pos_ao >> 15u) & 31u;
  uint my = (a_pos_ao >> 20u) & 31u;
  uint mz = (a_pos_ao >> 25u) & 31u;
  uint ao = (a_pos_ao >> 30u) & 3u;

  uint red = a_light_face_flags & 15u;
  uint green = (a_light_face_flags >> 4u) & 15u;
  uint blue = (a_light_face_flags >> 8u) & 15u;
  uint sky = (a_light_face_flags >> 12u) & 15u;
  uint face = (a_light_face_flags >> 16u) & 7u;

  vec3 world = u_chunk_origin + vec3(bx, by, bz) + vec3(mx, my, mz) / 16.0;
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
  float ao_shade = min_ao_shade + float(ao) / 3.0 * (1.0 - min_ao_shade);
  float sky_shade = 0.2 + float(sky) / 15.0 * 0.8;
  vec3 block_light = vec3(red, green, blue) / 15.0;
  v_color = (vec3(0.46, 0.66, 0.34) * sky_shade + block_light * 0.35) * face_shade * ao_shade;
  gl_Position = vec4(view_x * focal / aspect, view_y * focal, clip_z, view_z);
}
