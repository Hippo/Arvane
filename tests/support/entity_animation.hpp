#pragma once

#include "arv/entity/components.hpp"

#include <string_view>

namespace arv::testsupport {

inline const entity::EntityAnimationPlayback* find_active_animation(
    const entity::EntityAnimationState& animation,
    std::string_view name) {
  for (const entity::EntityAnimationPlayback& active : animation.active) {
    if (active.name == name) {
      return &active;
    }
  }
  return nullptr;
}

inline entity::EntityAnimationPlayback* find_active_animation(
    entity::EntityAnimationState& animation,
    std::string_view name) {
  for (entity::EntityAnimationPlayback& active : animation.active) {
    if (active.name == name) {
      return &active;
    }
  }
  return nullptr;
}

} // namespace arv::testsupport
