#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Logger {
public:
  explicit Logger(const ArvAPI* api) noexcept;

  void trace(std::string_view message) const;

  void debug(std::string_view message) const;

  void info(std::string_view message) const;

  void warn(std::string_view message) const;

  void error(std::string_view message) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
