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
  Buffer() noexcept;
  Buffer(const ArvAPI* api, ArvByteBuffer buffer) noexcept;

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  Buffer(Buffer&& other) noexcept;

  Buffer& operator=(Buffer&& other) noexcept;

  ~Buffer();

  void reset() noexcept;

  [[nodiscard]] bool empty() const noexcept;

  [[nodiscard]] std::span<const std::byte> data() const noexcept;

  [[nodiscard]] std::string_view text() const noexcept;

  [[nodiscard]] const ArvByteBuffer& abi() const noexcept;

private:
  const ArvAPI* api_{};
  ArvByteBuffer buffer_{};
};

struct BufferResult {
  Result result{};
  Buffer buffer{};

  [[nodiscard]] explicit operator bool() const noexcept;
};

} // namespace arv
