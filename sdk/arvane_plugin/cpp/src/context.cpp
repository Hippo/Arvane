#include "arv/context.hpp"

namespace arv {

Context::Context(const ArvAPI* api) noexcept : api_(api) {
}

std::string_view Context::pack_id() const {
  if (!available() || api_->context->pack_id == nullptr) {
    return {};
  }
  return view(api_->context->pack_id(api_->context_ctx));
}

std::string_view Context::pack_version() const {
  if (!available() || api_->context->pack_version == nullptr) {
    return {};
  }
  return view(api_->context->pack_version(api_->context_ctx));
}

std::string_view Context::side() const {
  if (!available() || api_->context->side == nullptr) {
    return {};
  }
  return view(api_->context->side(api_->context_ctx));
}

std::uint32_t Context::namespace_count() const {
  if (!available() || api_->context->namespace_count == nullptr) {
    return 0;
  }
  return api_->context->namespace_count(api_->context_ctx);
}

std::string_view Context::namespace_at(std::uint32_t index) const {
  if (!available() || api_->context->namespace_at == nullptr) {
    return {};
  }
  return view(api_->context->namespace_at(api_->context_ctx, index));
}

std::vector<std::string_view> Context::namespaces() const {
  std::vector<std::string_view> values;
  const std::uint32_t count = namespace_count();
  values.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    values.push_back(namespace_at(i));
  }
  return values;
}

bool Context::owns_namespace(std::string_view name_space) const {
  if (!available() || api_->context->owns_namespace == nullptr) {
    return false;
  }
  return api_->context->owns_namespace(api_->context_ctx, view(name_space));
}

std::string Context::config_root() const {
  if (!available() || api_->context->config_root == nullptr) {
    return {};
  }
  return std::string{view(api_->context->config_root(api_->context_ctx))};
}

bool Context::available() const noexcept {
  return api_ != nullptr && api_->context != nullptr;
}

} // namespace arv
