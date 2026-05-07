#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_color;
layout(location = 3) in uint a_bone_index;
layout(location = 4) in uint a_flags;
layout(location = 5) in vec3 a_instance_position;
layout(location = 6) in vec4 a_instance_rotation;
layout(location = 7) in float a_instance_scale;

uniform vec3 u_camera_position;
uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;
uniform float u_camera_near_plane;
uniform float u_camera_far_plane;
uniform int u_bone_count;
uniform vec3 u_bone_pivots[128];
uniform vec3 u_bone_rotations[128];
uniform vec3 u_bone_positions[128];

out vec2 v_uv;
out vec3 v_color;
flat out uint v_flags;

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

  vec3 scaled = model_position * (a_instance_scale / 16.0);
  vec3 world = a_instance_position + rotate_quat(scaled, a_instance_rotation);
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

  v_uv = a_uv;
  v_color = a_color;
  v_flags = a_flags;
  gl_Position = vec4(view_x * focal / aspect, view_y * focal, clip_z, view_z);
}
