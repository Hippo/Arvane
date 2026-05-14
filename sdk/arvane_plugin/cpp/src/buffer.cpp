#include "arv/buffer.hpp"

#include <utility>

namespace arv {

Buffer::Buffer() noexcept = default;

Buffer::Buffer(const ArvAPI* api, ArvByteBuffer buffer) noexcept
    : api_(api), buffer_(buffer) {
}

Buffer::Buffer(Buffer&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      buffer_(std::exchange(other.buffer_, {})) {
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
  if (this != &other) {
    reset();
    api_ = std::exchange(other.api_, nullptr);
    buffer_ = std::exchange(other.buffer_, {});
  }
  return *this;
}

Buffer::~Buffer() {
  reset();
}

void Buffer::reset() noexcept {
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

bool Buffer::empty() const noexcept {
  return buffer_.size == 0;
}

std::span<const std::byte> Buffer::data() const noexcept {
  return {
      reinterpret_cast<const std::byte*>(buffer_.data),
      buffer_.size,
  };
}

std::string_view Buffer::text() const noexcept {
  return {
      reinterpret_cast<const char*>(buffer_.data),
      buffer_.size,
  };
}

const ArvByteBuffer& Buffer::abi() const noexcept {
  return buffer_;
}

BufferResult::operator bool() const noexcept {
  return static_cast<bool>(result);
}

} // namespace arv
