#pragma once

#include "arv/core/time.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/render/render_types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv::client {

inline constexpr Seconds BlockContainerAnimationLengthSeconds = 0.5;
inline constexpr std::string_view BlockContainerOpenAnimation = "open";
inline constexpr std::string_view BlockContainerCloseAnimation = "close";

struct BlockContainerAnimationState {
  std::optional<protocol::ContainerBlockSource> last_source{};
  std::optional<protocol::ContainerBlockSource> active_source{};
  std::string active_clip{};
  Seconds active_started_at{};
};

void update_block_container_animation(
    BlockContainerAnimationState& state,
    std::optional<protocol::ContainerBlockSource> current_source,
    Seconds now_seconds);

void append_block_container_animation_playback(
    render::EntityDrawItem& item,
    const BlockContainerAnimationState& state,
    protocol::ContainerBlockSource source,
    Seconds now_seconds);

} // namespace arv::client
