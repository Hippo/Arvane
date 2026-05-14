#pragma once

#include "arv_mason/clipboard/clipboard.hpp"
#include "arv_mason/selection/selection.hpp"

#include <arv/types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arv_mason {

struct BlockChange {
  ArvBlockPos pos{};
  std::uint32_t before{};
  std::uint32_t after{};
};

struct OperationPlan {
  ArvWorldHandle world{};
  std::vector<BlockChange> changes{};
  std::string summary{};
};

struct SnapshotBlock {
  ArvBlockPos pos{};
  std::uint32_t runtime_variant{};
};

[[nodiscard]] OperationPlan build_set_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& snapshot,
    std::uint32_t to_variant);

[[nodiscard]] OperationPlan build_replace_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& snapshot,
    std::uint32_t from_variant,
    std::uint32_t to_variant);

[[nodiscard]] OperationPlan build_paste_plan_from_snapshot(
    ArvWorldHandle world,
    const std::vector<SnapshotBlock>& destination_snapshot,
    const MasonClipboard& clipboard,
    ArvBlockPos paste_origin,
    std::uint32_t air_variant,
    bool paste_air);

void for_each_position(
    const SelectionBounds& bounds,
    const std::function<void(ArvBlockPos)>& visitor);

} // namespace arv_mason
