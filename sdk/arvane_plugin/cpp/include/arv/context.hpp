#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace arv {

class Context {
public:
  explicit constexpr Context(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] std::string_view pack_id() const {
    if (!available() || api_->context->pack_id == nullptr) {
      return {};
    }
    return view(api_->context->pack_id(api_->context_ctx));
  }

  [[nodiscard]] std::string_view pack_version() const {
    if (!available() || api_->context->pack_version == nullptr) {
      return {};
    }
    return view(api_->context->pack_version(api_->context_ctx));
  }

  [[nodiscard]] std::string_view side() const {
    if (!available() || api_->context->side == nullptr) {
      return {};
    }
    return view(api_->context->side(api_->context_ctx));
  }

  [[nodiscard]] std::uint32_t namespace_count() const {
    if (!available() || api_->context->namespace_count == nullptr) {
      return 0;
    }
    return api_->context->namespace_count(api_->context_ctx);
  }

  [[nodiscard]] std::string_view namespace_at(std::uint32_t index) const {
    if (!available() || api_->context->namespace_at == nullptr) {
      return {};
    }
    return view(api_->context->namespace_at(api_->context_ctx, index));
  }

  [[nodiscard]] std::vector<std::string_view> namespaces() const {
    std::vector<std::string_view> values;
    const std::uint32_t count = namespace_count();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      values.push_back(namespace_at(i));
    }
    return values;
  }

  [[nodiscard]] bool owns_namespace(std::string_view name_space) const {
    if (!available() || api_->context->owns_namespace == nullptr) {
      return false;
    }
    return api_->context->owns_namespace(api_->context_ctx, view(name_space));
  }

private:
  [[nodiscard]] bool available() const noexcept {
    return api_ != nullptr && api_->context != nullptr;
  }

  const ArvAPI* api_{};
};

} // namespace arv
