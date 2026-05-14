#include "arv/resources.hpp"

namespace arv {

Resources::Resources(const ArvAPI* api) noexcept : api_(api) {
}

bool Resources::exists(
    ResourceKind kind,
    std::string_view id,
    std::string_view extension) const {
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

BufferResult Resources::read(
    ResourceKind kind,
    std::string_view id,
    std::string_view extension) const {
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

BufferResult Resources::debug_resolved_path(
    ResourceKind kind,
    std::string_view id,
    std::string_view extension) const {
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

} // namespace arv
