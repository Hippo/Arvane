#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

namespace arv {

class Buffer {
public:
  Buffer() = default;
  Buffer(const ArvAPI* api, ArvByteBuffer buffer) noexcept
      : api_(api), buffer_(buffer) {
  }

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  Buffer(Buffer&& other) noexcept
      : api_(std::exchange(other.api_, nullptr)),
        buffer_(std::exchange(other.buffer_, {})) {
  }

  Buffer& operator=(Buffer&& other) noexcept {
    if (this != &other) {
      reset();
      api_ = std::exchange(other.api_, nullptr);
      buffer_ = std::exchange(other.buffer_, {});
    }
    return *this;
  }

  ~Buffer() {
    reset();
  }

  void reset() noexcept {
    if (buffer_.data == nullptr) {
      return;
    }
    if (api_ != nullptr && api_->resources != nullptr &&
        api_->resources->release_buffer != nullptr) {
      api_->resources->release_buffer(api_->resources_ctx, buffer_);
    }
    api_ = nullptr;
    buffer_ = {};
  }

  [[nodiscard]] bool empty() const noexcept {
    return buffer_.size == 0;
  }

  [[nodiscard]] std::span<const std::byte> data() const noexcept {
    return {
        reinterpret_cast<const std::byte*>(buffer_.data),
        buffer_.size,
    };
  }

  [[nodiscard]] std::string_view text() const noexcept {
    return {
        reinterpret_cast<const char*>(buffer_.data),
        buffer_.size,
    };
  }

  [[nodiscard]] const ArvByteBuffer& abi() const noexcept {
    return buffer_;
  }

private:
  const ArvAPI* api_{};
  ArvByteBuffer buffer_{};
};

struct BufferResult {
  Result result{};
  Buffer buffer{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(result);
  }
};

} // namespace arv
