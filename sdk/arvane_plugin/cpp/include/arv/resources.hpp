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
  explicit constexpr Resources(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] bool exists(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const {
    if (api_ == nullptr || api_->resources == nullptr ||
        api_->resources->exists == nullptr) {
      return false;
    }
    return api_->resources->exists(
        api_->resources_ctx,
        static_cast<ArvResourceKind>(kind),
        view(id),
        view(extension));
  }

  [[nodiscard]] BufferResult read(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const {
    if (api_ == nullptr || api_->resources == nullptr ||
        api_->resources->read == nullptr) {
      return {Result::unsupported("resource API is unavailable"), {}};
    }
    ArvByteBuffer buffer{};
    Result result{api_->resources->read(
        api_->resources_ctx,
        static_cast<ArvResourceKind>(kind),
        view(id),
        view(extension),
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }

  [[nodiscard]] BufferResult debug_resolved_path(
      ResourceKind kind,
      std::string_view id,
      std::string_view extension = {}) const {
    if (api_ == nullptr || api_->resources == nullptr ||
        api_->resources->debug_resolved_path == nullptr) {
      return {Result::unsupported("resource API is unavailable"), {}};
    }
    ArvByteBuffer buffer{};
    Result result{api_->resources->debug_resolved_path(
        api_->resources_ctx,
        static_cast<ArvResourceKind>(kind),
        view(id),
        view(extension),
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
