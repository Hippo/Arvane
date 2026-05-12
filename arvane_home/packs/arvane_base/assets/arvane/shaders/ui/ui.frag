#version 330 core
in vec2 v_uv;
in vec4 v_color;
flat in int v_texture_slot;
flat in uint v_atlas_page;
flat in vec2 v_rect_size;
flat in vec4 v_border_color;
flat in float v_radius;
flat in float v_border_width;
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

float rounded_rect_sdf(vec2 point, vec2 half_size, float radius) {
  radius = clamp(radius, 0.0, min(half_size.x, half_size.y));
  vec2 corner = abs(point) - (half_size - vec2(radius));
  return length(max(corner, vec2(0.0))) + min(max(corner.x, corner.y), 0.0) - radius;
}

vec4 shade_rounded_rect(vec4 fill_color) {
  vec2 size = max(v_rect_size, vec2(0.0));
  if (size.x <= 0.0 || size.y <= 0.0) {
    return vec4(0.0);
  }

  vec2 half_size = size * 0.5;
  float radius = clamp(v_radius, 0.0, min(half_size.x, half_size.y));
  float border_width = clamp(v_border_width, 0.0, min(half_size.x, half_size.y));
  vec2 point = v_uv * size - half_size;

  float outer_distance = rounded_rect_sdf(point, half_size, radius);
  float antialias = max(fwidth(outer_distance), 0.75);
  float outer_alpha = 1.0 - smoothstep(0.0, antialias, outer_distance);
  vec4 shape_color = fill_color;

  if (border_width > 0.0 && v_border_color.a > 0.0) {
    vec2 inner_half = max(half_size - vec2(border_width), vec2(0.0));
    float inner_radius = max(radius - border_width, 0.0);
    float inner_distance = rounded_rect_sdf(point, inner_half, inner_radius);
    float inner_alpha = 1.0 - smoothstep(0.0, antialias, inner_distance);
    shape_color = mix(v_border_color, fill_color, inner_alpha);
  }

  shape_color.a *= outer_alpha;
  return shape_color;
}

void main() {
  vec4 sampled = sample_ui_texture(v_texture_slot, v_uv, v_atlas_page);
  if (v_texture_slot == 0 && (v_radius > 0.0 || v_border_width > 0.0)) {
    frag_color = shade_rounded_rect(v_color);
    return;
  }
  frag_color = sampled * v_color;
}
