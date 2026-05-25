#include "arv/client/item_animation.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] bool valid_stable_name(std::string_view name) noexcept {
  const usize separator = name.find(':');
  return separator != std::string_view::npos && separator > 0U &&
         separator + 1U < name.size();
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view key) noexcept {
  return object.get(key);
}

[[nodiscard]] ItemAnimationError animation_error(std::string message) {
  return {std::move(message)};
}

[[nodiscard]] Expected<ItemAnimationEasing, ItemAnimationError> parse_easing(
    std::string_view value,
    std::string_view context) {
  if (value == "linear") {
    return Expected<ItemAnimationEasing, ItemAnimationError>::success(
        ItemAnimationEasing::Linear);
  }
  if (value == "step") {
    return Expected<ItemAnimationEasing, ItemAnimationError>::success(
        ItemAnimationEasing::Step);
  }
  if (value == "ease_in") {
    return Expected<ItemAnimationEasing, ItemAnimationError>::success(
        ItemAnimationEasing::EaseIn);
  }
  if (value == "ease_out") {
    return Expected<ItemAnimationEasing, ItemAnimationError>::success(
        ItemAnimationEasing::EaseOut);
  }
  if (value == "ease_in_out") {
    return Expected<ItemAnimationEasing, ItemAnimationError>::success(
        ItemAnimationEasing::EaseInOut);
  }
  return Expected<ItemAnimationEasing, ItemAnimationError>::failure(
      animation_error("invalid easing '" + std::string{value} + "' in " +
                      std::string{context}));
}

[[nodiscard]] Expected<Vec3f, ItemAnimationError> read_vec3(
    const io::DataValue& node,
    std::string_view context) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 3U) {
    return Expected<Vec3f, ItemAnimationError>::failure(
        animation_error(std::string{context} + " must be an array of 3 numbers"));
  }
  if (!io::is_number_value((*array)[0]) || !io::is_number_value((*array)[1]) ||
      !io::is_number_value((*array)[2])) {
    return Expected<Vec3f, ItemAnimationError>::failure(
        animation_error(std::string{context} + " must be an array of 3 numbers"));
  }
  Vec3f value{
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0))};
  if (!finite(value)) {
    return Expected<Vec3f, ItemAnimationError>::failure(
        animation_error(std::string{context} + " values must be finite"));
  }
  return Expected<Vec3f, ItemAnimationError>::success(value);
}

[[nodiscard]] Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>
read_keyframes(
    const io::DataValue& node,
    std::string_view clip_name,
    std::string_view field) {
  const io::DataValue::Array* frame_array = node.array_if();
  if (frame_array == nullptr) {
    return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
        animation_error(
            "animation " + std::string{clip_name} + " channel " + std::string{field} +
            " must be an array"));
  }

  std::vector<ItemAnimationKeyframeVec3> frames;
  frames.reserve(frame_array->size());
  for (usize index = 0; index < frame_array->size(); ++index) {
    const io::DataValue& frame_json = (*frame_array)[index];
    const std::string context =
        "animation " + std::string{clip_name} + " " + std::string{field} +
        " keyframe " + std::to_string(index);
    if (frame_json.kind() != io::DataKind::Object) {
      return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
          animation_error(context + " must be an object"));
    }
    const io::DataValue* time_iter = member(frame_json, "time");
    const io::DataValue* value_iter = member(frame_json, "value");
    if (time_iter == nullptr || value_iter == nullptr) {
      return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
          animation_error(context + " requires time and value"));
    }
    if (!io::is_number_value(*time_iter)) {
      return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
          animation_error(context + " time must be numeric"));
    }

    const Seconds time_seconds = static_cast<Seconds>(time_iter->as_f64_or(0.0));
    if (!std::isfinite(time_seconds) || time_seconds < 0.0F) {
      return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
          animation_error(context + " time must be finite and non-negative"));
    }
    auto value = read_vec3(*value_iter, context + " value");
    if (!value) {
      return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
          value.error());
    }

    ItemAnimationEasing easing = ItemAnimationEasing::Linear;
    const io::DataValue* easing_iter = member(frame_json, "easing");
    if (easing_iter != nullptr) {
      auto easing_text = io::string_value_if(*easing_iter);
      if (!easing_text.has_value()) {
        return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
            animation_error(context + " easing must be a string"));
      }
      auto parsed_easing = parse_easing(easing_text.value(), context);
      if (!parsed_easing) {
        return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::failure(
            parsed_easing.error());
      }
      easing = parsed_easing.value();
    }

    frames.push_back({time_seconds, value.value(), easing});
  }

  std::sort(
      frames.begin(),
      frames.end(),
      [](const ItemAnimationKeyframeVec3& lhs, const ItemAnimationKeyframeVec3& rhs) {
        return lhs.time_seconds < rhs.time_seconds;
      });
  return Expected<std::vector<ItemAnimationKeyframeVec3>, ItemAnimationError>::success(
      std::move(frames));
}

