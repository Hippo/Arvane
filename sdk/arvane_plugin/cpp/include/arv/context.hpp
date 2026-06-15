#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arv {

class Context {
public:
  explicit Context(const ArvAPI* api) noexcept;

  [[nodiscard]] std::string_view pack_id() const;

  [[nodiscard]] std::string_view pack_version() const;

  [[nodiscard]] std::string_view side() const;

  [[nodiscard]] std::uint32_t namespace_count() const;

  [[nodiscard]] std::string_view namespace_at(std::uint32_t index) const;

  [[nodiscard]] std::vector<std::string_view> namespaces() const;

  [[nodiscard]] bool owns_namespace(std::string_view name_space) const;

  [[nodiscard]] std::string config_root() const;

private:
  [[nodiscard]] bool available() const noexcept;

  const ArvAPI* api_{};
};

} // namespace arv
