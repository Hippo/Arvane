#include "arv/client/client_runtime.hpp"

#include <gtest/gtest.h>

#include <memory>

TEST(ClientRuntimeTests, ChatDraftIgnoresOpeningKeyThenAcceptsTextEditingAndSubmit) {
  arv::client::ClientRuntime client{std::unique_ptr<arv::net::ITransport>{}};

  client.submit_input(arv::client::InputFrame{
      .open_chat_pressed = true,
      .text_input = "t"});
  EXPECT_TRUE(client.chat_open());
  EXPECT_TRUE(client.chat_draft().empty());

  client.submit_input(arv::client::InputFrame{.text_input = "hello"});
  EXPECT_EQ(client.chat_draft(), "hello");

  client.submit_input(arv::client::InputFrame{.text_backspace_pressed = true});
  EXPECT_EQ(client.chat_draft(), "hell");

  client.submit_input(arv::client::InputFrame{.text_submit_pressed = true});
  EXPECT_FALSE(client.chat_open());
  ASSERT_EQ(client.chat_messages().size(), 1U);
  EXPECT_EQ(client.chat_messages().front(), "hell");
}
