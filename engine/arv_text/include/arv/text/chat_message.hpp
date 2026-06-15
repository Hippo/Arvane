#pragma once

#include "arv/io/data_document.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/text/variables.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arv::text {

struct ChatMarkupPermissions {
  bool color{true};
  bool click{true};
  bool hover{true};
};

class ChatMessageTemplate {
public:
  ChatMessageTemplate() = default;

  [[nodiscard]] static ChatMessageTemplate parse(std::string_view source);
  [[nodiscard]] static ChatMessageTemplate parse(
      std::string_view source,
      ChatMarkupPermissions permissions);
  [[nodiscard]] static ChatMessageTemplate raw(std::string source);

  [[nodiscard]] const std::string& source() const noexcept;
  [[nodiscard]] bool parsed() const noexcept;

  [[nodiscard]] RichText to_rich_text(
      const TextVariableResolver& resolver = {}) const;
  [[nodiscard]] std::string to_string(
      const TextVariableResolver& resolver = {}) const;

private:
  std::string source_{};
  RichText rich_text_{};
  bool parsed_{};
};

[[nodiscard]] std::optional<RgbColor> named_chat_color(std::string_view name);

} // namespace arv::text

namespace arv::io {

template<>
struct DataCodec<arv::text::ChatMessageTemplate> {
  [[nodiscard]] static Expected<arv::text::ChatMessageTemplate, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    auto text = document.string_value(path);
    if (!text) {
      return Expected<arv::text::ChatMessageTemplate, DataDiagnostic>::failure(
          std::move(text.error()));
    }
    return Expected<arv::text::ChatMessageTemplate, DataDiagnostic>::success(
        arv::text::ChatMessageTemplate::parse(text.value()));
  }
};

} // namespace arv::io
