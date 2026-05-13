#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace arv {

enum class ServiceThreading {
  SyncOnly = ARV_SERVICE_SYNC_ONLY,
  AsyncSafe = ARV_SERVICE_ASYNC_SAFE,
  AnyThread = ARV_SERVICE_ANY_THREAD,
};

struct ServiceDescriptor {
  std::string_view id{};
  std::uint32_t api_version{};
  const void* api_table{};
  void* user_data{};
  std::int32_t priority{};
  ServiceThreading threading{ServiceThreading::SyncOnly};
  std::string_view display_name{};
};

class ServiceRef {
public:
  ServiceRef() = default;
  ServiceRef(const ArvAPI* api, ArvServiceRef ref) noexcept : api_(api), ref_(ref) {
  }

  ServiceRef(const ServiceRef&) = delete;
  ServiceRef& operator=(const ServiceRef&) = delete;

  ServiceRef(ServiceRef&& other) noexcept
      : api_(std::exchange(other.api_, nullptr)),
        ref_(std::exchange(other.ref_, {})) {
  }

  ServiceRef& operator=(ServiceRef&& other) noexcept {
    if (this != &other) {
      release();
      api_ = std::exchange(other.api_, nullptr);
      ref_ = std::exchange(other.ref_, {});
    }
    return *this;
  }

  ~ServiceRef() {
    release();
  }

  void release() noexcept {
    if (api_ != nullptr && ref_.valid && api_->services != nullptr &&
        api_->services->release != nullptr) {
      api_->services->release(api_->services_ctx, ref_);
    }
    api_ = nullptr;
    ref_ = {};
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return ref_.valid;
  }

  [[nodiscard]] std::string_view id() const noexcept {
    return view(ref_.service_id);
  }

  [[nodiscard]] std::string_view provider_pack_id() const noexcept {
    return view(ref_.provider_pack_id);
  }

  [[nodiscard]] std::uint32_t api_version() const noexcept {
    return ref_.api_version;
  }

  template<typename T>
  [[nodiscard]] const T* table() const noexcept {
    return static_cast<const T*>(ref_.api_table);
  }

  [[nodiscard]] void* user_data() const noexcept {
    return ref_.user_data;
  }

  [[nodiscard]] const ArvServiceRef& abi() const noexcept {
    return ref_;
  }

private:
  const ArvAPI* api_{};
  ArvServiceRef ref_{};
};

struct ServiceQuery {
  Result result{};
  ServiceRef service{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result) && static_cast<bool>(service);
  }
};

class Services {
public:
  explicit constexpr Services(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result publish(
      const ServiceDescriptor& descriptor,
      ArvServiceHandle* out_handle = nullptr) const {
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

  template<typename T>
  [[nodiscard]] Result publish(
      std::string_view id,
      std::uint32_t api_version,
      const T& api_table,
      ArvServiceHandle* out_handle = nullptr,
      std::string_view display_name = {},
      ServiceThreading threading = ServiceThreading::SyncOnly,
      std::int32_t priority = 0) const {
    return publish(
        {
            id,
            api_version,
            &api_table,
            nullptr,
            priority,
            threading,
            display_name,
        },
        out_handle);
  }

  [[nodiscard]] Result unpublish(ArvServiceHandle handle) const {
    if (api_ == nullptr || api_->services == nullptr ||
        api_->services->unpublish == nullptr) {
      return Result::unsupported("service API is unavailable");
    }
    return Result{api_->services->unpublish(api_->services_ctx, handle)};
  }

  [[nodiscard]] ServiceQuery get(
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

  [[nodiscard]] std::vector<ServiceRef> list(
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

private:
  const ArvAPI* api_{};
};

} // namespace arv
