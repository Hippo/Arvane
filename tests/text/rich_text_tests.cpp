#include "arv/io/data_document.hpp"
#include "arv/text/chat_message.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/text/variables.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(RichTextTests, FlattensInheritedStylesAndPlainText) {
  arv::text::TextStylePatch root_style;
  root_style.bold = true;

  arv::text::TextStylePatch child_style;
  child_style.color = arv::text::RgbColor{0x12U, 0x34U, 0x56U};
  child_style.bold = false;

  arv::text::RichText root = arv::text::plain_rich_text("root ", root_style);
  root.children.push_back(arv::text::plain_rich_text("child", child_style));

  EXPECT_EQ(arv::text::to_plain_text(root), "root child");

  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(root);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(runs[0].style.bold);
  EXPECT_FALSE(runs[1].style.bold);
  EXPECT_EQ(runs[1].style.color, (arv::text::RgbColor{0x12U, 0x34U, 0x56U}));
}

TEST(RichTextTests, ValidatesDepthNodeAndTextLimits) {
  arv::text::RichTextLimits limits;
  limits.max_depth = 2U;
  limits.max_nodes = 3U;
  limits.max_text_bytes = 8U;

  arv::text::RichText deep = arv::text::plain_rich_text("a");
  deep.children.push_back(arv::text::plain_rich_text("b"));
  deep.children.front().children.push_back(arv::text::plain_rich_text("c"));
  EXPECT_FALSE(arv::text::validate_rich_text(deep, limits).valid);

  arv::text::RichText many = arv::text::plain_rich_text("a");
  many.children.push_back(arv::text::plain_rich_text("b"));
  many.children.push_back(arv::text::plain_rich_text("c"));
  many.children.push_back(arv::text::plain_rich_text("d"));
  EXPECT_FALSE(arv::text::validate_rich_text(many, limits).valid);

  arv::text::RichText large = arv::text::plain_rich_text("too long for the limit");
  EXPECT_FALSE(arv::text::validate_rich_text(large, limits).valid);
}

