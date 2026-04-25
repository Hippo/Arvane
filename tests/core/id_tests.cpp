#include "arv/core/id.hpp"

#include <gtest/gtest.h>

#include <unordered_map>

TEST(IDTests, BasicEquality) {
  struct TestTag {};
  using TestID = arv::ID<TestTag>;

  const TestID id1{42};
  const TestID id2{42};
  const TestID id3{7};

  EXPECT_EQ(id1, id2);
  EXPECT_NE(id1, id3);
}

TEST(IDTests, IDsAreHashable) {
  struct TestTag {};
  using TestID = arv::ID<TestTag>;

  std::unordered_map<TestID, const char*> values;
  values.emplace(TestID{3}, "three");

  EXPECT_STREQ(values.at(TestID{3}), "three");
}
