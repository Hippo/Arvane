#include "arv/blueprint/blueprint.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>

namespace arv::blueprint {
namespace {

[[nodiscard]] bool ascii_alnum(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

[[nodiscard]] bool safe_name_char(char c) noexcept {
  return ascii_alnum(c) || c == '_' || c == '-' || c == '.';
}

[[nodiscard]] bool valid_block_state_id(std::string_view id) noexcept {
  const std::size_t colon = id.find(':');
  if (colon == std::string_view::npos || colon == 0U || colon + 1U >= id.size()) {
    return false;
  }
  for (const char c : id) {
    const bool ok = ascii_alnum(c) || c == ':' || c == '/' || c == '_' ||
                    c == '-' || c == '.' || c == '[' || c == ']' ||
                    c == '=' || c == ',';
    if (!ok) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string escape_value(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '\\' || c == '\n' || c == '\r') {
      out.push_back('\\');
      if (c == '\n') {
        out.push_back('n');
      } else if (c == '\r') {
        out.push_back('r');
      } else {
        out.push_back('\\');
      }
      continue;
    }
    out.push_back(c);
  }
  return out;
}

[[nodiscard]] std::string unescape_value(std::string_view text, bool& ok) {
  std::string out;
  out.reserve(text.size());
  ok = true;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (i + 1U >= text.size()) {
      ok = false;
      return {};
    }
    const char next = text[++i];
    if (next == 'n') {
      out.push_back('\n');
    } else if (next == 'r') {
      out.push_back('\r');
    } else if (next == '\\') {
      out.push_back('\\');
    } else {
      ok = false;
      return {};
    }
  }
  return out;
}

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& out) noexcept {
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc{} && ptr == end;
}

[[nodiscard]] bool parse_u32(std::string_view text, std::uint32_t& out) noexcept {
  std::uint64_t value{};
  if (!parse_u64(text, value) || value > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  out = static_cast<std::uint32_t>(value);
  return true;
}

[[nodiscard]] std::vector<std::string_view> split_words(std::string_view text) {
  std::vector<std::string_view> words;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() && text[cursor] == ' ') {
      ++cursor;
    }
    const std::size_t start = cursor;
    while (cursor < text.size() && text[cursor] != ' ') {
      ++cursor;
    }
    if (start < cursor) {
      words.push_back(text.substr(start, cursor - start));
    }
  }
  return words;
}

[[nodiscard]] bool parse_pos(std::string_view text, BlockPos& out) {
  const std::vector<std::string_view> words = split_words(text);
  if (words.size() != 3U) {
    return false;
  }
  std::int64_t values[3]{};
  for (std::size_t i = 0; i < 3U; ++i) {
    const char* begin = words[i].data();
    const char* end = words[i].data() + words[i].size();
    const auto [ptr, ec] = std::from_chars(begin, end, values[i]);
    if (ec != std::errc{} || ptr != end ||
        values[i] < std::numeric_limits<std::int32_t>::min() ||
        values[i] > std::numeric_limits<std::int32_t>::max()) {
      return false;
    }
  }
  out = {{
      static_cast<std::int32_t>(values[0]),
      static_cast<std::int32_t>(values[1]),
      static_cast<std::int32_t>(values[2]),
  }};
  return true;
}

[[nodiscard]] std::map<std::string, std::vector<std::string>> parse_lines(std::string_view text) {
  std::map<std::string, std::vector<std::string>> values;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    std::string_view line = end == std::string_view::npos
                                ? text.substr(start)
                                : text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (!line.empty() && line.front() != '#') {
      const std::size_t colon = line.find(':');
      if (colon != std::string_view::npos) {
        std::string key{line.substr(0, colon)};
        std::string_view value = line.substr(colon + 1U);
        if (!value.empty() && value.front() == ' ') {
          value.remove_prefix(1);
        }
        values[std::move(key)].push_back(std::string{value});
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1U;
  }
  return values;
}

[[nodiscard]] std::string get_one(
    const std::map<std::string, std::vector<std::string>>& values,
    std::string_view key) {
  const auto found = values.find(std::string{key});
  return found == values.end() || found->second.empty() ? std::string{} : found->second.front();
}

[[nodiscard]] BlockPos sub_pos(BlockPos lhs, BlockPos rhs) noexcept {
  return {{
      lhs.value.x - rhs.value.x,
      lhs.value.y - rhs.value.y,
      lhs.value.z - rhs.value.z,
  }};
}

} // namespace

