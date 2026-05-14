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
  DataDocument() noexcept;
  DataDocument(const ArvAPI* api, ArvDataDocumentHandle handle) noexcept;

  DataDocument(const DataDocument&) = delete;
  DataDocument& operator=(const DataDocument&) = delete;

  DataDocument(DataDocument&& other) noexcept;

  DataDocument& operator=(DataDocument&& other) noexcept;

  ~DataDocument();

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] ArvDataDocumentHandle handle() const noexcept;

  void reset() noexcept;

  [[nodiscard]] std::uint32_t schema_version() const;

  [[nodiscard]] DataKind kind(std::string_view path = {}) const;

  [[nodiscard]] std::size_t size(std::string_view path = {}) const;

  [[nodiscard]] Result set_null(std::string_view path) const;

  [[nodiscard]] Result set_bool(std::string_view path, bool value) const;

  [[nodiscard]] Result set_i64(std::string_view path, std::int64_t value) const;

  [[nodiscard]] Result set_u64(std::string_view path, std::uint64_t value) const;

  [[nodiscard]] Result set_f64(std::string_view path, double value) const;

  [[nodiscard]] Result set_string(std::string_view path, std::string_view value) const;

  [[nodiscard]] Result set_bytes(std::string_view path, ArvBytesView value) const;

  [[nodiscard]] Result set_array(std::string_view path) const;

  [[nodiscard]] Result set_object(std::string_view path) const;

  [[nodiscard]] ValueResult<bool> bool_value(std::string_view path) const;

  [[nodiscard]] ValueResult<std::int64_t> i64_value(std::string_view path) const;

  [[nodiscard]] ValueResult<std::uint64_t> u64_value(std::string_view path) const;

  [[nodiscard]] ValueResult<double> f64_value(std::string_view path) const;

  [[nodiscard]] ValueResult<std::string_view> string_value(std::string_view path) const;

  [[nodiscard]] ValueResult<ArvBytesView> bytes_value(std::string_view path) const;

private:
  const ArvAPI* api_{};
  ArvDataDocumentHandle handle_{};
};

class Data {
public:
  explicit Data(const ArvAPI* api) noexcept;

  [[nodiscard]] ValueResult<DataDocument> create_document(std::uint32_t schema_version = 1) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
