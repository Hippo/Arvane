#pragma once

#include "arv/result.h"

namespace arv {

enum class ResultCode {
  Ok = ARV_RESULT_OK,
  InvalidArgument = ARV_RESULT_INVALID_ARGUMENT,
  InvalidState = ARV_RESULT_INVALID_STATE,
  PermissionDenied = ARV_RESULT_PERMISSION_DENIED,
  NotFound = ARV_RESULT_NOT_FOUND,
  DuplicateID = ARV_RESULT_DUPLICATE_ID,
  UnsupportedVersion = ARV_RESULT_UNSUPPORTED_VERSION,
  ThreadViolation = ARV_RESULT_THREAD_VIOLATION,
  Unsupported = ARV_RESULT_UNSUPPORTED,
  InternalError = ARV_RESULT_INTERNAL_ERROR,
};

class Result {
public:
  Result() noexcept;
  Result(ResultCode code, const char* message = "") noexcept;
  explicit Result(ArvResult result) noexcept;

  [[nodiscard]] static Result ok() noexcept;

  [[nodiscard]] static Result unsupported(const char* message) noexcept;

  [[nodiscard]] bool ok_value() const noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;

  [[nodiscard]] ResultCode code() const noexcept;

  [[nodiscard]] const char* message() const noexcept;

  [[nodiscard]] ArvResult abi() const noexcept;

private:
  ArvResult abi_{ARV_RESULT_OK, ""};
};

template<typename T>
struct ValueResult {
  Result result{};
  T value{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result);
  }
};

} // namespace arv
