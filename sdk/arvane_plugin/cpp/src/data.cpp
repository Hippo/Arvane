#include "arv/data.hpp"

#include <utility>

namespace arv {

DataDocument::DataDocument() noexcept = default;

DataDocument::DataDocument(const ArvAPI* api, ArvDataDocumentHandle handle) noexcept
    : api_(api), handle_(handle) {
}

DataDocument::DataDocument(DataDocument&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      handle_(std::exchange(other.handle_, 0)) {
}

DataDocument& DataDocument::operator=(DataDocument&& other) noexcept {
  if (this != &other) {
    reset();
    api_ = std::exchange(other.api_, nullptr);
    handle_ = std::exchange(other.handle_, 0);
  }
  return *this;
}

DataDocument::~DataDocument() {
  reset();
}

bool DataDocument::valid() const noexcept {
  return api_ != nullptr && api_->data != nullptr && handle_ != 0;
}

ArvDataDocumentHandle DataDocument::handle() const noexcept {
  return handle_;
}

void DataDocument::reset() noexcept {
  if (api_ != nullptr && api_->data != nullptr &&
      api_->data->release_document != nullptr && handle_ != 0) {
    api_->data->release_document(api_->data_ctx, handle_);
  }
  api_ = nullptr;
  handle_ = 0;
}

std::uint32_t DataDocument::schema_version() const {
  if (!valid() || api_->data->schema_version == nullptr) {
    return 0;
  }
  return api_->data->schema_version(api_->data_ctx, handle_);
}

DataKind DataDocument::kind(std::string_view path) const {
  if (!valid() || api_->data->kind == nullptr) {
    return DataKind::Invalid;
  }
  return static_cast<DataKind>(api_->data->kind(api_->data_ctx, handle_, view(path)));
}

std::size_t DataDocument::size(std::string_view path) const {
  if (!valid() || api_->data->size == nullptr) {
    return 0;
  }
  return api_->data->size(api_->data_ctx, handle_, view(path));
}

Expected<bool, Result> DataDocument::array_bool_value(
    std::string_view path,
    std::size_t index) const {
  bool value{};
  if (!valid() || api_->data->array_get_bool == nullptr) {
    return Expected<bool, Result>::failure(Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->array_get_bool(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<bool, Result>::success(value)
                : Expected<bool, Result>::failure(result);
}

bool DataDocument::array_bool_value(
    std::string_view path,
    std::size_t index,
    bool fallback) const {
  auto result = array_bool_value(path, index);
  return result ? result.value() : fallback;
}

Expected<std::int64_t, Result> DataDocument::array_i64_value(
    std::string_view path,
    std::size_t index) const {
  std::int64_t value{};
  if (!valid() || api_->data->array_get_i64 == nullptr) {
    return Expected<std::int64_t, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->array_get_i64(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<std::int64_t, Result>::success(value)
                : Expected<std::int64_t, Result>::failure(result);
}

std::int64_t DataDocument::array_i64_value(
    std::string_view path,
    std::size_t index,
    std::int64_t fallback) const {
  auto result = array_i64_value(path, index);
  return result ? result.value() : fallback;
}

Expected<std::uint64_t, Result> DataDocument::array_u64_value(
    std::string_view path,
    std::size_t index) const {
  std::uint64_t value{};
  if (!valid() || api_->data->array_get_u64 == nullptr) {
    return Expected<std::uint64_t, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->array_get_u64(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<std::uint64_t, Result>::success(value)
                : Expected<std::uint64_t, Result>::failure(result);
}

std::uint64_t DataDocument::array_u64_value(
    std::string_view path,
    std::size_t index,
    std::uint64_t fallback) const {
  auto result = array_u64_value(path, index);
  return result ? result.value() : fallback;
}

Expected<double, Result> DataDocument::array_f64_value(
    std::string_view path,
    std::size_t index) const {
  double value{};
  if (!valid() || api_->data->array_get_f64 == nullptr) {
    return Expected<double, Result>::failure(Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->array_get_f64(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<double, Result>::success(value)
                : Expected<double, Result>::failure(result);
}

double DataDocument::array_f64_value(
    std::string_view path,
    std::size_t index,
    double fallback) const {
  auto result = array_f64_value(path, index);
  return result ? result.value() : fallback;
}

Expected<std::string_view, Result> DataDocument::array_string_value(
    std::string_view path,
    std::size_t index) const {
  ArvStringView value{};
  if (!valid() || api_->data->array_get_string == nullptr) {
    return Expected<std::string_view, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->array_get_string(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<std::string_view, Result>::success(view(value))
                : Expected<std::string_view, Result>::failure(result);
}

std::string_view DataDocument::array_string_value(
    std::string_view path,
    std::size_t index,
    std::string_view fallback) const {
  auto result = array_string_value(path, index);
  return result ? result.value() : fallback;
}

Expected<std::string_view, Result> DataDocument::object_key_at(
    std::string_view path,
    std::size_t index) const {
  ArvStringView value{};
  if (!valid() || api_->data->object_key_at == nullptr) {
    return Expected<std::string_view, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->object_key_at(
      api_->data_ctx,
      handle_,
      view(path),
      index,
      &value)};
  return result ? Expected<std::string_view, Result>::success(view(value))
                : Expected<std::string_view, Result>::failure(result);
}

Result DataDocument::set_null(std::string_view path) const {
  if (!valid() || api_->data->set_null == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_null(api_->data_ctx, handle_, view(path))};
}

Result DataDocument::set_bool(std::string_view path, bool value) const {
  if (!valid() || api_->data->set_bool == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_bool(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::set_i64(std::string_view path, std::int64_t value) const {
  if (!valid() || api_->data->set_i64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_i64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::set_u64(std::string_view path, std::uint64_t value) const {
  if (!valid() || api_->data->set_u64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_u64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::set_f64(std::string_view path, double value) const {
  if (!valid() || api_->data->set_f64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_f64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::set_string(std::string_view path, std::string_view value) const {
  if (!valid() || api_->data->set_string == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_string(api_->data_ctx, handle_, view(path), view(value))};
}

Result DataDocument::set_bytes(std::string_view path, ArvBytesView value) const {
  if (!valid() || api_->data->set_bytes == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_bytes(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::set_array(std::string_view path) const {
  if (!valid() || api_->data->set_array == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_array(api_->data_ctx, handle_, view(path))};
}

Result DataDocument::set_object(std::string_view path) const {
  if (!valid() || api_->data->set_object == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->set_object(api_->data_ctx, handle_, view(path))};
}

Result DataDocument::array_push_null(std::string_view path) const {
  if (!valid() || api_->data->array_push_null == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_null(api_->data_ctx, handle_, view(path))};
}

Result DataDocument::array_push_bool(std::string_view path, bool value) const {
  if (!valid() || api_->data->array_push_bool == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_bool(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::array_push_i64(std::string_view path, std::int64_t value) const {
  if (!valid() || api_->data->array_push_i64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_i64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::array_push_u64(std::string_view path, std::uint64_t value) const {
  if (!valid() || api_->data->array_push_u64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_u64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::array_push_f64(std::string_view path, double value) const {
  if (!valid() || api_->data->array_push_f64 == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_f64(api_->data_ctx, handle_, view(path), value)};
}

Result DataDocument::array_push_string(
    std::string_view path,
    std::string_view value) const {
  if (!valid() || api_->data->array_push_string == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_string(
      api_->data_ctx,
      handle_,
      view(path),
      view(value))};
}

Result DataDocument::array_push_object(std::string_view path) const {
  if (!valid() || api_->data->array_push_object == nullptr) {
    return Result::unsupported("data API is unavailable");
  }
  return Result{api_->data->array_push_object(api_->data_ctx, handle_, view(path))};
}

Expected<bool, Result> DataDocument::bool_value(std::string_view path) const {
  bool value{};
  if (!valid() || api_->data->get_bool == nullptr) {
    return Expected<bool, Result>::failure(Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_bool(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<bool, Result>::success(value)
                : Expected<bool, Result>::failure(result);
}

bool DataDocument::bool_value(std::string_view path, bool fallback) const {
  auto result = bool_value(path);
  return result ? result.value() : fallback;
}

Expected<std::int64_t, Result> DataDocument::i64_value(std::string_view path) const {
  std::int64_t value{};
  if (!valid() || api_->data->get_i64 == nullptr) {
    return Expected<std::int64_t, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_i64(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<std::int64_t, Result>::success(value)
                : Expected<std::int64_t, Result>::failure(result);
}

std::int64_t DataDocument::i64_value(std::string_view path, std::int64_t fallback) const {
  auto result = i64_value(path);
  return result ? result.value() : fallback;
}

Expected<std::uint64_t, Result> DataDocument::u64_value(std::string_view path) const {
  std::uint64_t value{};
  if (!valid() || api_->data->get_u64 == nullptr) {
    return Expected<std::uint64_t, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_u64(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<std::uint64_t, Result>::success(value)
                : Expected<std::uint64_t, Result>::failure(result);
}

std::uint64_t DataDocument::u64_value(
    std::string_view path,
    std::uint64_t fallback) const {
  auto result = u64_value(path);
  return result ? result.value() : fallback;
}

Expected<double, Result> DataDocument::f64_value(std::string_view path) const {
  double value{};
  if (!valid() || api_->data->get_f64 == nullptr) {
    return Expected<double, Result>::failure(Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_f64(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<double, Result>::success(value)
                : Expected<double, Result>::failure(result);
}

double DataDocument::f64_value(std::string_view path, double fallback) const {
  auto result = f64_value(path);
  return result ? result.value() : fallback;
}

Expected<std::string_view, Result> DataDocument::string_value(std::string_view path) const {
  ArvStringView value{};
  if (!valid() || api_->data->get_string == nullptr) {
    return Expected<std::string_view, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_string(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<std::string_view, Result>::success(view(value))
                : Expected<std::string_view, Result>::failure(result);
}

std::string_view DataDocument::string_value(
    std::string_view path,
    std::string_view fallback) const {
  auto result = string_value(path);
  return result ? result.value() : fallback;
}

Expected<ArvBytesView, Result> DataDocument::bytes_value(std::string_view path) const {
  ArvBytesView value{};
  if (!valid() || api_->data->get_bytes == nullptr) {
    return Expected<ArvBytesView, Result>::failure(
        Result::unsupported("data API is unavailable"));
  }
  Result result{api_->data->get_bytes(api_->data_ctx, handle_, view(path), &value)};
  return result ? Expected<ArvBytesView, Result>::success(value)
                : Expected<ArvBytesView, Result>::failure(result);
}

ArvBytesView DataDocument::bytes_value(
    std::string_view path,
    ArvBytesView fallback) const {
  auto result = bytes_value(path);
  return result ? result.value() : fallback;
}

Data::Data(const ArvAPI* api) noexcept : api_(api) {
}

ValueResult<DataDocument> Data::create_document(std::uint32_t schema_version) const {
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

} // namespace arv
