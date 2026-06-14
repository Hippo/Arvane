#include "arv/client/sky_rendering.hpp"

namespace arv::client {

render::SkyUniforms sky_uniforms_from_clock(
    const world::WorldClock& clock,
    const io::RenderConfig::SkyConfig& sky_config) noexcept {
  const world::CelestialState celestial = world::build_celestial_state(clock);
  render::SkyUniforms uniforms;
  uniforms.enabled = true;
  uniforms.sun_direction = celestial.sun_direction;
  uniforms.daylight_strength = celestial.daylight_strength;
  uniforms.moon_direction = celestial.moon_direction;
  uniforms.moonlight_strength = celestial.moonlight_strength;
  uniforms.daylight_color = celestial.daylight_color;
  uniforms.star_visibility = celestial.star_visibility * sky_config.star_intensity;
  uniforms.ambient_color = celestial.ambient_color;
  uniforms.day_fraction = celestial.day_fraction;
  uniforms.fog_color = celestial.fog_color;
  uniforms.cloud_time_seconds = world::day_time_seconds(clock);
  uniforms.cirrus_amount = sky_config.cirrus_amount;
  uniforms.cumulus_amount = sky_config.cumulus_amount;
  uniforms.cloud_intensity = sky_config.cloud_intensity;
  uniforms.exposure = 1.15F;
  return uniforms;
}

} // namespace arv::client
