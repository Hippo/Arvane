#pragma once

#include "arv/core/types.hpp"

#include <compare>
#include <functional>

namespace arv {

template<typename Tag, typename ValueType = u32>
struct ID {
  using value_type = ValueType;

  ValueType value{};

  constexpr ID() = default;
  constexpr explicit ID(ValueType v) : value(v) {}

  constexpr bool operator==(const ID&) const = default;
  constexpr auto operator<=>(const ID&) const = default;
};

} // namespace arv

template<typename Tag, typename ValueType>
struct std::hash<arv::ID<Tag, ValueType>> {
  std::size_t operator()(const arv::ID<Tag, ValueType>& id) const noexcept {
    return std::hash<ValueType>{}(id.value);
  }
};
