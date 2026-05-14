#include "arv_mason/operations/operation.hpp"

#include <sstream>
#include <unordered_map>

namespace arv_mason {
namespace {

[[nodiscard]] std::string changed_summary(std::string_view kind, std::size_t count) {
  std::ostringstream out;
  out << kind << " " << count << " blocks";
  return out.str();
}

} // namespace

OperationPlan build_set_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& snapshot,
    std::uint32_t to_variant) {
  OperationPlan plan;
  plan.world = world;
  for (const SnapshotBlock& block : snapshot) {
    if (block.runtime_variant == to_variant) {
      continue;
    }
    plan.changes.push_back({block.pos, block.runtime_variant, to_variant});
  }
  plan.summary = changed_summary("set", plan.changes.size());
  return plan;
}

OperationPlan build_replace_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& snapshot,
    std::uint32_t from_variant,
    std::uint32_t to_variant) {
  OperationPlan plan;
  plan.world = world;
  for (const SnapshotBlock& block : snapshot) {
    if (block.runtime_variant != from_variant || block.runtime_variant == to_variant) {
      continue;
    }
    plan.changes.push_back({block.pos, block.runtime_variant, to_variant});
  }
  plan.summary = changed_summary("replace", plan.changes.size());
  return plan;
}

OperationPlan build_paste_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& destination_snapshot,
    const MasonClipboard& clipboard,
    ArvBlockPos paste_origin,
    std::uint32_t air_variant,
    bool paste_air) {
  std::unordered_map<std::string, std::uint32_t> before_by_pos;
  before_by_pos.reserve(destination_snapshot.size());
  for (const SnapshotBlock& block : destination_snapshot) {
    before_by_pos.emplace(format_pos(block.pos), block.runtime_variant);
  }

  OperationPlan plan;
  plan.world = world;
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
        if (!paste_air && block.runtime_variant == air_variant) {
          continue;
        }

        const ArvBlockPos source_pos{
            clipboard.source_min.x + x,
            clipboard.source_min.y + y,
            clipboard.source_min.z + z,
        };
        const ArvBlockPos relative = sub_pos(source_pos, clipboard.origin);
        const ArvBlockPos dest = add_pos(paste_origin, relative);
        const auto found = before_by_pos.find(format_pos(dest));
        if (found == before_by_pos.end() || found->second == block.runtime_variant) {
          continue;
        }
        plan.changes.push_back({dest, found->second, block.runtime_variant});
      }
    }
  }
  plan.summary = changed_summary("paste", plan.changes.size());
  return plan;
}

void for_each_position(
    const SelectionBounds& bounds,
    const std::function<void(ArvBlockPos)>& visitor) {
  for (std::int32_t y = bounds.min.y; y <= bounds.max.y; ++y) {
    for (std::int32_t z = bounds.min.z; z <= bounds.max.z; ++z) {
      for (std::int32_t x = bounds.min.x; x <= bounds.max.x; ++x) {
        visitor({x, y, z});
        if (x == bounds.max.x) {
          break;
        }
      }
      if (z == bounds.max.z) {
        break;
      }
    }
    if (y == bounds.max.y) {
      break;
    }
  }
}

} // namespace arv_mason
