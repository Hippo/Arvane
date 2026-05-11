#version 330 core
in vec3 v_view_ray;
out vec4 frag_color;

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
const vec3 Kr = Br / pow(Wavelength, vec3(4.0));
const vec3 Km = Bm / pow(Wavelength, vec3(0.84));
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
  float horizon = max(view_dir.y, 0.0);
  float mu = dot(view_dir, u_sun_direction);
  float rayleigh = 3.0 / (8.0 * PI) * (1.0 + mu * mu);
  vec3 mie = (Kr + Km * (1.0 - MieG * MieG) /
      (2.0 + MieG * MieG) /
      pow(max(1.0 + MieG * MieG - 2.0 * MieG * mu, 0.001), 1.5)) / (Br + Bm);

  float low_horizon = exp(-horizon * 16.0) + 0.1;
  vec3 day_extinction =
      exp(-exp(-((horizon + sun_height * 4.0) *
                  low_horizon / 80.0) / Br) *
          low_horizon * Kr / Br) *
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
  frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
