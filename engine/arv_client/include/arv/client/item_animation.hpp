#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/item.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

inline constexpr std::string_view HeldItemActionPrimaryUse = "primary_use";
inline constexpr std::string_view HeldItemActionSecondaryUseStart = "secondary_use_start";
inline constexpr std::string_view HeldItemActionSecondaryUseHold = "secondary_use_hold";
inline constexpr std::string_view HeldItemActionSecondaryUseEnd = "secondary_use_end";
inline constexpr std::string_view DefaultHeldItemSwingAnimationName = "arvane:swing/default";

enum class ItemAnimationEasing : u8 {
  Linear,
  Step,
  EaseIn,
  EaseOut,
  EaseInOut,
};

struct ItemAnimationError {
  std::string message{};
};

struct ItemAnimationKeyframeVec3 {
  Seconds time_seconds{};
  Vec3f value{};
  ItemAnimationEasing easing{ItemAnimationEasing::Linear};
};

struct ItemAnimationTransformChannel {
  std::vector<ItemAnimationKeyframeVec3> position{};
  std::vector<ItemAnimationKeyframeVec3> rotation_degrees{};
  std::vector<ItemAnimationKeyframeVec3> scale{};
};

struct ItemAnimationClip {
  std::string name{};
  Seconds duration_seconds{};
  bool loop{};
  Seconds blend_in_seconds{};
  Seconds blend_out_seconds{};
  ItemAnimationTransformChannel first_person{};
};

struct HeldItemAnimationPose {
  Vec3f position{};
  Vec3f rotation_degrees{};
  Vec3f scale{1.0F, 1.0F, 1.0F};
};

struct HeldItemAnimationResolution {
  const ItemAnimationClip* clip{};
  player::HeldItemAnimationPlayCondition play_condition{
      player::HeldItemAnimationPlayCondition::Always};
  bool disabled{};
};

enum class HeldItemActionPlayResult : u8 {
  NoAnimation,
  Suppressed,
  Played,
};

struct HeldItemAnimationConditionState {
  bool placement_succeeds{};
};

class ItemAnimationLibrary {
public:
  using Result = Expected<ItemAnimationLibrary, ItemAnimationError>;

  [[nodiscard]] static Result build(std::vector<ItemAnimationClip> clips);

  [[nodiscard]] const ItemAnimationClip* find(std::string_view name) const noexcept;
  [[nodiscard]] ItemAnimationClip* find(std::string_view name) noexcept;
  [[nodiscard]] std::span<ItemAnimationClip> clips() noexcept {
    return clips_;
  }
  [[nodiscard]] std::span<const ItemAnimationClip> clips() const noexcept {
    return clips_;
  }
  [[nodiscard]] usize clip_count() const noexcept {
    return clips_.size();
  }

private:
  std::vector<ItemAnimationClip> clips_{};
  HashMap<std::string, usize> clip_by_name_{};
};

class HeldItemAnimationPlayer {
public:
  void play(const ItemAnimationClip& clip, Seconds now_seconds) noexcept;
  void stop(Seconds now_seconds) noexcept;

  [[nodiscard]] HeldItemAnimationPose sample(Seconds now_seconds) const noexcept;
  [[nodiscard]] bool finished(Seconds now_seconds) const noexcept;
  [[nodiscard]] const ItemAnimationClip* active_clip() const noexcept {
    return active_clip_;
  }

private:
  const ItemAnimationClip* active_clip_{};
  Seconds active_start_seconds_{};
  HeldItemAnimationPose blend_from_pose_{};
  Seconds blend_duration_seconds_{};
  bool fading_out_{};
  Seconds fade_start_seconds_{};
  Seconds fade_duration_seconds_{};
  HeldItemAnimationPose fade_from_pose_{};
};

[[nodiscard]] Expected<ItemAnimationLibrary, ItemAnimationError>
parse_item_animation_library_json(std::string_view text);

[[nodiscard]] Expected<void, ItemAnimationError> validate_item_animation_references(
    const player::ItemRegistry& item_registry,
    const ItemAnimationLibrary& library);

[[nodiscard]] const ItemAnimationClip* resolve_held_item_animation(
    const player::ItemRecord* item,
    std::string_view action,
    const ItemAnimationLibrary& library) noexcept;

[[nodiscard]] HeldItemAnimationResolution resolve_held_item_animation_binding(
    const player::ItemRecord* item,
    std::string_view action,
    const ItemAnimationLibrary& library) noexcept;

[[nodiscard]] bool held_item_animation_condition_passes(
    player::HeldItemAnimationPlayCondition condition,
    HeldItemAnimationConditionState state) noexcept;

[[nodiscard]] HeldItemActionPlayResult play_held_item_action(
    HeldItemAnimationPlayer& player,
    const player::ItemRecord* item,
    std::string_view action,
    const ItemAnimationLibrary& library,
    HeldItemAnimationConditionState condition_state,
    Seconds now_seconds) noexcept;

[[nodiscard]] const player::ItemRecord* selected_inventory_item(
    const player::Inventory& inventory,
    usize selected_slot,
    const player::ItemRegistry& item_registry) noexcept;

} // namespace arv::client
