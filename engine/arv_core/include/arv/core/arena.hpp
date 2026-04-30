#pragma once

#include "arv/core/types.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>

namespace arv {

class Arena {
public:
  Arena() = default;

  explicit Arena(usize capacity) {
    reset_storage(capacity);
  }

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  Arena(Arena&&) noexcept = default;
  Arena& operator=(Arena&&) noexcept = default;

  void reset_storage(usize capacity) {
    storage_ = std::make_unique<std::byte[]>(capacity);
    capacity_ = capacity;
    offset_ = 0;
  }

  void reset() noexcept {
    offset_ = 0;
  }

  [[nodiscard]] void* allocate(usize bytes, usize alignment = alignof(std::max_align_t)) noexcept {
    assert(alignment != 0U && (alignment & (alignment - 1U)) == 0U);
    const usize aligned_offset = (offset_ + alignment - 1U) & ~(alignment - 1U);
    if (aligned_offset > capacity_ || bytes > capacity_ - aligned_offset) {
      return nullptr;
    }

    void* ptr = storage_.get() + aligned_offset;
    offset_ = aligned_offset + bytes;
    return ptr;
  }

  template<typename T>
  [[nodiscard]] T* allocate(usize count = 1U) noexcept {
    static_assert(std::is_trivially_destructible_v<T>);
    return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
  }

  [[nodiscard]] usize capacity() const noexcept {
    return capacity_;
  }

  [[nodiscard]] usize used() const noexcept {
    return offset_;
  }

  [[nodiscard]] usize remaining() const noexcept {
    return offset_ <= capacity_ ? capacity_ - offset_ : 0U;
  }

private:
  std::unique_ptr<std::byte[]> storage_{};
  usize capacity_{};
  usize offset_{};
};

class FrameArena {
public:
  explicit FrameArena(usize capacity = 4U * 1024U * 1024U) : arena_(capacity) {
  }

  void begin_frame() noexcept {
    arena_.reset();
  }

  [[nodiscard]] Arena& arena() noexcept {
    return arena_;
  }

  [[nodiscard]] const Arena& arena() const noexcept {
    return arena_;
  }

private:
  Arena arena_;
};

class ArenaResource final : public std::pmr::memory_resource {
public:
  explicit ArenaResource(Arena& arena) noexcept : arena_(&arena) {
  }

private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    if (void* ptr = arena_->allocate(bytes, alignment); ptr != nullptr) {
      return ptr;
    }
    throw std::bad_alloc{};
  }

  void do_deallocate(void*, std::size_t, std::size_t) override {
  }

  [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  Arena* arena_{};
};

[[nodiscard]] inline Arena& worker_thread_arena() {
  thread_local Arena arena{2U * 1024U * 1024U};
  return arena;
}

} // namespace arv
