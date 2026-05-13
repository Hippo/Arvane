#pragma once

#include "arv/api.h"
#include "arv/buffer.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Config {
public:
  explicit constexpr Config(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] BufferResult load(std::string_view id) const {
    if (api_ == nullptr || api_->config == nullptr ||
        api_->config->load_plugin_config == nullptr) {
      return {Result::unsupported("config API is unavailable"), {}};
    }
    ArvByteBuffer buffer{};
    Result result{api_->config->load_plugin_config(api_->config_ctx, view(id), &buffer)};
    return {result, Buffer{api_, buffer}};
  }

  [[nodiscard]] Result save(std::string_view id, std::string_view text) const {
    return save(id, bytes(text));
  }

  [[nodiscard]] Result save(std::string_view id, ArvBytesView data) const {
    if (api_ == nullptr || api_->config == nullptr ||
        api_->config->save_plugin_config == nullptr) {
      return Result::unsupported("config API is unavailable");
    }
    return Result{api_->config->save_plugin_config(api_->config_ctx, view(id), data)};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