bool valid_blueprint_name(std::string_view name) noexcept {
  if (name.empty() || name == "." || name == ".." ||
      name.find("..") != std::string_view::npos) {
    return false;
  }
  for (const char c : name) {
    if (!safe_name_char(c)) {
      return false;
    }
  }
  return true;
}

std::string blueprint_key(std::string_view owner_namespace, std::string_view name) {
  return std::string{owner_namespace} + ":blueprints/" + std::string{name};
}

std::uint64_t blueprint_volume(BlockPos size) {
  if (size.value.x <= 0 || size.value.y <= 0 || size.value.z <= 0) {
    return 0U;
  }
  const std::uint64_t x = static_cast<std::uint64_t>(size.value.x);
  const std::uint64_t y = static_cast<std::uint64_t>(size.value.y);
  const std::uint64_t z = static_cast<std::uint64_t>(size.value.z);
  if (y > std::numeric_limits<std::uint64_t>::max() / x) {
    return 0U;
  }
  const std::uint64_t xy = x * y;
  if (z > std::numeric_limits<std::uint64_t>::max() / xy) {
    return 0U;
  }
  return xy * z;
}

Blueprint blueprint_from_clipboard(
    std::string name,
    const BlueprintClipboard& clipboard,
    std::string created_by) {
  Blueprint blueprint;
  blueprint.metadata.name = std::move(name);
  blueprint.metadata.created_by = std::move(created_by);
  blueprint.size = clipboard.size;
  blueprint.origin = sub_pos(clipboard.origin, clipboard.source_min);

  std::map<std::string, std::uint32_t> palette_indices;
  for (const BlueprintClipboardBlock& block : clipboard.blocks) {
    auto [iter, inserted] = palette_indices.emplace(
        block.stable_id,
        static_cast<std::uint32_t>(palette_indices.size()));
    if (inserted) {
      blueprint.palette.push_back(block.stable_id);
    }
    blueprint.blocks.push_back(iter->second);
  }
  return blueprint;
}

std::vector<BlueprintRleRun> encode_rle(const std::vector<std::uint32_t>& blocks) {
  std::vector<BlueprintRleRun> runs;
  for (const std::uint32_t state : blocks) {
    if (!runs.empty() && runs.back().state == state) {
      ++runs.back().count;
      continue;
    }
    runs.push_back({state, 1});
  }
  return runs;
}

bool decode_rle(
    const std::vector<BlueprintRleRun>& runs,
    std::uint64_t expected_count,
    std::uint32_t palette_size,
    std::vector<std::uint32_t>& out_blocks,
    std::string& out_error) {
  out_blocks.clear();
  out_blocks.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
      expected_count,
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))));

  std::uint64_t total{};
  for (const BlueprintRleRun& run : runs) {
    if (run.state >= palette_size) {
      out_error = "RLE run references a palette index that does not exist.";
      return false;
    }
    if (run.count == 0U || run.count > expected_count - total) {
      out_error = "RLE block count overflowed.";
      return false;
    }
    total += run.count;
    if (total > expected_count) {
      out_error = "RLE block count exceeds blueprint dimensions.";
      return false;
    }
    out_blocks.insert(out_blocks.end(), static_cast<std::size_t>(run.count), run.state);
  }

  if (total != expected_count) {
    out_error = "RLE block count does not match blueprint dimensions.";
    return false;
  }
  return true;
}

BlueprintCodecResult encode_blueprint(const Blueprint& blueprint) {
  const std::uint64_t expected = blueprint_volume(blueprint.size);
  if (expected == 0U || expected != blueprint.blocks.size()) {
    return {false, {}, {}, "Blueprint block count does not match its size."};
  }
  if (blueprint.schema_version != 1U || blueprint.format != BlueprintFormatID) {
    return {false, {}, {}, "Unsupported blueprint schema or format."};
  }
  if (blueprint.palette.empty()) {
    return {false, {}, {}, "Blueprint palette is empty."};
  }
  for (const std::string& entry : blueprint.palette) {
    if (!valid_block_state_id(entry)) {
      return {false, {}, {}, "Blueprint palette contains an invalid block state id."};
    }
  }
  for (const std::uint32_t state : blueprint.blocks) {
    if (state >= blueprint.palette.size()) {
      return {false, {}, {}, "Blueprint block references an invalid palette index."};
    }
  }

  std::ostringstream out;
  out << "schema_version: 1\n";
  out << "format: " << BlueprintFormatID << "\n";
  out << "metadata.name: " << escape_value(blueprint.metadata.name) << "\n";
  out << "metadata.author: " << escape_value(blueprint.metadata.author) << "\n";
  out << "metadata.created_by: " << escape_value(blueprint.metadata.created_by) << "\n";
  out << "size: " << blueprint.size.value.x << ' ' << blueprint.size.value.y
      << ' ' << blueprint.size.value.z << "\n";
  out << "origin: " << blueprint.origin.value.x << ' ' << blueprint.origin.value.y
      << ' ' << blueprint.origin.value.z << "\n";
  out << "block_order: x + size.x * (z + size.z * y)\n";
  out << "palette_count: " << blueprint.palette.size() << "\n";
  for (const std::string& entry : blueprint.palette) {
    out << "palette: " << escape_value(entry) << "\n";
  }
  const std::vector<BlueprintRleRun> runs = encode_rle(blueprint.blocks);
  out << "blocks.encoding: rle\n";
  out << "rle_count: " << runs.size() << "\n";
  for (const BlueprintRleRun& run : runs) {
    out << "rle: " << run.state << ' ' << run.count << "\n";
  }
  return {true, blueprint, out.str(), {}};
}

