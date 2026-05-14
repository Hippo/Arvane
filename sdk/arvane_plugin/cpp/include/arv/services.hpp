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
  ServiceRef() noexcept;
  ServiceRef(const ArvAPI* api, ArvServiceRef ref) noexcept;

  ServiceRef(const ServiceRef&) = delete;
  ServiceRef& operator=(const ServiceRef&) = delete;

  ServiceRef(ServiceRef&& other) noexcept;

  ServiceRef& operator=(ServiceRef&& other) noexcept;

  ~ServiceRef();

  void release() noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;

  [[nodiscard]] std::string_view id() const noexcept;

  [[nodiscard]] std::string_view provider_pack_id() const noexcept;

  [[nodiscard]] std::uint32_t api_version() const noexcept;

  template<typename T>
  [[nodiscard]] const T* table() const noexcept {
    return static_cast<const T*>(ref_.api_table);
  }

  [[nodiscard]] void* user_data() const noexcept;

  [[nodiscard]] const ArvServiceRef& abi() const noexcept;

private:
  const ArvAPI* api_{};
  ArvServiceRef ref_{};
};

struct ServiceQuery {
  Result result{};
  ServiceRef service{};

  [[nodiscard]] explicit operator bool() const noexcept;
};

class Services {
public:
  explicit Services(const ArvAPI* api) noexcept;

  [[nodiscard]] Result publish(
      const ServiceDescriptor& descriptor,
      ArvServiceHandle* out_handle = nullptr) const;

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

  [[nodiscard]] Result unpublish(ArvServiceHandle handle) const;

  [[nodiscard]] ServiceQuery get(
      std::string_view id,
      std::uint32_t min_api_version,
      std::uint32_t max_api_version) const;

  [[nodiscard]] std::vector<ServiceRef> list(
      std::string_view id,
      std::uint32_t min_api_version,
      std::uint32_t max_api_version) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
