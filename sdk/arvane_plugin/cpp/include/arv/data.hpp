#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>

namespace arv {

enum class DataKind {
  Null = ARV_DATA_NULL,
  Bool = ARV_DATA_BOOL,
  I64 = ARV_DATA_I64,
  U64 = ARV_DATA_U64,
  F64 = ARV_DATA_F64,
  String = ARV_DATA_STRING,
  Bytes = ARV_DATA_BYTES,
  Array = ARV_DATA_ARRAY,
  Object = ARV_DATA_OBJECT,
  Invalid = ARV_DATA_INVALID,
};

class DataDocument {
public:
  constexpr DataDocument() noexcept = default;
  constexpr DataDocument(const ArvAPI* api, ArvDataDocumentHandle handle) noexcept
      : api_(api),
        handle_(handle) {
  }

  DataDocument(const DataDocument&) = delete;
  DataDocument& operator=(const DataDocument&) = delete;

  DataDocument(DataDocument&& other) noexcept
      : api_(other.api_),
        handle_(other.handle_) {
    other.api_ = nullptr;
    other.handle_ = 0;
  }

  DataDocument& operator=(DataDocument&& other) noexcept {
    if (this != &other) {
      reset();
      api_ = other.api_;
      handle_ = other.handle_;
      other.api_ = nullptr;
      other.handle_ = 0;
    }
    return *this;
  }

  ~DataDocument() {
    reset();
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return api_ != nullptr && api_->data != nullptr && handle_ != 0;
  }

  [[nodiscard]] constexpr ArvDataDocumentHandle handle() const noexcept {
    return handle_;
  }

  void reset() noexcept {
    if (api_ != nullptr && api_->data != nullptr &&
        api_->data->release_document != nullptr && handle_ != 0) {
      api_->data->release_document(api_->data_ctx, handle_);
    }
    api_ = nullptr;
    handle_ = 0;
  }

  [[nodiscard]] std::uint32_t schema_version() const {
    if (!valid() || api_->data->schema_version == nullptr) {
      return 0;
    }
    return api_->data->schema_version(api_->data_ctx, handle_);
  }

  [[nodiscard]] DataKind kind(std::string_view path = {}) const {
    if (!valid() || api_->data->kind == nullptr) {
      return DataKind::Invalid;
    }
    return static_cast<DataKind>(api_->data->kind(api_->data_ctx, handle_, view(path)));
  }

  [[nodiscard]] std::size_t size(std::string_view path = {}) const {
    if (!valid() || api_->data->size == nullptr) {
      return 0;
    }
    return api_->data->size(api_->data_ctx, handle_, view(path));
  }

  [[nodiscard]] Result set_null(std::string_view path) const {
    if (!valid() || api_->data->set_null == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_null(api_->data_ctx, handle_, view(path))};
  }

  [[nodiscard]] Result set_bool(std::string_view path, bool value) const {
    if (!valid() || api_->data->set_bool == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_bool(api_->data_ctx, handle_, view(path), value)};
  }

  [[nodiscard]] Result set_i64(std::string_view path, std::int64_t value) const {
    if (!valid() || api_->data->set_i64 == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_i64(api_->data_ctx, handle_, view(path), value)};
  }

  [[nodiscard]] Result set_u64(std::string_view path, std::uint64_t value) const {
    if (!valid() || api_->data->set_u64 == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_u64(api_->data_ctx, handle_, view(path), value)};
  }

  [[nodiscard]] Result set_f64(std::string_view path, double value) const {
    if (!valid() || api_->data->set_f64 == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_f64(api_->data_ctx, handle_, view(path), value)};
  }

  [[nodiscard]] Result set_string(std::string_view path, std::string_view value) const {
    if (!valid() || api_->data->set_string == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_string(api_->data_ctx, handle_, view(path), view(value))};
  }

  [[nodiscard]] Result set_bytes(std::string_view path, ArvBytesView value) const {
    if (!valid() || api_->data->set_bytes == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_bytes(api_->data_ctx, handle_, view(path), value)};
  }

  [[nodiscard]] Result set_array(std::string_view path) const {
    if (!valid() || api_->data->set_array == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_array(api_->data_ctx, handle_, view(path))};
  }

  [[nodiscard]] Result set_object(std::string_view path) const {
    if (!valid() || api_->data->set_object == nullptr) {
      return Result::unsupported("data API is unavailable");
    }
    return Result{api_->data->set_object(api_->data_ctx, handle_, view(path))};
  }

  [[nodiscard]] ValueResult<bool> bool_value(std::string_view path) const {
    bool value{};
    if (!valid() || api_->data->get_bool == nullptr) {
      return {Result::unsupported("data API is unavailable"), value};
    }
    return {Result{api_->data->get_bool(api_->data_ctx, handle_, view(path), &value)}, value};
  }

  [[nodiscard]] ValueResult<std::int64_t> i64_value(std::string_view path) const {
    std::int64_t value{};
    if (!valid() || api_->data->get_i64 == nullptr) {
      return {Result::unsupported("data API is unavailable"), value};
    }
    return {Result{api_->data->get_i64(api_->data_ctx, handle_, view(path), &value)}, value};
  }

  [[nodiscard]] ValueResult<std::uint64_t> u64_value(std::string_view path) const {
    std::uint64_t value{};
    if (!valid() || api_->data->get_u64 == nullptr) {
      return {Result::unsupported("data API is unavailable"), value};
    }
    return {Result{api_->data->get_u64(api_->data_ctx, handle_, view(path), &value)}, value};
  }

  [[nodiscard]] ValueResult<double> f64_value(std::string_view path) const {
    double value{};
    if (!valid() || api_->data->get_f64 == nullptr) {
      return {Result::unsupported("data API is unavailable"), value};
    }
    return {Result{api_->data->get_f64(api_->data_ctx, handle_, view(path), &value)}, value};
  }

  [[nodiscard]] ValueResult<std::string_view> string_value(std::string_view path) const {
    ArvStringView value{};
    if (!valid() || api_->data->get_string == nullptr) {
      return {Result::unsupported("data API is unavailable"), {}};
    }
    return {
        Result{api_->data->get_string(api_->data_ctx, handle_, view(path), &value)},
        view(value)};
  }

  [[nodiscard]] ValueResult<ArvBytesView> bytes_value(std::string_view path) const {
    ArvBytesView value{};
    if (!valid() || api_->data->get_bytes == nullptr) {
      return {Result::unsupported("data API is unavailable"), value};
    }
    return {Result{api_->data->get_bytes(api_->data_ctx, handle_, view(path), &value)}, value};
  }

private:
  const ArvAPI* api_{};
  ArvDataDocumentHandle handle_{};
};

class Data {
public:
  explicit constexpr Data(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] ValueResult<DataDocument> create_document(std::uint32_t schema_version = 1) const {
    if (api_ == nullptr || api_->data == nullptr ||
        api_->data->create_document == nullptr) {
      return {Result::unsupported("data API is unavailable"), {}};
    }
    const ArvDataDocumentHandle handle =
        api_->data->create_document(api_->data_ctx, schema_version);
    if (handle == 0) {
      return {Result{ResultCode::InternalError, "failed to create data document"}, {}};
    }
    return {Result::ok(), DataDocument{api_, handle}};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