TEST(ChatMessageTemplateTests, ParsesColorsScopesClicksHoversAndVariables) {
  arv::text::TextVariableStore variables;
  variables.set_global("server_name", "Arvane");
  variables.set_player(arv::PlayerID{7}, "player_name", "Hippo");

  const arv::text::TextVariableResolver resolver{&variables, arv::PlayerID{7}};

  const auto hex =
      arv::text::ChatMessageTemplate::parse("<#ff8800>Hello <#aaaaaa>World");
  EXPECT_TRUE(hex.parsed());
  EXPECT_EQ(hex.to_string(), "Hello World");
  const auto hex_runs = arv::text::flatten_rich_text(hex.to_rich_text());
  ASSERT_EQ(hex_runs.size(), 2U);
  EXPECT_EQ(hex_runs[0].style.color, (arv::text::RgbColor{0xFFU, 0x88U, 0x00U}));
  EXPECT_EQ(hex_runs[1].style.color, (arv::text::RgbColor{0xAAU, 0xAAU, 0xAAU}));

  const auto named =
      arv::text::ChatMessageTemplate::parse("<red>Greetings <blue>{player_name}");
  EXPECT_EQ(named.to_string(resolver), "Greetings Hippo");
  const auto named_runs = arv::text::flatten_rich_text(named.to_rich_text(resolver));
  ASSERT_EQ(named_runs.size(), 2U);
  EXPECT_EQ(named_runs[0].style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
  EXPECT_EQ(named_runs[1].style.color, (arv::text::RgbColor{0x55U, 0x55U, 0xFFU}));

  const auto closed = arv::text::ChatMessageTemplate::parse(
      "<red>This is colored text!</> This is not.");
  const auto closed_runs = arv::text::flatten_rich_text(closed.to_rich_text());
  ASSERT_EQ(closed_runs.size(), 2U);
  EXPECT_EQ(closed_runs[0].style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
  EXPECT_EQ(closed_runs[1].style.color, (arv::text::RgbColor{0xFFU, 0xFFU, 0xFFU}));

  const auto hover = arv::text::ChatMessageTemplate::parse(
      "<hover:show_text:Click to message {player_name}>Hover Me!</> plain");
  const auto hover_runs = arv::text::flatten_rich_text(hover.to_rich_text(resolver));
  ASSERT_EQ(hover_runs.size(), 2U);
  ASSERT_TRUE(hover_runs[0].hover_event.has_value());
  ASSERT_NE(hover_runs[0].hover_event->show_text, nullptr);
  EXPECT_EQ(
      arv::text::to_plain_text(*hover_runs[0].hover_event->show_text),
      "Click to message Hippo");
  EXPECT_FALSE(hover_runs[1].hover_event.has_value());

  const auto click = arv::text::ChatMessageTemplate::parse(
      "<click:suggest_command:msg {player_name}>Click {player_name}!");
  const auto click_runs = arv::text::flatten_rich_text(click.to_rich_text(resolver));
  ASSERT_EQ(click_runs.size(), 1U);
  EXPECT_EQ(click.to_string(resolver), "Click Hippo!");
  ASSERT_TRUE(click_runs[0].click_event.has_value());
  EXPECT_EQ(click_runs[0].click_event->action, arv::text::ClickAction::SuggestCommand);
  EXPECT_EQ(click_runs[0].click_event->value, "msg Hippo");
}

TEST(ChatMessageTemplateTests, ColorTagsReplacePreviousColorScope) {
  const auto message = arv::text::ChatMessageTemplate::parse(
      "<red>Mason<gray>></> My message");
  ASSERT_TRUE(message.parsed());

  const auto runs = arv::text::flatten_rich_text(message.to_rich_text());
  ASSERT_EQ(runs.size(), 3U);
  EXPECT_EQ(runs[0].text, "Mason");
  EXPECT_EQ(runs[0].style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
  EXPECT_EQ(runs[1].text, ">");
  EXPECT_EQ(runs[1].style.color, (arv::text::RgbColor{0xAAU, 0xAAU, 0xAAU}));
  EXPECT_EQ(runs[2].text, " My message");
  EXPECT_EQ(runs[2].style.color, (arv::text::RgbColor{0xFFU, 0xFFU, 0xFFU}));
}

TEST(ChatMessageTemplateTests, ResolvedVariablesCanProvideMarkupBeforeParsing) {
  arv::text::TextVariableStore variables;
  variables.set_global("arv_mason_prefix", "<red>Mason<gray>></>");

  const auto message = arv::text::ChatMessageTemplate::parse(
      "{arv_mason_prefix} My message");
  const auto runs = arv::text::flatten_rich_text(message.to_rich_text(
      {&variables, std::nullopt, arv::text::TextVariableResolution::ResolvedOnly}));

  ASSERT_EQ(runs.size(), 3U);
  EXPECT_EQ(message.to_string({&variables}), "Mason> My message");
  EXPECT_EQ(runs[0].style.color, (arv::text::RgbColor{0xFFU, 0x55U, 0x55U}));
  EXPECT_EQ(runs[1].style.color, (arv::text::RgbColor{0xAAU, 0xAAU, 0xAAU}));
  EXPECT_EQ(runs[2].style.color, (arv::text::RgbColor{0xFFU, 0xFFU, 0xFFU}));
}

TEST(ChatMessageTemplateTests, NamedColorAliasesAreAvailable) {
  EXPECT_EQ(arv::text::named_chat_color("orange"), (arv::text::RgbColor{0xFFU, 0x88U, 0x00U}));
  EXPECT_EQ(arv::text::named_chat_color("purple"), (arv::text::RgbColor{0xAAU, 0x00U, 0xAAU}));
  EXPECT_EQ(arv::text::named_chat_color("pink"), (arv::text::RgbColor{0xFFU, 0x55U, 0xFFU}));
  EXPECT_EQ(arv::text::named_chat_color("cyan"), (arv::text::RgbColor{0x00U, 0xFFU, 0xFFU}));
  EXPECT_EQ(arv::text::named_chat_color("teal"), (arv::text::RgbColor{0x00U, 0x80U, 0x80U}));
  EXPECT_EQ(arv::text::named_chat_color("lime"), (arv::text::RgbColor{0x55U, 0xFFU, 0x55U}));
  EXPECT_EQ(arv::text::named_chat_color("silver"), (arv::text::RgbColor{0xC0U, 0xC0U, 0xC0U}));
  EXPECT_EQ(arv::text::named_chat_color("navy"), (arv::text::RgbColor{0x00U, 0x00U, 0x80U}));
}

TEST(ChatMessageTemplateTests, FallsBackToRawStringForMalformedMarkup) {
  const std::vector<std::string> malformed{
      "<click:run_off_example:im not closing this",
      "<#ff8800 did not close the color",
      "<#ff88zz>bad hex",
      "<click:run_off_example:value>bad action",
      "</> unmatched close",
      "empty {} variable",
      "unfinished {player_name",
      "unfinished <red",
      std::string{"bad utf8 \xC0\xAF", 11U},
  };

  for (const std::string& input : malformed) {
    const auto message = arv::text::ChatMessageTemplate::parse(input);
    EXPECT_FALSE(message.parsed()) << input;
    EXPECT_EQ(message.to_string(), input);
    EXPECT_EQ(arv::text::to_plain_text(message.to_rich_text()), input);
  }
}

TEST(ChatMessageTemplateTests, BoundsAbusiveEventPayloadsAndDeepNesting) {
  const std::string long_click(
      arv::text::RichTextLimits{}.max_click_value_bytes + 1U,
      'x');
  const std::string long_hover(
      arv::text::RichTextLimits{}.max_hover_text_bytes + 1U,
      'x');
  const auto click = arv::text::ChatMessageTemplate::parse(
      "<click:suggest_command:" + long_click + ">Click");
  const auto hover = arv::text::ChatMessageTemplate::parse(
      "<hover:show_text:" + long_hover + ">Hover");

  std::string deep;
  for (arv::usize index = 0; index <= arv::text::RichTextLimits{}.max_depth; ++index) {
    deep += "<hover:show_text:tip>";
  }
  deep += "too deep";
  const auto nested = arv::text::ChatMessageTemplate::parse(deep);

  EXPECT_FALSE(click.parsed());
  EXPECT_FALSE(hover.parsed());
  EXPECT_FALSE(nested.parsed());
  EXPECT_EQ(click.to_string(), "<click:suggest_command:" + long_click + ">Click");
  EXPECT_EQ(hover.to_string(), "<hover:show_text:" + long_hover + ">Hover");
  EXPECT_EQ(nested.to_string(), deep);
}

TEST(ChatMessageTemplateTests, DataCodecReadsStringFieldsAndReportsTypeDiagnostics) {
  arv::io::DataDocument document{1, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "message", "<red>Hello {player_name}"));
  ASSERT_TRUE(arv::io::set_path(document, "number", arv::i64{4}));

  auto message = document.value_as<arv::text::ChatMessageTemplate>("message");
  ASSERT_TRUE(message) << message.error().describe();
  arv::text::TextVariableStore variables;
  variables.set_player(arv::PlayerID{2}, "player_name", "Alex");
  EXPECT_EQ(
      message.value().to_string({&variables, arv::PlayerID{2}}),
      "Hello Alex");

  auto missing = document.value_as<arv::text::ChatMessageTemplate>("missing");
  EXPECT_FALSE(missing);
  EXPECT_NE(missing.error().describe().find("root.missing"), std::string::npos);

  auto wrong_type = document.value_as<arv::text::ChatMessageTemplate>("number");
  EXPECT_FALSE(wrong_type);
  EXPECT_NE(wrong_type.error().describe().find("not a string"), std::string::npos);
}

TEST(TextVariableTests, ResolvesGlobalAndPlayerScopesAndLeavesMissingPlaceholders) {
  arv::text::TextVariableStore variables;
  variables.set_global("online_player_count", "3");
  variables.set_player(arv::PlayerID{7}, "player_name", "Hippo");

  EXPECT_EQ(
      arv::text::interpolate_text_variables(
          "{player_name} on {online_player_count}/{missing}",
          {&variables, arv::PlayerID{7}}),
      "Hippo on 3/{missing}");
  EXPECT_EQ(
      arv::text::interpolate_text_variables("{player_name}", {&variables, std::nullopt}),
      "{player_name}");
}

TEST(TextVariableTests, BindingControlsResolvedAndLiveInterpolation) {
  arv::text::TextVariableStore variables;
  variables.set_global(
      "prefix",
      "My Prefix",
      arv::text::TextVariableBinding::Resolved);
  variables.set_player(
      arv::PlayerID{7},
      "player_countdown",
      "1",
      arv::text::TextVariableBinding::Live);

  EXPECT_EQ(
      arv::text::interpolate_text_variables(
          "{prefix} countdown: {player_countdown}",
          {&variables,
           arv::PlayerID{7},
           arv::text::TextVariableResolution::ResolvedOnly}),
      "My Prefix countdown: {player_countdown}");
  EXPECT_EQ(
      arv::text::interpolate_text_variables(
          "{prefix} countdown: {player_countdown}",
          {&variables,
           arv::PlayerID{7},
           arv::text::TextVariableResolution::LiveOnly}),
      "{prefix} countdown: 1");
}
