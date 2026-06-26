#include "arv/crafting/smelting.hpp"

#include "arv/asset/pack.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace arv::crafting {
namespace {

template<typename T>
using SmeltingResult = Expected<T, SmeltingRegistryError>;

[[nodiscard]] SmeltingRegistryError error(std::string message) {
  return SmeltingRegistryError{std::move(message)};
}

[[nodiscard]] std::string context_message(
    std::string_view source_name,
    std::string_view message) {
  return std::string{source_name} + ": " + std::string{message};
}

[[nodiscard]] std::string entry_context(
    std::string_view source_name,
    usize index) {
  return std::string{source_name} + ".smelting[" + std::to_string(index) + "]";
}

[[nodiscard]] SmeltingResult<std::string> read_required_string(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return SmeltingResult<std::string>::failure(
        error(std::string{context} + " requires string field " + std::string{key}));
  }
  std::string text{value->as_string_or({})};
  if (text.empty()) {
    return SmeltingResult<std::string>::failure(
        error(std::string{context} + " field " + std::string{key} + " must not be empty"));
  }
  const auto parsed = asset::ResourceID::parse(text);
  if (!parsed) {
    return SmeltingResult<std::string>::failure(
        error(
            std::string{context} + " field " + std::string{key} +
            " is invalid: " + parsed.error()));
  }
  return SmeltingResult<std::string>::success(std::move(text));
}

[[nodiscard]] SmeltingResult<u16> read_optional_positive_u16(
    const io::DataValue& object,
    std::string_view key,
    u16 fallback,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  if (value == nullptr) {
    return SmeltingResult<u16>::success(fallback);
  }
  if (!io::is_integer_value(*value)) {
    return SmeltingResult<u16>::failure(
        error(std::string{context} + " field " + std::string{key} + " must be an integer"));
  }
  const u64 raw = value->as_u64_or(0U);
  if (raw == 0U || raw > std::numeric_limits<u16>::max()) {
    return SmeltingResult<u16>::failure(
        error(
            std::string{context} + " field " + std::string{key} +
            " must be a positive item count"));
  }
  return SmeltingResult<u16>::success(static_cast<u16>(raw));
}

[[nodiscard]] SmeltingResult<Seconds> read_optional_positive_seconds(
    const io::DataValue& object,
    std::string_view key,
    Seconds fallback,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  if (value == nullptr) {
    return SmeltingResult<Seconds>::success(fallback);
  }
  if (!io::is_number_value(*value)) {
    return SmeltingResult<Seconds>::failure(
        error(std::string{context} + " field " + std::string{key} + " must be a number"));
  }
  const f64 raw = value->as_f64_or(0.0);
  if (!std::isfinite(raw) || raw <= 0.0 ||
      raw > static_cast<f64>(std::numeric_limits<Seconds>::max())) {
    return SmeltingResult<Seconds>::failure(
        error(
            std::string{context} + " field " + std::string{key} +
            " must be positive seconds"));
  }
  return SmeltingResult<Seconds>::success(static_cast<Seconds>(raw));
}

[[nodiscard]] SmeltingResult<player::ItemID> resolve_item(
    const player::ItemRegistry& registry,
    std::string_view item_name,
    std::string_view field_name,
    std::string_view context) {
  const std::optional<player::ItemID> item = registry.find_item(item_name);
  if (!item.has_value() || registry.is_air(item.value()) ||
      item_name == player::MissingItemName) {
    return SmeltingResult<player::ItemID>::failure(
        error(
            std::string{context} + " field " + std::string{field_name} +
            " references an unknown item: " + std::string{item_name}));
  }
  if (registry.item(item.value()) == nullptr) {
    return SmeltingResult<player::ItemID>::failure(
        error(
            std::string{context} + " field " + std::string{field_name} +
            " resolved to an unavailable item: " + std::string{item_name}));
  }
  return SmeltingResult<player::ItemID>::success(item.value());
}

[[nodiscard]] SmeltingResult<SmeltingRule> parse_rule_entry(
    const io::DataValue& entry,
    const player::ItemRegistry& item_registry,
    std::string_view context) {
  if (entry.kind() != io::DataKind::Object) {
    return SmeltingResult<SmeltingRule>::failure(
        error(std::string{context} + " must be an object"));
  }

  auto input_name = read_required_string(entry, "input", context);
  if (!input_name) {
    return SmeltingResult<SmeltingRule>::failure(input_name.error());
  }
  auto output_name = read_required_string(entry, "output", context);
  if (!output_name) {
    return SmeltingResult<SmeltingRule>::failure(output_name.error());
  }
  auto input = resolve_item(item_registry, input_name.value(), "input", context);
  if (!input) {
    return SmeltingResult<SmeltingRule>::failure(input.error());
  }
  auto output = resolve_item(item_registry, output_name.value(), "output", context);
  if (!output) {
    return SmeltingResult<SmeltingRule>::failure(output.error());
  }
  auto input_count = read_optional_positive_u16(entry, "input_count", 1U, context);
  if (!input_count) {
    return SmeltingResult<SmeltingRule>::failure(input_count.error());
  }
  auto output_count = read_optional_positive_u16(entry, "output_count", 1U, context);
  if (!output_count) {
    return SmeltingResult<SmeltingRule>::failure(output_count.error());
  }
  auto duration_secs = read_optional_positive_seconds(entry, "duration_secs", 10.0F, context);
  if (!duration_secs) {
    return SmeltingResult<SmeltingRule>::failure(duration_secs.error());
  }

  const player::ItemRecord* output_record = item_registry.item(output.value());
  if (output_record == nullptr || output_count.value() > output_record->max_stack_size) {
    return SmeltingResult<SmeltingRule>::failure(
        error(std::string{context} + " output_count exceeds the output item stack limit"));
  }

  SmeltingRule rule;
  rule.input_name = std::move(input_name.value());
  rule.input = input.value();
  rule.input_count = input_count.value();
  rule.output_name = std::move(output_name.value());
  rule.output = output.value();
  rule.output_count = output_count.value();
  rule.duration_secs = duration_secs.value();
  return SmeltingResult<SmeltingRule>::success(std::move(rule));
}

