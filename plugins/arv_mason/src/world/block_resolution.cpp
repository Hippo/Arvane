#include "arv_mason/mason_plugin.hpp"

#include "../string_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv_mason {

std::optional<std::uint32_t> MasonPlugin::parse_block_variant(
    arv::CommandContext& ctx,
    std::string_view text) const {
  if (text.empty()) {
    ctx.error("Block state must not be empty.");
    return std::nullopt;
  }
  std::vector<std::string> candidates;
  const std::string raw{text};
  append_unique_string(candidates, raw);
  if (raw.find(':') == std::string::npos) {
    append_unique_string(candidates, "arvane:" + raw);
  }
  const std::vector<std::string> base = candidates;
  for (const std::string& candidate : base) {
    if (candidate.find('[') == std::string::npos) {
      append_unique_string(candidates, candidate + "[]");
    }
  }
  for (const std::string& candidate : candidates) {
    auto resolved = plugin_.registries().find_block_variant(candidate);
    if (resolved) {
      return resolved.value;
    }
  }
  ctx.error("Unknown block state: " + raw + ".");
  return std::nullopt;
}

std::string MasonPlugin::stable_block_name(std::uint32_t variant) const {
  auto name = plugin_.registries().block_variant_name(variant);
  return name ? std::string{name.value} : std::string{"arvane:air[]"};
}

std::uint32_t MasonPlugin::air_variant() const {
  auto air = plugin_.registries().air_block_variant();
  return air ? air.value : 0U;
}

std::optional<std::vector<SnapshotBlock>> MasonPlugin::snapshot_selection(
    arv::CommandContext& ctx,
    const SelectionBounds& bounds) const {
  std::vector<SnapshotBlock> snapshot;
  snapshot.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(
      bounds.block_count,
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))));
  bool ok = true;
  std::string error;
  for_each_position(bounds, [&](ArvBlockPos pos) {
    if (!ok) {
      return;
    }
    auto block = plugin_.world().get_block(bounds.world, pos);
    if (!block) {
      ok = false;
      error = block.result.message();
      return;
    }
    snapshot.push_back({pos, block.value});
  });
  if (!ok) {
    ctx.error("Failed to read selection: " + error + ".");
    return std::nullopt;
  }
  return snapshot;
}

std::optional<std::vector<SnapshotBlock>> MasonPlugin::snapshot_clipboard_destination(
    arv::CommandContext& ctx,
    const MasonClipboard& clipboard,
    ArvWorldHandle world,
    ArvBlockPos paste_origin,
    bool paste_air) const {
  std::vector<SnapshotBlock> snapshot;
  const std::uint32_t air = air_variant();
  for (std::int32_t y = 0; y < clipboard.size.y; ++y) {
    for (std::int32_t z = 0; z < clipboard.size.z; ++z) {
      for (std::int32_t x = 0; x < clipboard.size.x; ++x) {
        const std::uint64_t index = clipboard_index(
            clipboard.size,
            static_cast<std::uint64_t>(x),
            static_cast<std::uint64_t>(y),
            static_cast<std::uint64_t>(z));
        if (index >= clipboard.blocks.size()) {
          continue;
        }
        const ClipboardBlock& block = clipboard.blocks[index];
        if (!paste_air && block.runtime_variant == air) {
          continue;
        }
        const ArvBlockPos source_pos{
            clipboard.source_min.x + x,
            clipboard.source_min.y + y,
            clipboard.source_min.z + z,
        };
        const ArvBlockPos dest = add_pos(paste_origin, sub_pos(source_pos, clipboard.origin));
        auto before = plugin_.world().get_block(world, dest);
        if (!before) {
          ctx.error("Failed to read paste destination: " + std::string{before.result.message()} + ".");
          return std::nullopt;
        }
        snapshot.push_back({dest, before.value});
      }
    }
  }
  return snapshot;
}

} // namespace arv_mason
