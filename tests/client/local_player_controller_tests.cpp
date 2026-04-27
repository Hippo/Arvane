#include "arv/client/local_player_controller.hpp"

#include <gtest/gtest.h>

TEST(LocalPlayerControllerTests, HotbarSelectionAndScrollWrapProduceStableCommands) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{.hotbar_select = 7});
  const arv::player::PlayerCommand selected = controller.consume_command(10);
  EXPECT_EQ(selected.client_tick, 10U);
  EXPECT_EQ(selected.selected_hotbar_slot, 7U);

  controller.submit_input(arv::client::InputFrame{.hotbar_scroll_steps = 3});
  const arv::player::PlayerCommand wrapped = controller.consume_command(11);
  EXPECT_EQ(wrapped.client_tick, 11U);
  EXPECT_EQ(wrapped.selected_hotbar_slot, 1U);
}

TEST(LocalPlayerControllerTests, StaleCorrectionDoesNotOverrideUnconfirmedHotbarSelection) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{.hotbar_select = 5});

  arv::protocol::PlayerCorrection stale;
  stale.server_tick = 10;
  stale.selected_hotbar_slot = 0;
  controller.apply_authoritative_state(stale);
  EXPECT_EQ(controller.selected_hotbar_slot(), 5U);

  arv::protocol::PlayerCorrection confirmed;
  confirmed.server_tick = 11;
  confirmed.selected_hotbar_slot = 5;
  controller.apply_authoritative_state(confirmed);
  EXPECT_EQ(controller.selected_hotbar_slot(), 5U);

  arv::protocol::PlayerCorrection late_stale;
  late_stale.server_tick = 10;
  late_stale.selected_hotbar_slot = 0;
  controller.apply_authoritative_state(late_stale);
  EXPECT_EQ(controller.selected_hotbar_slot(), 5U);
}

TEST(LocalPlayerControllerTests, SprintOnlyAppliesWhileMovingForwardAndNotSneaking) {
  arv::client::LocalPlayerController controller;

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .move_right = true,
      .sprint_held = true});
  const arv::player::PlayerCommand sprinting = controller.consume_command(12);
  EXPECT_TRUE(sprinting.sprint_held);

  controller.submit_input(arv::client::InputFrame{
      .move_right = true,
      .sprint_held = true});
  const arv::player::PlayerCommand strafing = controller.consume_command(13);
  EXPECT_FALSE(strafing.sprint_held);

  controller.submit_input(arv::client::InputFrame{
      .move_forward = true,
      .sprint_held = true,
      .sneak_held = true});
  const arv::player::PlayerCommand sneaking = controller.consume_command(14);
  EXPECT_TRUE(sneaking.sneak_held);
  EXPECT_FALSE(sneaking.sprint_held);
}
