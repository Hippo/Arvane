#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace arv::protocol {

struct ProtocolError {
  std::string message{};
};

namespace detail {

template<typename T, bool IsEnum = std::is_enum_v<T>>
struct WireValueType {
  using type = T;
};

template<typename T>
struct WireValueType<T, true> {
  using type = std::underlying_type_t<T>;
};

template<typename T>
using WireValueTypeT = typename WireValueType<T>::type;

} // namespace detail

class MessageWriter {
public:
  template<typename T>
  void write(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = detail::WireValueTypeT<T>;
    const Raw raw = static_cast<Raw>(value);
    for (usize i = 0; i < sizeof(Raw); ++i) {
      bytes_.push_back(static_cast<u8>((raw >> (i * 8U)) & 0xFFU));
    }
  }

  void write_bool(bool value) {
    write<u8>(value ? 1U : 0U);
  }

  void write_bytes(std::span<const u8> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  void write_string(std::string_view value) {
    const auto length = static_cast<u16>(value.size());
    write<u16>(length);
    const auto* first = reinterpret_cast<const u8*>(value.data());
    bytes_.insert(bytes_.end(), first, first + length);
  }

  [[nodiscard]] const std::vector<u8>& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<u8> take_bytes() && {
    return std::move(bytes_);
  }

private:
  std::vector<u8> bytes_;
};

class MessageReader {
public:
  explicit MessageReader(std::span<const u8> bytes) : bytes_(bytes) {}

  template<typename T>
  [[nodiscard]] Expected<T, ProtocolError> read() {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = detail::WireValueTypeT<T>;
    if (remaining() < sizeof(Raw)) {
      return Expected<T, ProtocolError>::failure(ProtocolError{"not enough bytes"});
    }

    Raw raw{};
    for (usize i = 0; i < sizeof(Raw); ++i) {
      raw |= static_cast<Raw>(bytes_[offset_ + i]) << (i * 8U);
    }
    offset_ += sizeof(Raw);
    return Expected<T, ProtocolError>::success(static_cast<T>(raw));
  }

  [[nodiscard]] Expected<bool, ProtocolError> read_bool() {
    auto value = read<u8>();
    if (!value) {
      return Expected<bool, ProtocolError>::failure(value.error());
    }
    if (value.value() > 1) {
      return Expected<bool, ProtocolError>::failure(ProtocolError{"invalid bool encoding"});
    }
    return Expected<bool, ProtocolError>::success(value.value() == 1);
  }

  [[nodiscard]] Expected<std::string, ProtocolError> read_string() {
    auto length = read<u16>();
    if (!length) {
      return Expected<std::string, ProtocolError>::failure(length.error());
    }
    if (remaining() < length.value()) {
      return Expected<std::string, ProtocolError>::failure(ProtocolError{"not enough bytes for string"});
    }

    const auto* first = reinterpret_cast<const char*>(bytes_.data() + offset_);
    std::string value(first, first + length.value());
    offset_ += length.value();
    return Expected<std::string, ProtocolError>::success(std::move(value));
  }

  [[nodiscard]] usize remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  std::span<const u8> bytes_;
  usize offset_{};
};

} // namespace arv::protocol