[[nodiscard]] Expected<ItemAnimationTransformChannel, ItemAnimationError> read_transform_channel(
    const io::DataValue& node,
    std::string_view clip_name,
    std::string_view channel_name) {
  if (node.kind() != io::DataKind::Object) {
    return Expected<ItemAnimationTransformChannel, ItemAnimationError>::failure(
        animation_error(
            "animation " + std::string{clip_name} + " channel " +
            std::string{channel_name} + " must be an object"));
  }

  ItemAnimationTransformChannel channel;
  for (const auto [field, target] : {
           std::pair{std::string_view{"position"}, &channel.position},
           std::pair{std::string_view{"rotation_degrees"}, &channel.rotation_degrees},
           std::pair{std::string_view{"scale"}, &channel.scale},
       }) {
    const io::DataValue* iter = member(node, field);
    if (iter == nullptr) {
      continue;
    }
    auto frames = read_keyframes(*iter, clip_name, field);
    if (!frames) {
      return Expected<ItemAnimationTransformChannel, ItemAnimationError>::failure(
          frames.error());
    }
    *target = std::move(frames.value());
  }
  return Expected<ItemAnimationTransformChannel, ItemAnimationError>::success(
      std::move(channel));
}

[[nodiscard]] f32 apply_easing(ItemAnimationEasing easing, f32 t) noexcept {
  t = std::clamp(t, 0.0F, 1.0F);
  switch (easing) {
    case ItemAnimationEasing::Linear:
      return t;
    case ItemAnimationEasing::Step:
      return 0.0F;
    case ItemAnimationEasing::EaseIn:
      return t * t;
    case ItemAnimationEasing::EaseOut:
      return 1.0F - (1.0F - t) * (1.0F - t);
    case ItemAnimationEasing::EaseInOut:
      return t * t * (3.0F - 2.0F * t);
  }
  return t;
}

[[nodiscard]] HeldItemAnimationPose lerp_pose(
    HeldItemAnimationPose from,
    HeldItemAnimationPose to,
    f32 alpha) noexcept {
  return {
      lerp(from.position, to.position, alpha),
      lerp(from.rotation_degrees, to.rotation_degrees, alpha),
      lerp(from.scale, to.scale, alpha)};
}

[[nodiscard]] Vec3f sample_vec3_keyframes(
    const std::vector<ItemAnimationKeyframeVec3>& frames,
    Seconds time_seconds,
    Vec3f default_value) noexcept {
  if (frames.empty()) {
    return default_value;
  }
  if (time_seconds <= frames.front().time_seconds) {
    if (frames.front().time_seconds <= 0.0F) {
      return frames.front().value;
    }
    const f32 alpha = std::clamp(time_seconds / frames.front().time_seconds, 0.0F, 1.0F);
    return lerp(default_value, frames.front().value, alpha);
  }

  const ItemAnimationKeyframeVec3* previous = &frames.front();
  for (usize index = 1; index < frames.size(); ++index) {
    const ItemAnimationKeyframeVec3& next = frames[index];
    if (time_seconds <= next.time_seconds) {
      const Seconds span = next.time_seconds - previous->time_seconds;
      if (span <= 0.0F) {
        return next.value;
      }
      const f32 alpha =
          apply_easing(previous->easing, (time_seconds - previous->time_seconds) / span);
      return lerp(previous->value, next.value, alpha);
    }
    previous = &next;
  }
  return frames.back().value;
}

[[nodiscard]] HeldItemAnimationPose sample_clip_pose(
    const ItemAnimationClip& clip,
    Seconds elapsed_seconds) noexcept {
  Seconds sample_time = std::max(elapsed_seconds, 0.0F);
  if (clip.loop && clip.duration_seconds > 0.0F) {
    sample_time = std::fmod(sample_time, clip.duration_seconds);
  } else {
    sample_time = std::min(sample_time, clip.duration_seconds);
  }

  return {
      sample_vec3_keyframes(clip.first_person.position, sample_time, {}),
      sample_vec3_keyframes(clip.first_person.rotation_degrees, sample_time, {}),
      sample_vec3_keyframes(clip.first_person.scale, sample_time, {1.0F, 1.0F, 1.0F})};
}

} // namespace