[[nodiscard]] const io::DataValue* smelting_entries_root(const io::DataValue& root) {
  if (root.kind() != io::DataKind::Object) {
    return &root;
  }
  const io::DataValue* entries = root.get("smelting");
  if (entries != nullptr) {
    return entries;
  }
  if (root.get("input") != nullptr || root.get("output") != nullptr) {
    return &root;
  }
  return nullptr;
}

[[nodiscard]] SmeltingRegistryError pack_file_error(
    const asset::PackDataFile& file,
    std::string message) {
  std::ostringstream out;
  out << "smelting data";
  if (!file.pack_id.empty()) {
    out << " pack=" << file.pack_id;
  }
  if (!file.pack_version.empty()) {
    out << " version=" << file.pack_version;
  }
  if (!file.relative_path.empty()) {
    out << " file=" << file.relative_path.generic_string();
  }
  out << ": " << message;
  return error(out.str());
}

[[nodiscard]] SmeltingResult<std::vector<SmeltingRule>> parse_smelting_file(
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry) {
  auto text = io::read_text_file_contents(file.path);
  if (!text) {
    return SmeltingResult<std::vector<SmeltingRule>>::failure(
        pack_file_error(file, "failed to read file: " + text.error().message));
  }
  auto parsed = parse_smelting_rules_json(
      text.value(),
      item_registry,
      file.relative_path.generic_string());
  if (!parsed) {
    return SmeltingResult<std::vector<SmeltingRule>>::failure(
        pack_file_error(file, parsed.error().message));
  }
  return parsed;
}

} // namespace

SmeltingRuleRegistry::Result SmeltingRuleRegistry::build(std::vector<SmeltingRule> rules) {
  SmeltingRuleRegistry registry;
  registry.rules_.reserve(rules.size());
  for (SmeltingRule& rule : rules) {
    if (rule.input_name.empty() || rule.output_name.empty()) {
      return Result::failure(error("smelting rule item names must not be empty"));
    }
    if (rule.input_count == 0U || rule.output_count == 0U ||
        !std::isfinite(rule.duration_secs) || rule.duration_secs <= 0.0F) {
      return Result::failure(error("smelting rule has invalid counts or duration"));
    }
    if (registry.rule_by_input_.contains(rule.input)) {
      return Result::failure(
          error("duplicate smelting rule input item: " + rule.input_name));
    }
    registry.rule_by_input_.emplace(rule.input, registry.rules_.size());
    registry.rules_.push_back(std::move(rule));
  }
  return Result::success(std::move(registry));
}

const SmeltingRule* SmeltingRuleRegistry::find_by_input(player::ItemID input) const noexcept {
  const auto found = rule_by_input_.find(input);
  return found == rule_by_input_.end() ? nullptr : &rules_[found->second];
}

std::span<const SmeltingRule> SmeltingRuleRegistry::rules() const noexcept {
  return rules_;
}

Expected<std::vector<SmeltingRule>, SmeltingRegistryError> parse_smelting_rules_json(
    std::string_view text,
    const player::ItemRegistry& item_registry,
    std::string_view source_name) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::failure(
        error(context_message(
            source_name,
            std::string{"failed to parse JSON: "} + decoded.error().describe())));
  }

  const io::DataValue* entries = smelting_entries_root(decoded.value().root);
  if (entries == nullptr) {
    return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::failure(
        error(context_message(source_name, "requires smelting array")));
  }

  std::vector<SmeltingRule> rules;
  if (const io::DataValue::Array* array = entries->array_if(); array != nullptr) {
    rules.reserve(array->size());
    for (usize index = 0; index < array->size(); ++index) {
      auto rule = parse_rule_entry(
          (*array)[index],
          item_registry,
          entry_context(source_name, index));
      if (!rule) {
        return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::failure(rule.error());
      }
      rules.push_back(std::move(rule.value()));
    }
  } else {
    auto rule = parse_rule_entry(*entries, item_registry, std::string{source_name});
    if (!rule) {
      return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::failure(rule.error());
    }
    rules.push_back(std::move(rule.value()));
  }

  auto registry = SmeltingRuleRegistry::build(rules);
  if (!registry) {
    return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::failure(registry.error());
  }
  return Expected<std::vector<SmeltingRule>, SmeltingRegistryError>::success(std::move(rules));
}

SmeltingRuleRegistry::Result load_smelting_rule_registry_from_packs(
    const asset::PackManager& packs,
    const player::ItemRegistry& item_registry) {
  std::vector<SmeltingRule> rules;
  for (const asset::PackDataFile& file : packs.data_files("smelting", ".json")) {
    auto file_rules = parse_smelting_file(file, item_registry);
    if (!file_rules) {
      return SmeltingRuleRegistry::Result::failure(file_rules.error());
    }
    rules.insert(
        rules.end(),
        std::make_move_iterator(file_rules.value().begin()),
        std::make_move_iterator(file_rules.value().end()));
  }
  return SmeltingRuleRegistry::build(std::move(rules));
}

} // namespace arv::crafting
