#pragma once

#include "arv/data.hpp"
#include "arv/result.hpp"

namespace arv {

template<typename T>
struct DecodeResult {
  Result result{};
  T value{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result);
  }
};

template<typename T>
struct Codec;

} // namespace arv
