#include "desktop_save_helpers.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <limits>
#include <system_error>

namespace arv::desktop {

char lowercase_ascii(char ch) noexcept {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool is_save_name_character(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

std::string sanitize_save_name(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  bool previous_separator = false;
  for (char ch : name) {
    ch = lowercase_ascii(ch);
    if (is_save_name_character(ch)) {
      result.push_back(ch);
      previous_separator = false;
      continue;
    }
    if ((ch == '-' || ch == '_' || ch == ' ' || ch == '.') && !result.empty() &&
        !previous_separator) {
      result.push_back('-');
      previous_separator = true;
    }
  }
  while (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  if (result.empty() || result == "." || result == "..") {
    return "world";
  }
  return result;
}

std::string unique_server_save_name(
    const io::EngineConfig& config,
    std::string_view display_name) {
  const std::string base = sanitize_save_name(display_name);
  std::string candidate = base;
  for (u32 suffix = 2; std::filesystem::exists(config.paths.server_save_path(candidate));
       ++suffix) {
    candidate = base + "-" + std::to_string(suffix);
  }
  return candidate;
}

u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<u64>(now == 0 ? 1 : now);
}

u64 parse_or_generate_seed(std::string_view seed_text) noexcept {
  if (seed_text.empty()) {
    return generated_world_seed();
  }

  u64 seed = 0;
  const char* begin = seed_text.data();
  const char* end = seed_text.data() + seed_text.size();
  const auto parsed = std::from_chars(begin, end, seed);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return generated_world_seed();
  }
  return seed;
}

std::optional<net::NetworkAddress> parse_server_address(std::string_view address) {
  const usize colon = address.rfind(':');
  net::NetworkAddress parsed;
  if (colon == std::string_view::npos) {
    if (address.empty()) {
      return std::nullopt;
    }
    parsed.host = std::string{address};
    return parsed;
  }

  if (colon == 0U || colon + 1U >= address.size() || address.find(':') != colon) {
    return std::nullopt;
  }

  u32 port = 0;
  const std::string_view port_text = address.substr(colon + 1U);
  const char* begin = port_text.data();
  const char* end = port_text.data() + port_text.size();
  const auto port_result = std::from_chars(begin, end, port);
  if (port_result.ec != std::errc{} || port_result.ptr != end || port == 0U ||
      port > std::numeric_limits<u16>::max()) {
    return std::nullopt;
  }

  parsed.host = std::string{address.substr(0U, colon)};
  parsed.port = static_cast<u16>(port);
  return parsed;
}

} // namespace arv::desktop
