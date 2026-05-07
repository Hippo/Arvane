#include "arv/core/rate_limiter.hpp"

#include <gtest/gtest.h>

TEST(RateLimiterTests, TokenBucketConsumesAndRefills) {
  arv::core::TokenBucket bucket{3.0, 1.0};

  EXPECT_TRUE(bucket.try_consume(1.0, 0.0));
  EXPECT_TRUE(bucket.try_consume(2.0, 0.0));
  EXPECT_FALSE(bucket.try_consume(1.0, 0.0));

  EXPECT_DOUBLE_EQ(bucket.available(0.5), 0.5);
  EXPECT_FALSE(bucket.try_consume(1.0, 0.5));
  EXPECT_TRUE(bucket.try_consume(1.0, 1.0));
}

TEST(RateLimiterTests, TokenBucketCapsAtCapacity) {
  arv::core::TokenBucket bucket{2.0, 10.0};

  EXPECT_TRUE(bucket.try_consume(2.0, 0.0));
  EXPECT_DOUBLE_EQ(bucket.available(10.0), 2.0);
  EXPECT_TRUE(bucket.try_consume(2.0, 10.0));
  EXPECT_FALSE(bucket.try_consume(0.1, 10.0));
}
