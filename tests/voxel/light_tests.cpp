#include "arv/voxel/light.hpp"

#include <gtest/gtest.h>

TEST(LightTests, PacksRgbAndSkyIntoDocumentedSixteenBitLayout) {
  const arv::voxel::LightWord light = arv::voxel::pack_light(1, 2, 3, 4);

  EXPECT_EQ(arv::voxel::light_r(light), 1);
  EXPECT_EQ(arv::voxel::light_g(light), 2);
  EXPECT_EQ(arv::voxel::light_b(light), 3);
  EXPECT_EQ(arv::voxel::light_sky(light), 4);
  EXPECT_EQ(light, 0x4321);
}

TEST(LightTests, ClampsPackedChannelsToFourBits) {
  const arv::voxel::LightWord light = arv::voxel::pack_light(16, 99, 15, 255);

  EXPECT_EQ(arv::voxel::light_r(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_g(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_b(light), arv::voxel::MaxLightLevel);
  EXPECT_EQ(arv::voxel::light_sky(light), arv::voxel::MaxLightLevel);
}