ItemAnimationLibrary::Result ItemAnimationLibrary::build(std::vector<ItemAnimationClip> clips) {
  ItemAnimationLibrary library;
  library.clips_.reserve(clips.size());
  for (ItemAnimationClip& clip : clips) {
    if (!valid_stable_name(clip.name)) {
      return Result::failure(
          animation_error("item animation name must be namespaced: " + clip.name));
    }
    if (!std::isfinite(clip.duration_seconds) || clip.duration_seconds <= 0.0F) {
      return Result::failure(
          animation_error("item animation " + clip.name + " duration_seconds must be positive"));
    }
    if (!std::isfinite(clip.blend_in_seconds) || clip.blend_in_seconds < 0.0F ||
        !std::isfinite(clip.blend_out_seconds) || clip.blend_out_seconds < 0.0F) {
      return Result::failure(
          animation_error("item animation " + clip.name + " blend durations must be non-negative"));
    }
    if (!library.clip_by_name_.emplace(clip.name, library.clips_.size()).second) {
      return Result::failure(animation_error("duplicate item animation: " + clip.name));
    }
    library.clips_.push_back(std::move(clip));
  }
  return Result::success(std::move(library));
}

const ItemAnimationClip* ItemAnimationLibrary::find(std::string_view name) const noexcept {
  const auto found = clip_by_name_.find(std::string{name});
  if (found == clip_by_name_.end()) {
    return nullptr;
  }
  return &clips_[found->second];
}

ItemAnimationClip* ItemAnimationLibrary::find(std::string_view name) noexcept {
  const auto found = clip_by_name_.find(std::string{name});
  if (found == clip_by_name_.end()) {
    return nullptr;
  }
  return &clips_[found->second];
}

void HeldItemAnimationPlayer::play(
    const ItemAnimationClip& clip,
    Seconds now_seconds) noexcept {
  const HeldItemAnimationPose current_pose = sample(now_seconds);
  active_clip_ = &clip;
  active_start_seconds_ = now_seconds;
  blend_from_pose_ = current_pose;
  blend_duration_seconds_ = clip.blend_in_seconds;
  fading_out_ = false;
}

void HeldItemAnimationPlayer::stop(Seconds now_seconds) noexcept {
  if (active_clip_ == nullptr && !fading_out_) {
    return;
  }
  fade_from_pose_ = sample(now_seconds);
  fade_start_seconds_ = now_seconds;
  fade_duration_seconds_ = active_clip_ != nullptr ? active_clip_->blend_out_seconds : 0.0F;
  active_clip_ = nullptr;
  blend_duration_seconds_ = 0.0F;
  fading_out_ = fade_duration_seconds_ > 0.0F;
}

HeldItemAnimationPose HeldItemAnimationPlayer::sample(Seconds now_seconds) const noexcept {
  if (active_clip_ != nullptr) {
    const Seconds elapsed = std::max(now_seconds - active_start_seconds_, 0.0F);
    HeldItemAnimationPose pose = sample_clip_pose(*active_clip_, elapsed);
    if (blend_duration_seconds_ > 0.0F && elapsed < blend_duration_seconds_) {
      pose = lerp_pose(
          blend_from_pose_,
          pose,
          std::clamp(elapsed / blend_duration_seconds_, 0.0F, 1.0F));
    }
    return pose;
  }

  if (fading_out_) {
    const Seconds elapsed = std::max(now_seconds - fade_start_seconds_, 0.0F);
    if (fade_duration_seconds_ <= 0.0F || elapsed >= fade_duration_seconds_) {
      return {};
    }
    return lerp_pose(
        fade_from_pose_,
        {},
        std::clamp(elapsed / fade_duration_seconds_, 0.0F, 1.0F));
  }
  return {};
}

bool HeldItemAnimationPlayer::finished(Seconds now_seconds) const noexcept {
  if (active_clip_ == nullptr) {
    return true;
  }
  return !active_clip_->loop &&
         std::max(now_seconds - active_start_seconds_, 0.0F) >= active_clip_->duration_seconds;
}

