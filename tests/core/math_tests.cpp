#include "arv/core/math.hpp"

#include <gtest/gtest.h>

TEST(MathTests, Vec3ArithmeticAndDotProduct) {
  const arv::Vec3f a{1.0F, 2.0F, 3.0F};
  const arv::Vec3f b{4.0F, 5.0F, 6.0F};

  EXPECT_EQ(a + b, (arv::Vec3f{5.0F, 7.0F, 9.0F}));
  EXPECT_EQ(b - a, (arv::Vec3f{3.0F, 3.0F, 3.0F}));
  EXPECT_FLOAT_EQ(arv::dot(a, b), 32.0F);
}

TEST(MathTests, NormalizesNonZeroVector) {
  const arv::Vec3f normalized = arv::normalize_or_zero({3.0F, 0.0F, 4.0F});

  EXPECT_NEAR(normalized.x, 0.6F, 0.0001F);
  EXPECT_NEAR(normalized.y, 0.0F, 0.0001F);
  EXPECT_NEAR(normalized.z, 0.8F, 0.0001F);
}

TEST(MathTests, AabbOverlapUsesOpenBounds) {
  const arv::Aabb3f a{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
  const arv::Aabb3f overlapping{{0.5F, 0.5F, 0.5F}, {1.5F, 1.5F, 1.5F}};
  const arv::Aabb3f touching{{1.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F}};

  EXPECT_TRUE(a.overlaps(overlapping));
  EXPECT_FALSE(a.overlaps(touching));
}
