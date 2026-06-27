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

void configure_frame_sky(
    render::FramePacket& frame,
    const world::WorldClock& clock,
    const io::RenderConfig::SkyConfig& sky_config) noexcept {
  frame.sky = sky_uniforms_from_clock(clock, sky_config);
}

void configure_frame_space_sky(render::FramePacket& frame) noexcept {
  frame.sky.enabled = true;
  frame.sky.sun_direction = {0.0F, 1.0F, 0.0F};
  frame.sky.daylight_strength = 0.05F;
  frame.sky.moon_direction = {0.0F, -1.0F, 0.0F};
  frame.sky.moonlight_strength = 0.0F;
  frame.sky.daylight_color = {0.60F, 0.72F, 1.0F};
  frame.sky.star_visibility = 2.4F;
  frame.sky.ambient_color = {0.02F, 0.025F, 0.04F};
  frame.sky.day_fraction = 0.5F;
  frame.sky.fog_color = {0.0F, 0.0F, 0.0F};
  frame.sky.cirrus_amount = 0.0F;
  frame.sky.cumulus_amount = 0.0F;
  frame.sky.cloud_intensity = 0.0F;
  frame.sky.exposure = 1.35F;
  frame.clear_color = {0.003F, 0.004F, 0.010F, 1.0F};
}

} // namespace arv::client
