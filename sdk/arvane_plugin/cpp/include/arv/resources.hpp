#pragma once

#include "arv/api.h"
#include "arv/buffer.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

enum class ResourceKind {
  Data = ARV_RESOURCE_DATA,
  Asset = ARV_RESOURCE_ASSET,
  Config = ARV_RESOURCE_CONFIG,
};

class Resources {
public:
  explicit Resources(const ArvAPI* api) noexcept;

  [[nodiscard]] bool exists(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const;

  [[nodiscard]] BufferResult read(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const;

  [[nodiscard]] BufferResult debug_resolved_path(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
