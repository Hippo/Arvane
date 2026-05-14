#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {

inline constexpr std::string_view DefaultLocale = "en_US";
inline constexpr std::string_view DisplayNameSuffix = "display_name";

struct LanguageError {
  std::string message{};
};

struct FormatArg {
  std::string name{};
  std::string value{};
};

class LanguageCatalog {
public:
  void set_locale(std::string locale);
  [[nodiscard]] const std::string& locale() const noexcept {
    return locale_;
  }

  void set(std::string key, std::string value);
  [[nodiscard]] std::optional<std::string_view> find(std::string_view key) const noexcept;
  [[nodiscard]] std::string text(std::string_view key) const;
  [[nodiscard]] std::string text(std::string_view key, std::string_view fallback) const;
  [[nodiscard]] std::string format(
      std::string_view key,
      std::initializer_list<FormatArg> args) const;
  [[nodiscard]] std::string format(
      std::string_view key,
      std::initializer_list<FormatArg> args,
      std::string_view fallback) const;

  [[nodiscard]] usize entry_count() const noexcept {
    return entries_.size();
  }

  void merge_from(LanguageCatalog other);

private:
  std::string locale_{std::string{DefaultLocale}};
  HashMap<std::string, std::string> entries_{};
};

using LanguageResult = Expected<LanguageCatalog, LanguageError>;

[[nodiscard]] LanguageResult parse_language_yaml(std::string_view text, std::string locale);
[[nodiscard]] LanguageResult load_language_file(
    const std::filesystem::path& path,
    std::string locale);
[[nodiscard]] LanguageResult load_language_files(
    std::span<const std::filesystem::path> paths,
    std::string locale);
[[nodiscard]] LanguageResult load_language_directory(
    const std::filesystem::path& lang_root,
    std::string locale = std::string{DefaultLocale},
    std::string fallback_locale = std::string{DefaultLocale});

[[nodiscard]] std::string key_for_namespaced_id(
    std::string_view domain,
    std::string_view namespaced_id,
    std::string_view suffix = DisplayNameSuffix);
[[nodiscard]] std::string block_display_name_key(std::string_view block_name);
[[nodiscard]] std::string item_display_name_key(std::string_view item_name);
[[nodiscard]] std::string entity_display_name_key(std::string_view entity_name);

} // namespace arv::text
