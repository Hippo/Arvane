#include "arv/client/block_container_animation.hpp"

#include <algorithm>

namespace arv::client {
namespace {

void start_block_container_animation(
    BlockContainerAnimationState& state,
    protocol::ContainerBlockSource source,
    std::string_view clip,
    Seconds now_seconds) {
  state.active_source = source;
  state.active_clip = std::string{clip};
  state.active_started_at = now_seconds;
}

[[nodiscard]] f32 animation_elapsed_seconds(
    Seconds now_seconds,
    Seconds started_at) noexcept {
  return static_cast<f32>(std::max<Seconds>(0.0, now_seconds - started_at));
}

} // namespace

void update_block_container_animation(
    BlockContainerAnimationState& state,
    std::optional<protocol::ContainerBlockSource> current_source,
    Seconds now_seconds) {
  if (current_source != state.last_source) {
    if (current_source.has_value()) {
      start_block_container_animation(
          state,
          current_source.value(),
          BlockContainerOpenAnimation,
          now_seconds);
    } else if (state.last_source.has_value()) {
      start_block_container_animation(
          state,
          state.last_source.value(),
          BlockContainerCloseAnimation,
          now_seconds);
    }
    state.last_source = current_source;
  }

  if (state.active_source.has_value() &&
      state.active_clip == BlockContainerCloseAnimation &&
      animation_elapsed_seconds(now_seconds, state.active_started_at) >
          BlockContainerAnimationLengthSeconds) {
    state.active_source.reset();
    state.active_clip.clear();
  }
}

void append_block_container_animation_playback(
    render::EntityDrawItem& item,
    const BlockContainerAnimationState& state,
    protocol::ContainerBlockSource source,
    Seconds now_seconds) {
  if (!state.active_source.has_value() ||
      state.active_source.value() != source ||
      state.active_clip.empty()) {
    return;
  }
  item.animations.push_back({
      state.active_clip,
      animation_elapsed_seconds(now_seconds, state.active_started_at)});
}

} // namespace arv::client
