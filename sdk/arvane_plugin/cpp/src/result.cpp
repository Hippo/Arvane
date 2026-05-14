#include "arv/result.hpp"

namespace arv {

Result::Result() noexcept = default;

Result::Result(ResultCode code, const char* message) noexcept
    : abi_{static_cast<ArvResultCode>(code), message} {
}

Result::Result(ArvResult result) noexcept : abi_(result) {
}

Result Result::ok() noexcept {
  return Result{ResultCode::Ok};
}

Result Result::unsupported(const char* message) noexcept {
  return Result{ResultCode::Unsupported, message};
}

bool Result::ok_value() const noexcept {
  return abi_.code == ARV_RESULT_OK;
}

Result::operator bool() const noexcept {
  return ok_value();
}

ResultCode Result::code() const noexcept {
  return static_cast<ResultCode>(abi_.code);
}

const char* Result::message() const noexcept {
  return abi_.message == nullptr ? "" : abi_.message;
}

ArvResult Result::abi() const noexcept {
  return abi_;
}

} // namespace arv
