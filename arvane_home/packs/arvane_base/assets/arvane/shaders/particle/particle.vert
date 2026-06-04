#version 330 core
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
