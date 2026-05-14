#include "arv/text/language.hpp"

#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <utility>

namespace arv::text {
namespace {

[[nodiscard]] std::string language_file_name(std::string_view locale) {
  return std::string{locale} + ".yaml";
}

[[nodiscard]] std::optional<LanguageError> flatten_language_node(
    const io::DataValue& node,
    std::string_view prefix,
    LanguageCatalog& catalog) {
  if (node.kind() == io::DataKind::String || node.kind() == io::DataKind::Bool ||
      node.kind() == io::DataKind::I64 || node.kind() == io::DataKind::U64 ||
      node.kind() == io::DataKind::F64) {
    if (prefix.empty()) {
      return LanguageError{"language scalar requires a key"};
    }
    switch (node.kind()) {
      case io::DataKind::String:
        catalog.set(std::string{prefix}, std::string{node.as_string_or({})});
        break;
      case io::DataKind::Bool:
        catalog.set(std::string{prefix}, node.as_bool_or(false) ? "true" : "false");
        break;
      case io::DataKind::I64:
        catalog.set(std::string{prefix}, std::to_string(node.as_i64_or(0)));
        break;
      case io::DataKind::U64:
        catalog.set(std::string{prefix}, std::to_string(node.as_u64_or(0)));
        break;
      case io::DataKind::F64:
        catalog.set(std::string{prefix}, std::to_string(node.as_f64_or(0.0)));
        break;
      default:
        break;
    }
    return std::nullopt;
  }

  const io::DataValue::Object* object = node.object_if();
  if (object == nullptr) {
    return LanguageError{
        prefix.empty()
            ? "language root must be a map"
            : "language key '" + std::string{prefix} + "' must be a string or map"};
  }

  for (const auto& [part, child] : *object) {
    if (part.empty()) {
      return LanguageError{"language keys must be non-empty scalars"};
    }
    const std::string key =
        prefix.empty() ? part : std::string{prefix} + "." + part;
    if (std::optional<LanguageError> error =
            flatten_language_node(child, key, catalog);
        error.has_value()) {
      return error;
    }
  }
  return std::nullopt;
}

void replace_all(std::string& text, std::string_view token, std::string_view value) {
  if (token.empty()) {
    return;
  }
  usize pos = 0;
  while ((pos = text.find(token, pos)) != std::string::npos) {
    text.replace(pos, token.size(), value);
    pos += value.size();
  }
}

[[nodiscard]] bool valid_locale(std::string_view locale) noexcept {
  if (locale.empty() || locale.find('/') != std::string_view::npos ||
      locale.find('\\') != std::string_view::npos ||
      locale.find("..") != std::string_view::npos) {
    return false;
  }
  return true;
}

} // namespace

void LanguageCatalog::set_locale(std::string locale) {
  locale_ = std::move(locale);
}

void LanguageCatalog::set(std::string key, std::string value) {
  entries_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<std::string_view> LanguageCatalog::find(std::string_view key) const noexcept {
  const auto found = entries_.find(std::string{key});
  if (found == entries_.end()) {
    return std::nullopt;
  }
  return std::string_view{found->second};
}

std::string LanguageCatalog::text(std::string_view key) const {
  const auto found = find(key);
  return found.has_value() ? std::string{found.value()} : std::string{key};
}

std::string LanguageCatalog::text(std::string_view key, std::string_view fallback) const {
  const auto found = find(key);
  return found.has_value() ? std::string{found.value()} : std::string{fallback};
}

std::string LanguageCatalog::format(
    std::string_view key,
    std::initializer_list<FormatArg> args) const {
  std::string result = text(key);
  for (const FormatArg& arg : args) {
    replace_all(result, "{" + arg.name + "}", arg.value);
  }
  return result;
}

std::string LanguageCatalog::format(
    std::string_view key,
    std::initializer_list<FormatArg> args,
    std::string_view fallback) const {
  std::string result = text(key, fallback);
  for (const FormatArg& arg : args) {
    replace_all(result, "{" + arg.name + "}", arg.value);
  }
  return result;
}

void LanguageCatalog::merge_from(LanguageCatalog other) {
  for (auto& [key, value] : other.entries_) {
    entries_.insert_or_assign(std::move(key), std::move(value));
  }
}

LanguageResult parse_language_yaml(std::string_view text, std::string locale) {
  auto decoded = io::decode_yaml_value(text);
  if (!decoded) {
    return LanguageResult::failure(
        {"failed to parse language YAML: " + decoded.error().describe()});
  }
  if (decoded.value().root.kind() != io::DataKind::Object) {
    return LanguageResult::failure({"language YAML root must be a map"});
  }
  LanguageCatalog catalog;
  catalog.set_locale(std::move(locale));
  if (std::optional<LanguageError> error =
          flatten_language_node(decoded.value().root, {}, catalog);
      error.has_value()) {
    return LanguageResult::failure(error.value());
  }
  return LanguageResult::success(std::move(catalog));
}

LanguageResult load_language_file(const std::filesystem::path& path, std::string locale) {
  auto text = io::read_text_file_contents(path);
  if (!text) {
    return LanguageResult::failure(
        {"language file could not be opened: " + path.string()});
  }
  return parse_language_yaml(text.value(), std::move(locale));
}

LanguageResult load_language_files(
    std::span<const std::filesystem::path> paths,
    std::string locale) {
  if (paths.empty()) {
    return LanguageResult::failure({"no language files were found for locale: " + locale});
  }

  LanguageCatalog catalog;
  catalog.set_locale(locale);
  for (const std::filesystem::path& path : paths) {
    auto loaded = load_language_file(path, locale);
    if (!loaded) {
      return loaded;
    }
    catalog.merge_from(std::move(loaded.value()));
  }
  catalog.set_locale(std::move(locale));
  return LanguageResult::success(std::move(catalog));
}

LanguageResult load_language_directory(
    const std::filesystem::path& lang_root,
    std::string locale,
    std::string fallback_locale) {
  if (!valid_locale(locale)) {
    return LanguageResult::failure({"invalid language locale: " + locale});
  }
  if (!valid_locale(fallback_locale)) {
    return LanguageResult::failure({"invalid fallback language locale: " + fallback_locale});
  }

  auto fallback = load_language_file(lang_root / language_file_name(fallback_locale), fallback_locale);
  if (!fallback) {
    return fallback;
  }

  LanguageCatalog catalog = std::move(fallback.value());
  if (locale != fallback_locale) {
    auto selected = load_language_file(lang_root / language_file_name(locale), locale);
    if (!selected) {
      return selected;
    }
    catalog.merge_from(std::move(selected.value()));
  }
  catalog.set_locale(std::move(locale));
  return LanguageResult::success(std::move(catalog));
}

std::string key_for_namespaced_id(
    std::string_view domain,
    std::string_view namespaced_id,
    std::string_view suffix) {
  std::string key;
  key.reserve(domain.size() + namespaced_id.size() + suffix.size() + 3U);
  key.append(domain);
  key.push_back('.');
  for (const char ch : namespaced_id) {
    key.push_back((ch == ':' || ch == '/') ? '.' : ch);
  }
  if (!suffix.empty()) {
    key.push_back('.');
    key.append(suffix);
  }
  return key;
}

std::string text_or_fallback(
    const LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

std::string format_or_fallback(
    const LanguageCatalog* language,
    std::string_view key,
    std::initializer_list<FormatArg> args,
    std::string_view fallback) {
  return language == nullptr ? LanguageCatalog{}.format(key, args, fallback)
                             : language->format(key, args, fallback);
}

std::string block_display_name_key(std::string_view block_name) {
  return key_for_namespaced_id("block", block_name, DisplayNameSuffix);
}

std::string item_display_name_key(std::string_view item_name) {
  return key_for_namespaced_id("item", item_name, DisplayNameSuffix);
}

std::string entity_display_name_key(std::string_view entity_name) {
  return key_for_namespaced_id("entity", entity_name, DisplayNameSuffix);
}

} // namespace arv::text
