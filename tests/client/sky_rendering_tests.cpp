#include "arv/client/sky_rendering.hpp"
#include "arv/world/world_time.hpp"

#include <gtest/gtest.h>

TEST(SkyRenderingTests, ConfiguresFrameSkyFromClockAndRenderSettings) {
  arv::world::WorldClock clock = arv::world::make_world_clock();
  arv::world::set_day_fraction(clock, 0.42F);

  arv::io::RenderConfig::SkyConfig config;
  config.star_intensity = 0.35F;
  config.cloud_intensity = 0.65F;
  config.cirrus_amount = 0.25F;
  config.cumulus_amount = 0.75F;

  arv::render::FramePacket frame;
  arv::client::configure_frame_sky(frame, clock, config);
  const arv::render::SkyUniforms expected =
      arv::client::sky_uniforms_from_clock(clock, config);

  EXPECT_TRUE(frame.sky.enabled);
  EXPECT_FLOAT_EQ(frame.sky.day_fraction, expected.day_fraction);
  EXPECT_FLOAT_EQ(frame.sky.star_visibility, expected.star_visibility);
  EXPECT_FLOAT_EQ(frame.sky.cloud_intensity, 0.65F);
  EXPECT_FLOAT_EQ(frame.sky.cirrus_amount, 0.25F);
  EXPECT_FLOAT_EQ(frame.sky.cumulus_amount, 0.75F);
  EXPECT_FLOAT_EQ(frame.sky.cloud_time_seconds, expected.cloud_time_seconds);
}
