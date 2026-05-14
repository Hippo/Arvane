#include "arv/services.hpp"

#include <utility>

namespace arv {

ServiceRef::ServiceRef() noexcept = default;

ServiceRef::ServiceRef(const ArvAPI* api, ArvServiceRef ref) noexcept
    : api_(api), ref_(ref) {
}

ServiceRef::ServiceRef(ServiceRef&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      ref_(std::exchange(other.ref_, {})) {
}

ServiceRef& ServiceRef::operator=(ServiceRef&& other) noexcept {
  if (this != &other) {
    release();
    api_ = std::exchange(other.api_, nullptr);
    ref_ = std::exchange(other.ref_, {});
  }
  return *this;
}

ServiceRef::~ServiceRef() {
  release();
}

void ServiceRef::release() noexcept {
  if (api_ != nullptr && ref_.valid && api_->services != nullptr &&
      api_->services->release != nullptr) {
    api_->services->release(api_->services_ctx, ref_);
  }
  api_ = nullptr;
  ref_ = {};
}

ServiceRef::operator bool() const noexcept {
  return ref_.valid;
}

std::string_view ServiceRef::id() const noexcept {
  return view(ref_.service_id);
}

std::string_view ServiceRef::provider_pack_id() const noexcept {
  return view(ref_.provider_pack_id);
}

std::uint32_t ServiceRef::api_version() const noexcept {
  return ref_.api_version;
}

void* ServiceRef::user_data() const noexcept {
  return ref_.user_data;
}

const ArvServiceRef& ServiceRef::abi() const noexcept {
  return ref_;
}

ServiceQuery::operator bool() const noexcept {
  return static_cast<bool>(result) && static_cast<bool>(service);
}

Services::Services(const ArvAPI* api) noexcept : api_(api) {
}

Result Services::publish(
    const ServiceDescriptor& descriptor,
    ArvServiceHandle* out_handle) const {
  if (api_ == nullptr || api_->services == nullptr || api_->services->publish == nullptr) {
    return Result::unsupported("service API is unavailable");
  }
  ArvServiceDescriptor abi_descriptor{};
  abi_descriptor.service_id = view(descriptor.id);
  abi_descriptor.api_version = descriptor.api_version;
  abi_descriptor.priority = descriptor.priority;
  abi_descriptor.threading = static_cast<ArvServiceThreading>(descriptor.threading);
  abi_descriptor.display_name = view(descriptor.display_name);
  abi_descriptor.api_table = descriptor.api_table;
  abi_descriptor.user_data = descriptor.user_data;
  return Result{api_->services->publish(api_->services_ctx, &abi_descriptor, out_handle)};
}

Result Services::unpublish(ArvServiceHandle handle) const {
  if (api_ == nullptr || api_->services == nullptr ||
      api_->services->unpublish == nullptr) {
    return Result::unsupported("service API is unavailable");
  }
  return Result{api_->services->unpublish(api_->services_ctx, handle)};
}

ServiceQuery Services::get(
    std::string_view id,
    std::uint32_t min_api_version,
    std::uint32_t max_api_version) const {
  if (api_ == nullptr || api_->services == nullptr || api_->services->get == nullptr) {
    return {Result::unsupported("service API is unavailable"), {}};
  }
  ArvServiceRef ref{};
  Result result{api_->services->get(
      api_->services_ctx,
      view(id),
      min_api_version,
      max_api_version,
      &ref)};
  return {result, ServiceRef{api_, ref}};
}

std::vector<ServiceRef> Services::list(
    std::string_view id,
    std::uint32_t min_api_version,
    std::uint32_t max_api_version) const {
  std::vector<ServiceRef> results;
  if (api_ == nullptr || api_->services == nullptr || api_->services->list == nullptr) {
    return results;
  }
  const std::uint64_t count = api_->services->list(
      api_->services_ctx,
      view(id),
      min_api_version,
      max_api_version,
      nullptr,
      0);
  std::vector<ArvServiceRef> refs(count);
  api_->services->list(
      api_->services_ctx,
      view(id),
      min_api_version,
      max_api_version,
      refs.data(),
      refs.size());
  results.reserve(refs.size());
  for (ArvServiceRef& ref : refs) {
    results.emplace_back(api_, ref);
  }
  return results;
}

} // namespace arv
