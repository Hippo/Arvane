#include "arv/server/chat_service.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(ServerChatTests, ValidationRejectsEmptyLongNewlineAndInvalidUtf8) {
  arv::server::ChatValidationConfig config;
  config.max_message_bytes = 8U;
  config.max_message_codepoints = 8U;
  config.allow_newlines = false;

  EXPECT_FALSE(arv::server::validate_chat_message_text("   ", config));
  EXPECT_FALSE(arv::server::validate_chat_message_text("this is too long", config));
  EXPECT_FALSE(arv::server::validate_chat_message_text("hello\nthere", config));
  EXPECT_FALSE(arv::server::validate_chat_message_text(std::string{"\xC0\xAF", 2U}, config));
}

TEST(ServerChatTests, ValidationTrimsAndPreservesLiteralFormattingText) {
  const auto validated = arv::server::validate_chat_message_text(
      "  {\"text\":\"fake admin\"} &cRed  ");

  ASSERT_TRUE(validated);
  EXPECT_EQ(validated.value(), "{\"text\":\"fake admin\"} &cRed");
}

TEST(ServerChatTests, RateLimiterAllowsWindowThenRejectsSpam) {
  arv::server::ChatValidationConfig config;
  config.max_messages_per_window = 2U;
  config.rate_limit_window = 4.0F;
  arv::server::ChatRateLimitState state;

  EXPECT_TRUE(arv::server::consume_chat_rate_limit(state, config, 0.0F));
  EXPECT_TRUE(arv::server::consume_chat_rate_limit(state, config, 0.0F));
  EXPECT_FALSE(arv::server::consume_chat_rate_limit(state, config, 0.0F));
  EXPECT_TRUE(arv::server::consume_chat_rate_limit(state, config, 2.0F));
}

TEST(ServerChatTests, FormatterBuildsRichTextWithPlayerInteractions) {
  arv::text::ChatFormatter formatter;
  arv::text::ChatFormatContext context;
  context.sender_name = "Hippo";
  context.raw_message = "hello";

  const arv::text::RichText rich = formatter.format_player_chat(context);

  EXPECT_EQ(arv::text::to_plain_text(rich), "Hippo: hello");
  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(rich);
  ASSERT_GE(runs.size(), 3U);
  ASSERT_TRUE(runs.front().click_event.has_value());
  EXPECT_EQ(runs.front().click_event->action, arv::text::ClickAction::SuggestCommand);
  EXPECT_EQ(runs.front().click_event->value, "/msg Hippo ");
  ASSERT_TRUE(runs.front().hover_event.has_value());
  ASSERT_NE(runs.front().hover_event->show_text, nullptr);
}
