#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

namespace arv {

class Text {
public:
  explicit constexpr Text(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->text == nullptr ||
        api_->text->unsupported == nullptr) {
      return Result::unsupported("text API is unavailable");
    }
    return Result{api_->text->unsupported(api_->text_ctx)};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
