#include "arv/protocol/chat.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::protocol::RichText sample_rich_text() {
  arv::protocol::TextStylePatch root_style;
  root_style.bold = true;

  arv::protocol::TextStylePatch child_style;
  child_style.color = arv::protocol::RgbColor{0x12U, 0x34U, 0x56U};
  child_style.bold = false;

  arv::protocol::RichText child = arv::protocol::plain_rich_text("child", child_style);
  child.click_event = {arv::protocol::ClickAction::SuggestCommand, "/msg Hippo "};
  child.hover_event = arv::protocol::make_show_text_hover(
      arv::protocol::plain_rich_text("hover"));

  arv::protocol::RichText root = arv::protocol::plain_rich_text("root ", root_style);
  root.children.push_back(std::move(child));
  return root;
}

} // namespace

TEST(ChatProtocolTests, ServerboundChatMessageRoundTrips) {
  const arv::protocol::ServerboundChatMessage message{"hello", std::string{"arvane:global"}};

  const auto decoded = arv::protocol::deserialize_serverbound_chat_message(
      arv::protocol::serialize_serverbound_chat_message(message));

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().message, "hello");
  ASSERT_TRUE(decoded.value().channel.has_value());
  EXPECT_EQ(decoded.value().channel.value(), "arvane:global");
}

TEST(ChatProtocolTests, ClientboundChatMessageRoundTripsRichText) {
  const arv::protocol::ClientboundChatMessage message{
      42U,
      "arvane:global",
      arv::protocol::ChatMessageKind::Chat,
      sample_rich_text()};

  const auto decoded = arv::protocol::deserialize_clientbound_chat_message(
      arv::protocol::serialize_clientbound_chat_message(message));

  ASSERT_TRUE(decoded);
  EXPECT_EQ(decoded.value().message_id, 42U);
  EXPECT_EQ(decoded.value().channel, "arvane:global");
  EXPECT_EQ(decoded.value().kind, arv::protocol::ChatMessageKind::Chat);
  EXPECT_EQ(arv::protocol::to_plain_text(decoded.value().message), "root child");

  const std::vector<arv::protocol::RichTextRun> runs =
      arv::protocol::flatten_rich_text(decoded.value().message);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(runs[0].style.bold);
  EXPECT_FALSE(runs[1].style.bold);
  EXPECT_EQ(runs[1].style.color, (arv::protocol::RgbColor{0x12U, 0x34U, 0x56U}));
  ASSERT_TRUE(runs[1].click_event.has_value());
  EXPECT_EQ(runs[1].click_event->action, arv::protocol::ClickAction::SuggestCommand);
  ASSERT_TRUE(runs[1].hover_event.has_value());
  ASSERT_NE(runs[1].hover_event->show_text, nullptr);
  EXPECT_EQ(arv::protocol::to_plain_text(*runs[1].hover_event->show_text), "hover");
}

TEST(ChatProtocolTests, TranslatableRichTextFallsBackToKeyAndRoundTrips) {
  arv::protocol::RichText text =
      arv::protocol::translatable_rich_text("example_plugin_pack.text.hello");
  text.children.push_back(arv::protocol::plain_rich_text("!"));

  const auto decoded = arv::protocol::deserialize_rich_text(
      arv::protocol::serialize_rich_text(text));

  ASSERT_TRUE(decoded) << decoded.error().message;
  EXPECT_EQ(decoded.value().kind, arv::protocol::RichTextNodeKind::Translatable);
  EXPECT_EQ(decoded.value().translation_key, "example_plugin_pack.text.hello");
  EXPECT_EQ(
      arv::protocol::to_plain_text(decoded.value()),
      "example_plugin_pack.text.hello!");
  const std::vector<arv::protocol::RichTextRun> runs =
      arv::protocol::flatten_rich_text(decoded.value());
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_EQ(runs.front().text, "example_plugin_pack.text.hello");
}

TEST(ChatProtocolTests, RichTextLimitsRejectDepthNodeAndTextAbuse) {
  arv::protocol::RichTextLimits limits;
  limits.max_depth = 2U;
  limits.max_nodes = 3U;
  limits.max_text_bytes = 8U;

  arv::protocol::RichText deep = arv::protocol::plain_rich_text("a");
  deep.children.push_back(arv::protocol::plain_rich_text("b"));
  deep.children.front().children.push_back(arv::protocol::plain_rich_text("c"));
  EXPECT_FALSE(arv::protocol::validate_rich_text(deep, limits).valid);

  arv::protocol::RichText many = arv::protocol::plain_rich_text("a");
  many.children.push_back(arv::protocol::plain_rich_text("b"));
  many.children.push_back(arv::protocol::plain_rich_text("c"));
  many.children.push_back(arv::protocol::plain_rich_text("d"));
  EXPECT_FALSE(arv::protocol::validate_rich_text(many, limits).valid);

  arv::protocol::RichText large = arv::protocol::plain_rich_text("too long for the limit");
  EXPECT_FALSE(arv::protocol::validate_rich_text(large, limits).valid);
}

TEST(ChatProtocolTests, DeserializationRejectsInvalidEnumAndUtf8) {
  arv::protocol::MessageWriter bad_kind;
  bad_kind.write<arv::u64>(1U);
  bad_kind.write_string("arvane:global");
  bad_kind.write<arv::u8>(99U);
  const auto decoded_bad_kind =
      arv::protocol::deserialize_clientbound_chat_message(std::move(bad_kind).take_bytes());
  EXPECT_FALSE(decoded_bad_kind);

  arv::protocol::MessageWriter bad_utf8;
  bad_utf8.write_string(std::string{"\xC0\xAF", 2U});
  bad_utf8.write_bool(false);
  const auto decoded_bad_utf8 =
      arv::protocol::deserialize_serverbound_chat_message(std::move(bad_utf8).take_bytes());
  EXPECT_FALSE(decoded_bad_utf8);
}
