#pragma once

namespace arv {

class Result;

template<typename T, typename ResultType = Result>
struct DecodeResult {
  ResultType result{};
  T value{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result);
  }
};

template<typename T>
struct Codec;

} // namespace arv
