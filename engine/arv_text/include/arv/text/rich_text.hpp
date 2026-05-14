#pragma once

#include "arv/core/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {

inline constexpr usize MaxRichTextFontBytes = 128U;
inline constexpr usize MaxRichTextTranslationKeyBytes = 256U;

struct RgbColor {
  u8 r{};
  u8 g{};
  u8 b{};

  constexpr bool operator==(const RgbColor&) const = default;
};

struct TextStylePatch {
  std::optional<RgbColor> color{};
  std::optional<bool> bold{};
  std::optional<bool> italic{};
  std::optional<bool> underlined{};
  std::optional<bool> strikethrough{};
  std::optional<bool> obfuscated{};
  std::optional<std::string> font{};
};

struct ResolvedTextStyle {
  RgbColor color{0xFFU, 0xFFU, 0xFFU};
  bool bold{};
  bool italic{};
  bool underlined{};
  bool strikethrough{};
  bool obfuscated{};
  std::string font{};

  bool operator==(const ResolvedTextStyle&) const = default;
};

enum class ClickAction : u8 {
  OpenUrl,
  SuggestCommand,
  RunCommand,
  CopyToClipboard,
};

struct ClickEvent {
  ClickAction action{ClickAction::SuggestCommand};
  std::string value{};

  bool operator==(const ClickEvent&) const = default;
};

enum class HoverAction : u8 {
  ShowText,
  ShowItem,
  ShowEntity,
};

struct RichText;

struct HoverEvent {
  HoverAction action{HoverAction::ShowText};
  std::shared_ptr<RichText> show_text{};
  std::string fallback_text{};
};

enum class RichTextNodeKind : u8 {
  Literal,
  Translatable,
};

struct RichText {
  RichTextNodeKind kind{RichTextNodeKind::Literal};
  std::string text{};
  std::string translation_key{};
  TextStylePatch style{};
  std::vector<RichText> children{};
  std::optional<ClickEvent> click_event{};
  std::optional<HoverEvent> hover_event{};
};

struct RichTextRun {
  std::string text{};
  ResolvedTextStyle style{};
  std::optional<ClickEvent> click_event{};
  std::optional<HoverEvent> hover_event{};
};

struct RichTextLimits {
  usize max_serialized_size_bytes{16U * 1024U};
  usize max_text_bytes{4096U};
  usize max_nodes{256U};
  usize max_depth{16U};
  usize max_hover_text_bytes{4096U};
  usize max_click_value_bytes{1024U};
};

struct RichTextValidation {
  bool valid{};
  std::string message{};
};

class RichTextBuilder {
public:
  RichTextBuilder& append(std::string text, TextStylePatch style = {});
  RichTextBuilder& append_literal(std::string text, TextStylePatch style = {});
  RichTextBuilder& append_component(RichText component);
  RichTextBuilder& click_open_url(std::string value);
  RichTextBuilder& click_suggest_command(std::string value);
  RichTextBuilder& click_run_command(std::string value);
  RichTextBuilder& click_copy_to_clipboard(std::string value);
  RichTextBuilder& hover_text(std::string text);
  RichTextBuilder& hover_text(RichText text);
  [[nodiscard]] RichText build() const;

private:
  void attach_click(ClickAction action, std::string value);

  RichText root_{};
};

[[nodiscard]] bool valid_click_action(ClickAction action) noexcept;
[[nodiscard]] bool valid_hover_action(HoverAction action) noexcept;
[[nodiscard]] bool valid_rich_text_node_kind(RichTextNodeKind kind) noexcept;

[[nodiscard]] TextStylePatch style_patch_color(RgbColor color);
[[nodiscard]] TextStylePatch style_patch_color(u8 r, u8 g, u8 b);
[[nodiscard]] RichText plain_rich_text(std::string text, TextStylePatch style = {});
[[nodiscard]] RichText translatable_rich_text(
    std::string translation_key,
    TextStylePatch style = {});
[[nodiscard]] HoverEvent make_show_text_hover(RichText text);

[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;
[[nodiscard]] usize utf8_codepoint_count(std::string_view text) noexcept;
[[nodiscard]] std::string to_plain_text(const RichText& text);
[[nodiscard]] std::vector<RichTextRun> flatten_rich_text(
    const RichText& text,
    ResolvedTextStyle base_style = {});
[[nodiscard]] RichTextValidation validate_rich_text(
    const RichText& text,
    const RichTextLimits& limits = {});

} // namespace arv::text
