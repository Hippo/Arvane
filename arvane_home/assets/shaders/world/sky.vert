#version 330 core
out vec3 v_view_ray;

uniform vec3 u_camera_forward;
uniform vec3 u_camera_right;
uniform vec3 u_camera_up;
uniform float u_camera_aspect;
uniform float u_camera_fov_y_degrees;

const vec2 kQuad[4] = vec2[](
  vec2(-1.0,  1.0),
  vec2(-1.0, -1.0),
  vec2( 1.0,  1.0),
  vec2( 1.0, -1.0)
);

void main() {
  vec2 ndc = kQuad[gl_VertexID];
  float focal_spread = tan(radians(u_camera_fov_y_degrees) * 0.5);
  v_view_ray =
      u_camera_forward +
      u_camera_right * (ndc.x * max(u_camera_aspect, 0.01) * focal_spread) +
      u_camera_up * (ndc.y * focal_spread);
  gl_Position = vec4(ndc, 1.0, 1.0);
}