Expected<ItemAnimationLibrary, ItemAnimationError> parse_item_animation_library_json(
    std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
        animation_error("failed to parse item animations JSON: " + decoded.error().describe()));
  }
  const io::DataValue& root = decoded.value().root;
  if (root.kind() != io::DataKind::Object) {
    return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
        animation_error("item animation file root must be an object"));
  }
  const io::DataValue* animations_iter = member(root, "animations");
  const io::DataValue::Array* animation_array =
      animations_iter == nullptr ? nullptr : animations_iter->array_if();
  if (animation_array == nullptr) {
    return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
        animation_error("item animation file must contain animations array"));
  }

  std::vector<ItemAnimationClip> clips;
  clips.reserve(animation_array->size());
  for (usize index = 0; index < animation_array->size(); ++index) {
    const io::DataValue& animation_json = (*animation_array)[index];
    if (animation_json.kind() != io::DataKind::Object) {
      return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
          animation_error("item animation entry " + std::to_string(index) + " must be an object"));
    }
    ItemAnimationClip clip;
    const io::DataValue* name_iter = member(animation_json, "name");
    auto name_text = name_iter == nullptr ? std::optional<std::string_view>{}
                                          : io::string_value_if(*name_iter);
    if (!name_text.has_value()) {
      return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
          animation_error("item animation entry " + std::to_string(index) + " is missing name"));
    }
    clip.name = std::string{name_text.value()};
    if (const io::DataValue* duration = member(animation_json, "duration_seconds");
        duration != nullptr && io::is_number_value(*duration)) {
      clip.duration_seconds = static_cast<Seconds>(duration->as_f64_or(0.0));
    }
    if (const io::DataValue* loop = member(animation_json, "loop");
        loop != nullptr && loop->kind() == io::DataKind::Bool) {
      clip.loop = loop->as_bool_or(false);
    }
    if (const io::DataValue* blend_in = member(animation_json, "blend_in_seconds");
        blend_in != nullptr && io::is_number_value(*blend_in)) {
      clip.blend_in_seconds = static_cast<Seconds>(blend_in->as_f64_or(0.0));
    }
    if (const io::DataValue* blend_out = member(animation_json, "blend_out_seconds");
        blend_out != nullptr && io::is_number_value(*blend_out)) {
      clip.blend_out_seconds = static_cast<Seconds>(blend_out->as_f64_or(0.0));
    }

    const io::DataValue* channels_iter = member(animation_json, "channels");
    if (channels_iter != nullptr) {
      if (channels_iter->kind() != io::DataKind::Object) {
        return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(
            animation_error("animation " + clip.name + " channels must be an object"));
      }
      const io::DataValue* first_person_iter = member(*channels_iter, "first_person");
      if (first_person_iter != nullptr) {
        auto channel = read_transform_channel(*first_person_iter, clip.name, "first_person");
        if (!channel) {
          return Expected<ItemAnimationLibrary, ItemAnimationError>::failure(channel.error());
        }
        clip.first_person = std::move(channel.value());
      }
    }

    clips.push_back(std::move(clip));
  }

  return ItemAnimationLibrary::build(std::move(clips));
}

Expected<void, ItemAnimationError> validate_item_animation_references(
    const player::ItemRegistry& item_registry,
    const ItemAnimationLibrary& library) {
  if (library.find(DefaultHeldItemSwingAnimationName) == nullptr) {
    return Expected<void, ItemAnimationError>::failure(
        animation_error(
            "default held item animation is missing: " +
            std::string{DefaultHeldItemSwingAnimationName}));
  }

  for (u32 item_index = 0; item_index < item_registry.item_count(); ++item_index) {
    const player::ItemRecord* item = item_registry.item(player::ItemID{item_index});
    if (item == nullptr) {
      continue;
    }
    for (const auto& [action, animation] : item->held_animations) {
      if (animation.selection != player::HeldItemAnimationSelection::Named) {
        continue;
      }
      if (!valid_stable_name(animation.animation)) {
        return Expected<void, ItemAnimationError>::failure(
            animation_error(
                "item " + item->name + " held animation " + action +
                " must be namespaced: " + animation.animation));
      }
      if (library.find(animation.animation) == nullptr) {
        return Expected<void, ItemAnimationError>::failure(
            animation_error(
                "item " + item->name + " references missing held animation " +
                animation.animation + " for " + action));
      }
    }
  }

  return Expected<void, ItemAnimationError>::success();
}

const ItemAnimationClip* resolve_held_item_animation(
    const player::ItemRecord* item,
    std::string_view action,
    const ItemAnimationLibrary& library) noexcept {
  return resolve_held_item_animation_binding(item, action, library).clip;
}

HeldItemAnimationResolution resolve_held_item_animation_binding(
    const player::ItemRecord* item,
    std::string_view action,
    const ItemAnimationLibrary& library) noexcept {
  if (item != nullptr) {
    const auto found = item->held_animations.find(std::string{action});
    if (found != item->held_animations.end()) {
      switch (found->second.selection) {
        case player::HeldItemAnimationSelection::Default:
          return {
              library.find(DefaultHeldItemSwingAnimationName),
              found->second.play_condition};
        case player::HeldItemAnimationSelection::Named:
          return {library.find(found->second.animation), found->second.play_condition};
        case player::HeldItemAnimationSelection::Disabled:
          return {nullptr, found->second.play_condition, true};
      }
    }
  }

  if (action == HeldItemActionPrimaryUse ||
      action == HeldItemActionSecondaryUseStart) {
    player::HeldItemAnimationPlayCondition play_condition =
        player::HeldItemAnimationPlayCondition::Always;
    if (item != nullptr && item->placement_variant.has_value() &&
        action == HeldItemActionSecondaryUseStart) {
      play_condition = player::HeldItemAnimationPlayCondition::PlacementSucceeds;
    }
    return {library.find(DefaultHeldItemSwingAnimationName), play_condition};
  }
  return {};
}

} // namespace arv::client
