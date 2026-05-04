#include "arv/crypto/hex.hpp"

#include <array>
#include <utility>

namespace arv::crypto {
namespace {

[[nodiscard]] constexpr int hex_value(char ch) noexcept {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  return -1;
}

} // namespace

std::string to_lower_hex(std::span<const std::byte> data) {
  static constexpr std::array<char, 16> digits{
      '0', '1', '2', '3', '4', '5', '6', '7',
      '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  std::string out;
  out.resize(data.size() * 2U);
  usize offset = 0;
  for (const std::byte byte : data) {
    const auto value = static_cast<u8>(byte);
    out[offset++] = digits[(value >> 4U) & 0x0FU];
    out[offset++] = digits[value & 0x0FU];
  }
  return out;
}

std::string to_prefixed_lower_hex(std::span<const std::byte> data) {
  std::string out{"0x"};
  out += to_lower_hex(data);
  return out;
}

Expected<std::vector<std::byte>, HexError> from_lower_hex(std::string_view text) {
  if ((text.size() % 2U) != 0U) {
    return Expected<std::vector<std::byte>, HexError>::failure(
        HexError{"hex text must contain an even number of digits"});
  }

  std::vector<std::byte> bytes;
  bytes.reserve(text.size() / 2U);
  for (usize index = 0; index < text.size(); index += 2U) {
    const int high = hex_value(text[index]);
    const int low = hex_value(text[index + 1U]);
    if (high < 0 || low < 0) {
      return Expected<std::vector<std::byte>, HexError>::failure(
          HexError{"hex text must be lowercase hexadecimal"});
    }
    bytes.push_back(static_cast<std::byte>((high << 4) | low));
  }
  return Expected<std::vector<std::byte>, HexError>::success(std::move(bytes));
}

Expected<std::vector<std::byte>, HexError> from_prefixed_lower_hex(std::string_view text) {
  if (text.size() < 2U || text.substr(0, 2) != "0x") {
    return Expected<std::vector<std::byte>, HexError>::failure(
        HexError{"hex text must start with 0x"});
  }
  return from_lower_hex(text.substr(2));
}

bool is_prefixed_lower_hex(std::string_view text) noexcept {
  if (text.size() < 2U || text.substr(0, 2) != "0x" ||
      ((text.size() - 2U) % 2U) != 0U) {
    return false;
  }
  for (usize index = 2U; index < text.size(); ++index) {
    if (hex_value(text[index]) < 0) {
      return false;
    }
  }
  return true;
}

} // namespace arv::crypto
