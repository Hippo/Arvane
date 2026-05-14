#pragma once

#include <arv/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace arv_mason {

struct ClipboardBlock {
  std::uint32_t runtime_variant{};
  std::string stable_id{};
};

struct MasonClipboard {
  ArvBlockPos size{};
  ArvBlockPos source_min{};
  ArvBlockPos origin{};
  std::vector<ClipboardBlock> blocks{};

  [[nodiscard]] std::uint64_t block_count() const noexcept {
    return static_cast<std::uint64_t>(blocks.size());
  }

  [[nodiscard]] bool empty() const noexcept {
    return blocks.empty();
  }
};

[[nodiscard]] std::uint64_t clipboard_index(
    ArvBlockPos size,
    std::uint64_t x,
    std::uint64_t y,
    std::uint64_t z) noexcept;

[[nodiscard]] ArvBlockPos add_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept;
[[nodiscard]] ArvBlockPos sub_pos(ArvBlockPos lhs, ArvBlockPos rhs) noexcept;

} // namespace arv_mason
