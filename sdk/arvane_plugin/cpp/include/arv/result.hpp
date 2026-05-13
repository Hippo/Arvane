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
  constexpr Result() noexcept = default;
  constexpr Result(ResultCode code, const char* message = "") noexcept
      : abi_{static_cast<ArvResultCode>(code), message} {
  }
  constexpr explicit Result(ArvResult result) noexcept : abi_(result) {
  }

  [[nodiscard]] static constexpr Result ok() noexcept {
    return Result{ResultCode::Ok};
  }

  [[nodiscard]] static constexpr Result unsupported(const char* message) noexcept {
    return Result{ResultCode::Unsupported, message};
  }

  [[nodiscard]] constexpr bool ok_value() const noexcept {
    return abi_.code == ARV_RESULT_OK;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ok_value();
  }

  [[nodiscard]] constexpr ResultCode code() const noexcept {
    return static_cast<ResultCode>(abi_.code);
  }

  [[nodiscard]] constexpr const char* message() const noexcept {
    return abi_.message == nullptr ? "" : abi_.message;
  }

  [[nodiscard]] constexpr ArvResult abi() const noexcept {
    return abi_;
  }

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
