#pragma once

#include "arv_mason/clipboard/clipboard.hpp"

#include <arv/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace arv_mason {

inline constexpr const char* blueprint_format_id = "arvane:blueprint";
inline constexpr const char* blueprint_created_by = "arv_mason";

struct BlueprintMetadata {
  std::string name{};
  std::string author{"unknown"};
  std::string created_by{blueprint_created_by};
  std::vector<std::string> tags{};
};

struct BlueprintRleRun {
  std::uint32_t state{};
  std::uint64_t count{};
};

struct Blueprint {
  std::uint32_t schema_version{1};
  std::string format{blueprint_format_id};
  BlueprintMetadata metadata{};
  ArvBlockPos size{};
  ArvBlockPos origin{};
  std::vector<std::string> palette{};
  std::vector<std::uint32_t> blocks{};
};

struct BlueprintCodecResult {
  bool ok{};
  Blueprint blueprint{};
  std::string text{};
  std::string message{};
};

[[nodiscard]] bool valid_blueprint_name(std::string_view name) noexcept;
[[nodiscard]] std::string blueprint_key(std::string_view name);
[[nodiscard]] std::uint64_t blueprint_volume(ArvBlockPos size);
[[nodiscard]] Blueprint blueprint_from_clipboard(
    std::string name,
    const MasonClipboard& clipboard);
[[nodiscard]] BlueprintCodecResult encode_blueprint(const Blueprint& blueprint);
[[nodiscard]] BlueprintCodecResult decode_blueprint(std::string_view text);
[[nodiscard]] std::vector<BlueprintRleRun> encode_rle(const std::vector<std::uint32_t>& blocks);
[[nodiscard]] bool decode_rle(
    const std::vector<BlueprintRleRun>& runs,
    std::uint64_t expected_count,
    std::uint32_t palette_size,
    std::vector<std::uint32_t>& out_blocks,
    std::string& out_error);

} // namespace arv_mason
