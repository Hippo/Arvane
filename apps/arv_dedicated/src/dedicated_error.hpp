#pragma once

#include <string>
#include <utility>

namespace arv::dedicated {

struct DedicatedError {
  std::string category{};
  std::string message{};
};

[[nodiscard]] inline DedicatedError dedicated_error(
    std::string category,
    std::string message) {
  return {std::move(category), std::move(message)};
}

} // namespace arv::dedicated
