#include "arv/client/block_container_animation.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] arv::protocol::ContainerBlockSource make_source(
    arv::i32 x = 1,
    arv::i32 y = 2,
    arv::i32 z = 3) {
  return {arv::WorldID{7}, arv::BlockPos{{x, y, z}}};
}

} // namespace

TEST(BlockContainerAnimationTests, OpeningContainerStartsOpenClip) {
  arv::client::BlockContainerAnimationState state;
  const arv::protocol::ContainerBlockSource source = make_source();

  arv::client::update_block_container_animation(state, source, 3.0);

  ASSERT_TRUE(state.last_source.has_value());
  EXPECT_EQ(state.last_source.value(), source);
  ASSERT_TRUE(state.active_source.has_value());
  EXPECT_EQ(state.active_source.value(), source);
  EXPECT_EQ(state.active_clip, arv::client::BlockContainerOpenAnimation);
  EXPECT_DOUBLE_EQ(state.active_started_at, 3.0);

  arv::render::EntityDrawItem item;
  arv::client::append_block_container_animation_playback(item, state, source, 3.25);
  ASSERT_EQ(item.animations.size(), 1U);
  EXPECT_EQ(item.animations.front().name, arv::client::BlockContainerOpenAnimation);
  EXPECT_FLOAT_EQ(item.animations.front().time_seconds, 0.25F);
}

TEST(BlockContainerAnimationTests, UnchangedSourceDoesNotRestartClip) {
  arv::client::BlockContainerAnimationState state;
  const arv::protocol::ContainerBlockSource source = make_source();

  arv::client::update_block_container_animation(state, source, 3.0);
  arv::client::update_block_container_animation(state, source, 4.0);

  EXPECT_DOUBLE_EQ(state.active_started_at, 3.0);
  EXPECT_EQ(state.active_clip, arv::client::BlockContainerOpenAnimation);
}

TEST(BlockContainerAnimationTests, ClosingContainerKeepsCloseClipUntilDurationExpires) {
  arv::client::BlockContainerAnimationState state;
  const arv::protocol::ContainerBlockSource source = make_source();

  arv::client::update_block_container_animation(state, source, 1.0);
  arv::client::update_block_container_animation(state, std::nullopt, 2.0);

  EXPECT_FALSE(state.last_source.has_value());
  ASSERT_TRUE(state.active_source.has_value());
  EXPECT_EQ(state.active_source.value(), source);
  EXPECT_EQ(state.active_clip, arv::client::BlockContainerCloseAnimation);

  arv::render::EntityDrawItem item;
  arv::client::append_block_container_animation_playback(item, state, source, 2.5);
  ASSERT_EQ(item.animations.size(), 1U);
  EXPECT_EQ(item.animations.front().name, arv::client::BlockContainerCloseAnimation);
  EXPECT_FLOAT_EQ(item.animations.front().time_seconds, 0.5F);

  arv::client::update_block_container_animation(
      state,
      std::nullopt,
      2.0 + arv::client::BlockContainerAnimationLengthSeconds + 0.01);
  EXPECT_FALSE(state.active_source.has_value());
  EXPECT_TRUE(state.active_clip.empty());
}

TEST(BlockContainerAnimationTests, PlaybackIgnoresOtherSourcesAndClampsNegativeTime) {
  arv::client::BlockContainerAnimationState state;
  const arv::protocol::ContainerBlockSource source = make_source();
  const arv::protocol::ContainerBlockSource other = make_source(9, 8, 7);

  arv::client::update_block_container_animation(state, source, 10.0);

  arv::render::EntityDrawItem ignored;
  arv::client::append_block_container_animation_playback(ignored, state, other, 10.25);
  EXPECT_TRUE(ignored.animations.empty());

  arv::render::EntityDrawItem clamped;
  arv::client::append_block_container_animation_playback(clamped, state, source, 9.5);
  ASSERT_EQ(clamped.animations.size(), 1U);
  EXPECT_FLOAT_EQ(clamped.animations.front().time_seconds, 0.0F);
}