BlueprintCodecResult decode_blueprint(std::string_view text) {
  const std::map<std::string, std::vector<std::string>> values = parse_lines(text);
  Blueprint blueprint;

  std::uint32_t schema{};
  if (!parse_u32(get_one(values, "schema_version"), schema) || schema != 1U) {
    return {false, {}, {}, "Unsupported or missing blueprint schema_version."};
  }
  blueprint.schema_version = schema;
  blueprint.format = get_one(values, "format");
  if (blueprint.format != BlueprintFormatID) {
    return {false, {}, {}, "Unsupported or missing blueprint format."};
  }

  bool ok = true;
  blueprint.metadata.name = unescape_value(get_one(values, "metadata.name"), ok);
  if (!ok) {
    return {false, {}, {}, "Invalid escaped blueprint name."};
  }
  blueprint.metadata.author = unescape_value(get_one(values, "metadata.author"), ok);
  if (!ok) {
    return {false, {}, {}, "Invalid escaped blueprint author."};
  }
  blueprint.metadata.created_by = unescape_value(get_one(values, "metadata.created_by"), ok);
  if (!ok) {
    return {false, {}, {}, "Invalid escaped blueprint creator."};
  }

  if (!parse_pos(get_one(values, "size"), blueprint.size) ||
      blueprint_volume(blueprint.size) == 0U) {
    return {false, {}, {}, "Invalid blueprint size."};
  }
  if (!parse_pos(get_one(values, "origin"), blueprint.origin)) {
    return {false, {}, {}, "Invalid blueprint origin."};
  }
  if (get_one(values, "blocks.encoding") != "rle") {
    return {false, {}, {}, "Unsupported blueprint block encoding."};
  }

  std::uint64_t palette_count{};
  if (!parse_u64(get_one(values, "palette_count"), palette_count)) {
    return {false, {}, {}, "Missing blueprint palette_count."};
  }
  const auto palette_found = values.find("palette");
  if (palette_found == values.end() || palette_found->second.size() != palette_count) {
    return {false, {}, {}, "Blueprint palette count does not match palette entries."};
  }
  for (const std::string& encoded : palette_found->second) {
    std::string entry = unescape_value(encoded, ok);
    if (!ok || !valid_block_state_id(entry)) {
      return {false, {}, {}, "Blueprint palette contains an invalid block state id."};
    }
    blueprint.palette.push_back(std::move(entry));
  }

  std::uint64_t rle_count{};
  if (!parse_u64(get_one(values, "rle_count"), rle_count)) {
    return {false, {}, {}, "Missing blueprint rle_count."};
  }
  const auto rle_found = values.find("rle");
  if (rle_found == values.end() || rle_found->second.size() != rle_count) {
    return {false, {}, {}, "Blueprint RLE count does not match RLE entries."};
  }

  std::vector<BlueprintRleRun> runs;
  runs.reserve(rle_found->second.size());
  for (std::string_view encoded : rle_found->second) {
    const std::vector<std::string_view> words = split_words(encoded);
    if (words.size() != 2U) {
      return {false, {}, {}, "Invalid RLE run."};
    }
    BlueprintRleRun run;
    if (!parse_u32(words[0], run.state) || !parse_u64(words[1], run.count)) {
      return {false, {}, {}, "Invalid RLE run value."};
    }
    runs.push_back(run);
  }

  std::string error;
  if (!decode_rle(
          runs,
          blueprint_volume(blueprint.size),
          static_cast<std::uint32_t>(blueprint.palette.size()),
          blueprint.blocks,
          error)) {
    return {false, {}, {}, error};
  }
  return {true, blueprint, {}, {}};
}

} // namespace arv::blueprint
