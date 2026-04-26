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
