#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arv::blueprint {

inline constexpr const char* BlueprintFormatID = "arvane:blueprint";
inline constexpr const char* BlueprintCreatedBy = "arv_blueprint";

struct BlueprintMetadata {
  std::string name{};
  std::string author{"unknown"};
  std::string created_by{BlueprintCreatedBy};
  std::vector<std::string> tags{};
};

struct BlueprintRleRun {
  std::uint32_t state{};
  std::uint64_t count{};
};

struct Blueprint {
  std::uint32_t schema_version{1};
  std::string format{BlueprintFormatID};
  BlueprintMetadata metadata{};
  BlockPos size{};
  BlockPos origin{};
  std::vector<std::string> palette{};
  std::vector<std::uint32_t> blocks{};
};

struct BlueprintCodecResult {
  bool ok{};
  Blueprint blueprint{};
  std::string text{};
  std::string message{};
};

struct BlueprintClipboardBlock {
  std::uint32_t runtime_variant{};
  std::string stable_id{};
};

struct BlueprintClipboard {
  BlockPos size{};
  BlockPos source_min{};
  BlockPos origin{};
  std::vector<BlueprintClipboardBlock> blocks{};

  [[nodiscard]] std::uint64_t block_count() const noexcept {
    return static_cast<std::uint64_t>(blocks.size());
  }

  [[nodiscard]] bool empty() const noexcept {
    return blocks.empty();
  }
};

[[nodiscard]] bool valid_blueprint_name(std::string_view name) noexcept;
[[nodiscard]] std::string blueprint_key(
    std::string_view owner_namespace,
    std::string_view name);
[[nodiscard]] std::uint64_t blueprint_volume(BlockPos size);
[[nodiscard]] Blueprint blueprint_from_clipboard(
    std::string name,
    const BlueprintClipboard& clipboard,
    std::string created_by = std::string{BlueprintCreatedBy});
[[nodiscard]] BlueprintCodecResult encode_blueprint(const Blueprint& blueprint);
[[nodiscard]] BlueprintCodecResult decode_blueprint(std::string_view text);
[[nodiscard]] std::vector<BlueprintRleRun> encode_rle(
    const std::vector<std::uint32_t>& blocks);
[[nodiscard]] bool decode_rle(
    const std::vector<BlueprintRleRun>& runs,
    std::uint64_t expected_count,
    std::uint32_t palette_size,
    std::vector<std::uint32_t>& out_blocks,
    std::string& out_error);

} // namespace arv::blueprint
