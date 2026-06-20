#include "arv/client/particles.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace arv::client {
namespace {

struct Rng {
  u32 state{1U};

  [[nodiscard]] u32 next_u32() noexcept {
    if (state == 0U) {
      state = 0xA341316CU;
    }
    u32 x = state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    state = x;
    return x;
  }

  [[nodiscard]] f32 unit() noexcept {
    return static_cast<f32>(next_u32() >> 8U) * (1.0F / 16777216.0F);
  }

  [[nodiscard]] f32 signed_unit() noexcept {
    return unit() * 2.0F - 1.0F;
  }
};

[[nodiscard]] ParticleLoadError error(std::string source, std::string message) {
  return {std::move(source), std::move(message)};
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view name) noexcept {
  return object.kind() == io::DataKind::Object ? object.get(name) : nullptr;
}

[[nodiscard]] bool is_number(const io::DataValue& value) noexcept {
  return io::is_number_value(value);
}

[[nodiscard]] std::optional<f32> number_value(const io::DataValue& value) noexcept {
  if (!is_number(value)) {
    return std::nullopt;
  }
  return static_cast<f32>(value.as_f64_or(0.0));
}

[[nodiscard]] Expected<std::string, ParticleLoadError> required_string(
    const io::DataValue& object,
    std::string_view key,
    std::string_view source) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return Expected<std::string, ParticleLoadError>::failure(
        error(std::string{source}, std::string{"missing string field '"} + std::string{key} + "'"));
  }
  return Expected<std::string, ParticleLoadError>::success(
      std::string{value->as_string_or({})});
}

[[nodiscard]] std::string optional_string(
    const io::DataValue& object,
    std::string_view key,
    std::string_view fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::String
             ? std::string{value->as_string_or({})}
             : std::string{fallback};
}

[[nodiscard]] f32 optional_number(
    const io::DataValue& object,
    std::string_view key,
    f32 fallback) noexcept {
  const io::DataValue* value = member(object, key);
  const std::optional<f32> parsed = value == nullptr ? std::nullopt : number_value(*value);
  return parsed.value_or(fallback);
}

[[nodiscard]] bool optional_bool(
    const io::DataValue& object,
    std::string_view key,
    bool fallback) noexcept {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::Bool
             ? value->as_bool_or(fallback)
             : fallback;
}

[[nodiscard]] Expected<ParticleScalarRange, ParticleLoadError> parse_scalar_range(
    const io::DataValue& value,
    ParticleScalarRange fallback,
    std::string source,
    std::string path) {
  if (is_number(value)) {
    const f32 scalar = static_cast<f32>(value.as_f64_or(0.0));
    return Expected<ParticleScalarRange, ParticleLoadError>::success({scalar, scalar});
  }

  const io::DataValue::Array* values = value.array_if();
  if (values == nullptr || values->size() != 2U ||
      !is_number((*values)[0]) || !is_number((*values)[1])) {
    return Expected<ParticleScalarRange, ParticleLoadError>::failure(
        error(std::move(source), path + " must be a number or two-number range"));
  }

  ParticleScalarRange range{
      static_cast<f32>((*values)[0].as_f64_or(fallback.min)),
      static_cast<f32>((*values)[1].as_f64_or(fallback.max))};
  if (!std::isfinite(range.min) || !std::isfinite(range.max) || range.max < range.min) {
    return Expected<ParticleScalarRange, ParticleLoadError>::failure(
        error(std::move(source), path + " range is invalid"));
  }
  return Expected<ParticleScalarRange, ParticleLoadError>::success(range);
}

[[nodiscard]] Expected<ParticleScalarRange, ParticleLoadError> optional_scalar_range(
    const io::DataValue& object,
    std::string_view key,
    ParticleScalarRange fallback,
    std::string_view source) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return Expected<ParticleScalarRange, ParticleLoadError>::success(fallback);
  }
  return parse_scalar_range(
      *value,
      fallback,
      std::string{source},
      std::string{key});
}

[[nodiscard]] Expected<ParticleCountRange, ParticleLoadError> optional_count_range(
    const io::DataValue& object,
    std::string_view key,
    ParticleCountRange fallback,
    std::string_view source) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return Expected<ParticleCountRange, ParticleLoadError>::success(fallback);
  }

  if (is_number(*value)) {
    const u64 scalar = value->as_u64_or(fallback.min);
    if (scalar > std::numeric_limits<u16>::max()) {
      return Expected<ParticleCountRange, ParticleLoadError>::failure(
          error(std::string{source}, std::string{key} + " is out of range"));
    }
    return Expected<ParticleCountRange, ParticleLoadError>::success(
        {static_cast<u16>(scalar), static_cast<u16>(scalar)});
  }

  const io::DataValue::Array* values = value->array_if();
  if (values == nullptr || values->size() != 2U ||
      !is_number((*values)[0]) || !is_number((*values)[1])) {
    return Expected<ParticleCountRange, ParticleLoadError>::failure(
        error(std::string{source}, std::string{key} + " must be a count or count range"));
  }
  const u64 min = (*values)[0].as_u64_or(fallback.min);
  const u64 max = (*values)[1].as_u64_or(fallback.max);
  if (min > max || max > std::numeric_limits<u16>::max()) {
    return Expected<ParticleCountRange, ParticleLoadError>::failure(
        error(std::string{source}, std::string{key} + " range is invalid"));
  }
  return Expected<ParticleCountRange, ParticleLoadError>::success(
      {static_cast<u16>(min), static_cast<u16>(max)});
}

[[nodiscard]] Expected<Vec3f, ParticleLoadError> parse_vec3(
    const io::DataValue& value,
    Vec3f fallback,
    std::string source,
    std::string path) {
  if (const io::DataValue::Array* values = value.array_if();
      values != nullptr && values->size() == 3U) {
    for (const io::DataValue& child : *values) {
      if (!is_number(child)) {
        return Expected<Vec3f, ParticleLoadError>::failure(
            error(std::move(source), path + " must contain numbers"));
      }
    }
    return Expected<Vec3f, ParticleLoadError>::success({
        static_cast<f32>((*values)[0].as_f64_or(fallback.x)),
        static_cast<f32>((*values)[1].as_f64_or(fallback.y)),
        static_cast<f32>((*values)[2].as_f64_or(fallback.z))});
  }

  if (value.kind() == io::DataKind::Object) {
    return Expected<Vec3f, ParticleLoadError>::success({
        optional_number(value, "x", fallback.x),
        optional_number(value, "y", fallback.y),
        optional_number(value, "z", fallback.z)});
  }

  return Expected<Vec3f, ParticleLoadError>::failure(
      error(std::move(source), path + " must be a three-number array or object"));
}

[[nodiscard]] Expected<Vec3f, ParticleLoadError> optional_vec3(
    const io::DataValue& object,
    std::string_view key,
    Vec3f fallback,
    std::string_view source) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return Expected<Vec3f, ParticleLoadError>::success(fallback);
  }
  return parse_vec3(*value, fallback, std::string{source}, std::string{key});
}

[[nodiscard]] Expected<Vec4f, ParticleLoadError> parse_vec4(
    const io::DataValue& value,
    Vec4f fallback,
    std::string source,
    std::string path) {
  const io::DataValue::Array* values = value.array_if();
  if (values == nullptr || values->size() != 4U) {
    return Expected<Vec4f, ParticleLoadError>::failure(
        error(std::move(source), path + " must be a four-number array"));
  }
  for (const io::DataValue& child : *values) {
    if (!is_number(child)) {
      return Expected<Vec4f, ParticleLoadError>::failure(
          error(std::move(source), path + " must contain numbers"));
    }
  }
  return Expected<Vec4f, ParticleLoadError>::success({
      static_cast<f32>((*values)[0].as_f64_or(fallback.x)),
      static_cast<f32>((*values)[1].as_f64_or(fallback.y)),
      static_cast<f32>((*values)[2].as_f64_or(fallback.z)),
      static_cast<f32>((*values)[3].as_f64_or(fallback.w))});
}

[[nodiscard]] Expected<ParticleVec4Range, ParticleLoadError> optional_color_range(
    const io::DataValue& object,
    ParticleVec4Range fallback,
    std::string_view source) {
  const io::DataValue* value = member(object, "color");
  if (value == nullptr) {
    return Expected<ParticleVec4Range, ParticleLoadError>::success(fallback);
  }
  if (value->kind() != io::DataKind::Object) {
    return Expected<ParticleVec4Range, ParticleLoadError>::failure(
        error(std::string{source}, "color must be an object"));
  }
  const io::DataValue* start = member(*value, "start");
  const io::DataValue* end = member(*value, "end");
  ParticleVec4Range color = fallback;
  if (start != nullptr) {
    auto parsed = parse_vec4(*start, fallback.min, std::string{source}, "color.start");
    if (!parsed) {
      return Expected<ParticleVec4Range, ParticleLoadError>::failure(std::move(parsed.error()));
    }
    color.min = parsed.value();
  }
  if (end != nullptr) {
    auto parsed = parse_vec4(*end, fallback.max, std::string{source}, "color.end");
    if (!parsed) {
      return Expected<ParticleVec4Range, ParticleLoadError>::failure(std::move(parsed.error()));
    }
    color.max = parsed.value();
  }
  return Expected<ParticleVec4Range, ParticleLoadError>::success(color);
}

[[nodiscard]] Expected<ParticleVec3Range, ParticleLoadError> optional_vec3_range(
    const io::DataValue& object,
    std::string_view key,
    ParticleVec3Range fallback,
    std::string_view source) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return Expected<ParticleVec3Range, ParticleLoadError>::success(fallback);
  }
  if (value->kind() != io::DataKind::Object) {
    return Expected<ParticleVec3Range, ParticleLoadError>::failure(
        error(std::string{source}, std::string{key} + " must be an object"));
  }

  ParticleVec3Range result = fallback;
  if (const io::DataValue* child = member(*value, "x"); child != nullptr) {
    auto parsed = parse_scalar_range(*child, result.x, std::string{source}, std::string{key} + ".x");
    if (!parsed) {
      return Expected<ParticleVec3Range, ParticleLoadError>::failure(std::move(parsed.error()));
    }
    result.x = parsed.value();
  }
  if (const io::DataValue* child = member(*value, "y"); child != nullptr) {
    auto parsed = parse_scalar_range(*child, result.y, std::string{source}, std::string{key} + ".y");
    if (!parsed) {
      return Expected<ParticleVec3Range, ParticleLoadError>::failure(std::move(parsed.error()));
    }
    result.y = parsed.value();
  }
  if (const io::DataValue* child = member(*value, "z"); child != nullptr) {
    auto parsed = parse_scalar_range(*child, result.z, std::string{source}, std::string{key} + ".z");
    if (!parsed) {
      return Expected<ParticleVec3Range, ParticleLoadError>::failure(std::move(parsed.error()));
    }
    result.z = parsed.value();
  }
  return Expected<ParticleVec3Range, ParticleLoadError>::success(result);
}

[[nodiscard]] ParticleAnimationSampleMode sample_mode_from_string(std::string_view value) {
  return value == "fixed_fps" ? ParticleAnimationSampleMode::FixedFps
                              : ParticleAnimationSampleMode::LifetimeScaled;
}

[[nodiscard]] std::optional<ParticleAnimationPlayback> playback_from_string(
    std::string_view value) {
  if (value == "once") {
    return ParticleAnimationPlayback::Once;
  }
  if (value == "loop") {
    return ParticleAnimationPlayback::Loop;
  }
  if (value == "clamp_last_frame") {
    return ParticleAnimationPlayback::ClampLastFrame;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ParticleBlendMode> blend_from_string(std::string_view value) {
  if (value == "alpha") {
    return ParticleBlendMode::Alpha;
  }
  if (value == "additive") {
    return ParticleBlendMode::Additive;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ParticlePriority> priority_from_string(std::string_view value) {
  if (value == "low") {
    return ParticlePriority::Low;
  }
  if (value == "normal") {
    return ParticlePriority::Normal;
  }
  if (value == "high") {
    return ParticlePriority::High;
  }
  if (value == "critical") {
    return ParticlePriority::Critical;
  }
  return std::nullopt;
}

[[nodiscard]] f32 sample_range(Rng& rng, ParticleScalarRange range) noexcept {
  return range.min + (range.max - range.min) * rng.unit();
}

[[nodiscard]] u16 sample_count(Rng& rng, ParticleCountRange range) noexcept {
  if (range.max <= range.min) {
    return range.min;
  }
  return static_cast<u16>(
      range.min + (rng.next_u32() % (static_cast<u32>(range.max - range.min) + 1U)));
}

[[nodiscard]] Vec3f sample_range(Rng& rng, ParticleVec3Range range) noexcept {
  return {
      sample_range(rng, range.x),
      sample_range(rng, range.y),
      sample_range(rng, range.z)};
}

[[nodiscard]] Vec4f sample_range(Rng& rng, ParticleVec4Range range) noexcept {
  return {
      range.min.x + (range.max.x - range.min.x) * rng.unit(),
      range.min.y + (range.max.y - range.min.y) * rng.unit(),
      range.min.z + (range.max.z - range.min.z) * rng.unit(),
      range.min.w + (range.max.w - range.min.w) * rng.unit()};
}

[[nodiscard]] Vec4f lerp(Vec4f from, Vec4f to, f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha,
      from.w + (to.w - from.w) * alpha};
}

[[nodiscard]] render::ParticleRenderBlendMode render_blend(ParticleBlendMode mode) noexcept {
  return mode == ParticleBlendMode::Additive
             ? render::ParticleRenderBlendMode::Additive
             : render::ParticleRenderBlendMode::Alpha;
}

[[nodiscard]] ParticleAtlasFrame atlas_frame_from_entry(
    const asset::TextureAtlasEntry& entry) {
  return {
      entry.asset_id.value,
      {static_cast<f32>(entry.unpadded_pixel_min.x),
       static_cast<f32>(entry.unpadded_pixel_min.y)},
      {static_cast<f32>(entry.unpadded_pixel_max.x),
       static_cast<f32>(entry.unpadded_pixel_max.y)},
      entry.page,
      entry.width,
      entry.height};
}

[[nodiscard]] Vec3f safe_normal(Vec3f normal) noexcept {
  Vec3f normalized = normalize_or_zero(normal);
  return length(normalized) <= 0.0F ? Vec3f{0.0F, 1.0F, 0.0F} : normalized;
}

[[nodiscard]] Vec3f random_position_in_shape(
    Rng& rng,
    const ParticleBurstDesc& desc) noexcept {
  switch (desc.shape) {
    case protocol::ParticleSpawnShape::Box:
      return desc.position + Vec3f{
          desc.extents.x * rng.signed_unit(),
          desc.extents.y * rng.signed_unit(),
          desc.extents.z * rng.signed_unit()};
    case protocol::ParticleSpawnShape::BlockFace: {
      const Vec3f n = safe_normal(desc.normal);
      const Vec3f reference = std::abs(n.y) < 0.9F ? Vec3f{0.0F, 1.0F, 0.0F}
                                                   : Vec3f{1.0F, 0.0F, 0.0F};
      const Vec3f tangent = normalize_or_zero(cross(reference, n));
      const Vec3f bitangent = normalize_or_zero(cross(n, tangent));
      return desc.position +
             tangent * (desc.extents.x * rng.signed_unit()) +
             bitangent * (desc.extents.y * rng.signed_unit()) +
             n * (std::max(desc.radius, 0.0F) * rng.unit());
    }
    case protocol::ParticleSpawnShape::Sphere: {
      const f32 radius = std::max(desc.radius, 0.0F);
      if (radius <= 0.0F) {
        return desc.position;
      }
      const f32 z = rng.signed_unit();
      const f32 angle = rng.unit() * TauRadians;
      const f32 r = std::sqrt(std::max(0.0F, 1.0F - z * z));
      const f32 distance = radius * std::cbrt(rng.unit());
      return desc.position + Vec3f{
          std::cos(angle) * r * distance,
          z * distance,
          std::sin(angle) * r * distance};
    }
    case protocol::ParticleSpawnShape::Point:
      break;
  }
  return desc.position;
}

} // namespace

std::string ParticleLoadError::describe() const {
  if (source.empty()) {
    return message;
  }
  return source + ": " + message;
}

ParticleTypeID ParticleRegistry::add(ParticleType type) {
  const auto found = by_name_.find(type.name);
  if (found != by_name_.end()) {
    type.id = found->second;
    for (const ParticleAtlasFrame& frame : type.frames) {
      add_frame_reference(frame);
    }
    types_[found->second.value - 1U] = std::move(type);
    return found->second;
  }

  type.id = ParticleTypeID{static_cast<u32>(types_.size() + 1U)};
  const ParticleTypeID id = type.id;
  by_name_[type.name] = id;
  for (const ParticleAtlasFrame& frame : type.frames) {
    add_frame_reference(frame);
  }
  types_.push_back(std::move(type));
  return id;
}

const ParticleType* ParticleRegistry::find(std::string_view name) const noexcept {
  const auto found = by_name_.find(std::string{name});
  return found == by_name_.end() ? nullptr : find(found->second);
}

const ParticleType* ParticleRegistry::find(ParticleTypeID id) const noexcept {
  if (id.value == 0U || id.value > types_.size()) {
    return nullptr;
  }
  return &types_[id.value - 1U];
}

const ParticleAtlasFrame* ParticleRegistry::find_frame(std::string_view asset_id) const noexcept {
  const auto found = frame_by_asset_.find(std::string{asset_id});
  if (found == frame_by_asset_.end() || found->second >= frames_.size()) {
    return nullptr;
  }
  return &frames_[found->second];
}

void ParticleRegistry::clear() noexcept {
  types_.clear();
  frames_.clear();
  by_name_.clear();
  frame_by_asset_.clear();
}

void ParticleRegistry::add_frame_reference(const ParticleAtlasFrame& frame) {
  if (frame.asset_id.empty()) {
    return;
  }
  const auto found = frame_by_asset_.find(frame.asset_id);
  if (found != frame_by_asset_.end()) {
    frames_[found->second] = frame;
    return;
  }
  const u32 index = static_cast<u32>(frames_.size());
  frame_by_asset_[frame.asset_id] = index;
  frames_.push_back(frame);
}

u32 next_local_particle_seed(LocalParticleSeedState& state, BlockPos pos) noexcept {
  const u32 sequence = state.next_sequence++;
  const u64 x = static_cast<u64>(static_cast<i64>(pos.value.x) * 73856093LL);
  const u64 y = static_cast<u64>(static_cast<i64>(pos.value.y) * 19349663LL);
  const u64 z = static_cast<u64>(static_cast<i64>(pos.value.z) * 83492791LL);
  const u32 seed = static_cast<u32>(x ^ y ^ z ^ sequence);
  return seed == 0U ? 1U : seed;
}

std::optional<ParticleBurstDesc> build_local_block_face_particle_burst(
    const LocalBlockFaceParticleBurstInput& input) {
  const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
      input.collision_query,
      input.origin,
      input.direction,
      input.max_distance);
  if (!hit.has_value() || input.block_registry.is_air(hit->variant)) {
    return std::nullopt;
  }

  const voxel::BlockVariantRecord* variant =
      input.block_registry.variant(hit->variant);
  const Vec3i offset = voxel::block_face_offset(hit->face);
  const Vec3f normal{
      static_cast<f32>(offset.x),
      static_cast<f32>(offset.y),
      static_cast<f32>(offset.z)};

  ParticleBurstDesc desc;
  desc.type_name = "arvane:block_dust";
  if (variant != nullptr) {
    desc.texture_asset = variant->particle_texture_asset;
  }
  desc.position = hit->hit_point + normal * 0.035F;
  desc.normal = normal;
  desc.extents = {input.extent, input.extent, 0.0F};
  desc.radius = input.radius;
  desc.count = input.count;
  desc.seed = next_local_particle_seed(input.seed_state, hit->block);
  desc.shape = protocol::ParticleSpawnShape::BlockFace;
  return desc;
}

bool consume_local_block_mining_particle_due(
    LocalBlockMiningParticleState& state,
    bool mining_block,
    Seconds now_seconds,
    Seconds interval_seconds) noexcept {
  if (!mining_block) {
    state.next_spawn_time_seconds = 0.0;
    return false;
  }
  if (now_seconds < state.next_spawn_time_seconds) {
    return false;
  }
  state.next_spawn_time_seconds = now_seconds + std::max<Seconds>(interval_seconds, 0.0);
  return true;
}

std::optional<ParticleBurstDesc> update_local_sprint_ground_particle_burst(
    LocalSprintGroundParticleState& state,
    const LocalSprintGroundParticleInput& input) {
  const f32 horizontal_speed_sq =
      input.player_velocity.x * input.player_velocity.x +
      input.player_velocity.z * input.player_velocity.z;
  const bool sprinting_on_ground =
      input.sprint_held &&
      input.move_forward &&
      !input.move_backward &&
      !input.sneak_held &&
      input.player_grounded &&
      horizontal_speed_sq > 1.0F;
  if (!sprinting_on_ground) {
    state.accumulator_seconds = 0.0;
    return std::nullopt;
  }

  const Seconds interval = std::max<Seconds>(input.interval_seconds, 0.0);
  if (interval <= 0.0) {
    state.accumulator_seconds = 0.0;
  } else {
    state.accumulator_seconds += std::max<Seconds>(input.delta_seconds, 0.0);
    if (state.accumulator_seconds < interval) {
      return std::nullopt;
    }
    state.accumulator_seconds = std::fmod(state.accumulator_seconds, interval);
  }

  return build_local_block_face_particle_burst(
      {
          .collision_query = input.collision_query,
          .block_registry = input.block_registry,
          .seed_state = input.seed_state,
          .origin = input.player_position + Vec3f{0.0F, 0.08F, 0.0F},
          .direction = {0.0F, -1.0F, 0.0F},
          .max_distance = 0.35F,
          .count = 3U,
          .extent = 0.28F,
          .radius = 0.02F,
      });
}

asset::TextureAtlasBuildOptions default_particle_atlas_options() noexcept {
  return asset::TextureAtlasBuildOptions{1024U, 4U, 2U, false};
}

ParticleDefinitionResult parse_particle_definition_yaml(
    std::string_view text,
    std::string source) {
  auto decoded = io::decode_yaml_value(text);
  if (!decoded) {
    return ParticleDefinitionResult::failure(
        error(source, "failed to parse YAML: " + decoded.error().describe()));
  }

  const io::DataValue& root = decoded.value().root;
  if (root.kind() != io::DataKind::Object) {
    return ParticleDefinitionResult::failure(error(source, "particle definition must be an object"));
  }

  ParticleTypeDefinition definition;
  auto id = required_string(root, "id", source);
  if (!id) {
    return ParticleDefinitionResult::failure(std::move(id.error()));
  }
  definition.id = std::move(id.value());
  if (!asset::is_valid_asset_id(definition.id)) {
    return ParticleDefinitionResult::failure(error(source, "particle id must be a namespaced id"));
  }

  const io::DataValue* texture = member(root, "texture");
  if (texture == nullptr || texture->kind() != io::DataKind::Object) {
    return ParticleDefinitionResult::failure(error(source, "texture must be an object"));
  }
  const std::string texture_mode = optional_string(*texture, "mode", "static");
  if (texture_mode == "static") {
    auto image = required_string(*texture, "image", source);
    if (!image) {
      return ParticleDefinitionResult::failure(std::move(image.error()));
    }
    definition.texture_frames.push_back(std::move(image.value()));
  } else if (texture_mode == "sequence") {
    const io::DataValue* frames = member(*texture, "frames");
    const io::DataValue::Array* frame_array =
        frames == nullptr ? nullptr : frames->array_if();
    if (frame_array == nullptr || frame_array->empty()) {
      return ParticleDefinitionResult::failure(
          error(source, "texture.frames must contain at least one frame"));
    }
    definition.texture_frames.reserve(frame_array->size());
    for (const io::DataValue& frame : *frame_array) {
      if (frame.kind() != io::DataKind::String) {
        return ParticleDefinitionResult::failure(
            error(source, "texture.frames entries must be strings"));
      }
      definition.texture_frames.emplace_back(frame.as_string_or({}));
    }
  } else {
    return ParticleDefinitionResult::failure(
        error(source, "texture.mode must be static or sequence"));
  }
  for (const std::string& frame : definition.texture_frames) {
    if (!asset::is_valid_asset_id(frame)) {
      return ParticleDefinitionResult::failure(
          error(source, "particle texture frame must be a valid asset id: " + frame));
    }
  }

  if (const io::DataValue* animation = member(root, "animation");
      animation != nullptr && animation->kind() == io::DataKind::Object) {
    const std::string sample = optional_string(*animation, "sample", "lifetime_scaled");
    if (sample != "lifetime_scaled" && sample != "fixed_fps") {
      return ParticleDefinitionResult::failure(
          error(source, "animation.sample must be lifetime_scaled or fixed_fps"));
    }
    definition.animation.sample_mode = sample_mode_from_string(sample);
    const std::string playback = optional_string(*animation, "playback", "once");
    const std::optional<ParticleAnimationPlayback> parsed_playback =
        playback_from_string(playback);
    if (!parsed_playback.has_value()) {
      return ParticleDefinitionResult::failure(
          error(source, "animation.playback must be once, loop, or clamp_last_frame"));
    }
    definition.animation.playback = parsed_playback.value();
    definition.animation.frames_per_second =
        optional_number(*animation, "fps", definition.animation.frames_per_second);
    if (definition.animation.sample_mode == ParticleAnimationSampleMode::FixedFps &&
        (!std::isfinite(definition.animation.frames_per_second) ||
         definition.animation.frames_per_second <= 0.0F)) {
      return ParticleDefinitionResult::failure(
          error(source, "animation.fps must be positive for fixed_fps"));
    }
  }

  auto count = optional_count_range(root, "count", definition.count, source);
  auto lifetime =
      optional_scalar_range(root, "lifetime", definition.lifetime_seconds, source);
  auto velocity = optional_vec3_range(root, "velocity", definition.velocity, source);
  auto acceleration = optional_vec3(root, "acceleration", definition.acceleration, source);
  auto color = optional_color_range(root, definition.color, source);
  if (!count || !lifetime || !velocity || !acceleration || !color) {
    if (!count) {
      return ParticleDefinitionResult::failure(std::move(count.error()));
    }
    if (!lifetime) {
      return ParticleDefinitionResult::failure(std::move(lifetime.error()));
    }
    if (!velocity) {
      return ParticleDefinitionResult::failure(std::move(velocity.error()));
    }
    if (!acceleration) {
      return ParticleDefinitionResult::failure(std::move(acceleration.error()));
    }
    return ParticleDefinitionResult::failure(std::move(color.error()));
  }
  definition.count = count.value();
  definition.lifetime_seconds = lifetime.value();
  definition.velocity = velocity.value();
  definition.acceleration = acceleration.value();
  definition.color = color.value();

  if (const io::DataValue* size = member(root, "size");
      size != nullptr && size->kind() == io::DataKind::Object) {
    if (const io::DataValue* start = member(*size, "start"); start != nullptr) {
      auto parsed = parse_scalar_range(*start, definition.size_start, source, "size.start");
      if (!parsed) {
        return ParticleDefinitionResult::failure(std::move(parsed.error()));
      }
      definition.size_start = parsed.value();
    }
    if (const io::DataValue* end = member(*size, "end"); end != nullptr) {
      auto parsed = parse_scalar_range(*end, definition.size_end, source, "size.end");
      if (!parsed) {
        return ParticleDefinitionResult::failure(std::move(parsed.error()));
      }
      definition.size_end = parsed.value();
    }
  }

  definition.drag = optional_number(root, "drag", definition.drag);
  if (!std::isfinite(definition.drag) || definition.drag < 0.0F) {
    return ParticleDefinitionResult::failure(error(source, "drag must be finite and non-negative"));
  }

  const std::string blend = optional_string(root, "blend", "alpha");
  const std::optional<ParticleBlendMode> parsed_blend = blend_from_string(blend);
  if (!parsed_blend.has_value()) {
    return ParticleDefinitionResult::failure(error(source, "blend must be alpha or additive"));
  }
  definition.blend_mode = parsed_blend.value();

  const std::string priority = optional_string(root, "priority", "normal");
  const std::optional<ParticlePriority> parsed_priority = priority_from_string(priority);
  if (!parsed_priority.has_value()) {
    return ParticleDefinitionResult::failure(
        error(source, "priority must be low, normal, high, or critical"));
  }
  definition.priority = parsed_priority.value();
  definition.distance_scale = optional_bool(root, "distance_scale", definition.distance_scale);

  if (definition.lifetime_seconds.min <= 0.0F ||
      definition.lifetime_seconds.max < definition.lifetime_seconds.min) {
    return ParticleDefinitionResult::failure(
        error(source, "lifetime range must be positive and ordered"));
  }
  if (definition.size_start.min < 0.0F || definition.size_end.min < 0.0F) {
    return ParticleDefinitionResult::failure(error(source, "particle sizes must be non-negative"));
  }

  return ParticleDefinitionResult::success(std::move(definition));
}

ParticleAssetsResult load_particle_assets_from_packs(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    asset::TextureAtlasBuildOptions options,
    std::span<const std::string> extra_texture_assets) {
  const std::vector<asset::PackDataFile> files = packs.data_files("particles", ".yaml");
  if (files.empty()) {
    return ParticleAssetsResult::failure(error("particles", "no particle definitions were found"));
  }

  std::vector<ParticleTypeDefinition> definitions;
  HashMap<std::string, usize> definition_by_id;
  for (const asset::PackDataFile& file : files) {
    const std::string source = file.relative_path.generic_string();
    std::ifstream input(file.path);
    if (!input) {
      return ParticleAssetsResult::failure(error(source, "file could not be opened"));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    auto parsed = parse_particle_definition_yaml(buffer.str(), source);
    if (!parsed) {
      return ParticleAssetsResult::failure(std::move(parsed.error()));
    }
    ParticleTypeDefinition definition = std::move(parsed.value());
    const auto found = definition_by_id.find(definition.id);
    if (found == definition_by_id.end()) {
      definition_by_id[definition.id] = definitions.size();
      definitions.push_back(std::move(definition));
    } else {
      definitions[found->second] = std::move(definition);
    }
  }

  std::vector<asset::TextureAtlasInput> atlas_inputs;
  HashSet<std::string> seen_frames;
  for (const ParticleTypeDefinition& definition : definitions) {
    for (const std::string& asset_id : definition.texture_frames) {
      if (!seen_frames.insert(asset_id).second) {
        continue;
      }
      auto resolved = resources.resolve_asset({asset_id}, asset::AssetKind::Texture);
      if (!resolved) {
        return ParticleAssetsResult::failure(
            error(asset_id, "particle texture was not found: " + resolved.error().describe()));
      }
      auto loaded = resources.load_texture({asset_id});
      if (!loaded) {
        return ParticleAssetsResult::failure(
            error(asset_id, "particle texture could not be loaded: " + loaded.error().message));
      }
      const asset::TextureResource* texture = resources.texture(loaded.value());
      if (texture == nullptr || texture->fallback) {
        return ParticleAssetsResult::failure(
            error(asset_id, "particle texture resolved to missing-texture fallback"));
      }
      atlas_inputs.push_back({texture->asset_id, texture->image, false});
    }
  }
  for (const std::string& asset_id : extra_texture_assets) {
    if (asset_id.empty() || !seen_frames.insert(asset_id).second) {
      continue;
    }
    auto resolved = resources.resolve_asset({asset_id}, asset::AssetKind::Texture);
    if (!resolved) {
      return ParticleAssetsResult::failure(
          error(asset_id, "particle texture was not found: " + resolved.error().describe()));
    }
    auto loaded = resources.load_texture({asset_id});
    if (!loaded) {
      return ParticleAssetsResult::failure(
          error(asset_id, "particle texture could not be loaded: " + loaded.error().message));
    }
    const asset::TextureResource* texture = resources.texture(loaded.value());
    if (texture == nullptr || texture->fallback) {
      return ParticleAssetsResult::failure(
          error(asset_id, "particle texture resolved to missing-texture fallback"));
    }
    atlas_inputs.push_back({texture->asset_id, texture->image, false});
  }

  auto atlas = asset::build_texture_atlas(atlas_inputs, options);
  if (!atlas) {
    return ParticleAssetsResult::failure(
        error(atlas.error().asset_id, atlas.error().operation + ": " + atlas.error().message));
  }

  ParticleRegistry registry;
  for (const ParticleTypeDefinition& definition : definitions) {
    ParticleType type;
    type.name = definition.id;
    type.animation = definition.animation;
    type.count = definition.count;
    type.lifetime_seconds = definition.lifetime_seconds;
    type.size_start = definition.size_start;
    type.size_end = definition.size_end;
    type.velocity = definition.velocity;
    type.acceleration = definition.acceleration;
    type.drag = definition.drag;
    type.color = definition.color;
    type.blend_mode = definition.blend_mode;
    type.priority = definition.priority;
    type.distance_scale = definition.distance_scale;
    type.frames.reserve(definition.texture_frames.size());
    for (const std::string& frame : definition.texture_frames) {
      const asset::TextureAtlasEntry* entry = atlas.value().find(frame);
      if (entry == nullptr) {
        return ParticleAssetsResult::failure(
            error(definition.id, "particle frame was not present in atlas: " + frame));
      }
      type.frames.push_back(atlas_frame_from_entry(*entry));
    }
    (void)registry.add(std::move(type));
  }
  for (const std::string& asset_id : extra_texture_assets) {
    const asset::TextureAtlasEntry* entry = atlas.value().find(asset_id);
    if (entry != nullptr) {
      registry.add_frame_reference(atlas_frame_from_entry(*entry));
    }
  }

  ParticleAssets assets;
  assets.registry = std::move(registry);
  assets.atlas = std::move(atlas.value());
  return ParticleAssetsResult::success(std::move(assets));
}

std::vector<std::string> block_particle_texture_assets(
    const voxel::BlockRegistry& registry) {
  std::vector<std::string> textures;
  for (u32 index = 0; index < registry.block_count(); ++index) {
    const voxel::BlockRecord* block = registry.block(voxel::BlockID{index});
    if (block == nullptr || block->particle_texture_asset.empty()) {
      continue;
    }
    if (std::find(textures.begin(), textures.end(), block->particle_texture_asset) ==
        textures.end()) {
      textures.push_back(block->particle_texture_asset);
    }
  }
  return textures;
}

usize particle_animation_frame_index(
    const ParticleAnimation& animation,
    usize frame_count,
    Seconds age_seconds,
    Seconds lifetime_seconds) noexcept {
  if (frame_count <= 1U) {
    return 0U;
  }
  const Seconds safe_age = std::max(age_seconds, 0.0F);
  u32 frame = 0U;
  if (animation.sample_mode == ParticleAnimationSampleMode::FixedFps) {
    const f32 fps = std::max(animation.frames_per_second, 0.001F);
    frame = static_cast<u32>(std::floor(safe_age * fps));
  } else {
    const f32 lifetime = std::max(lifetime_seconds, 0.001F);
    const f32 alpha = std::clamp(safe_age / lifetime, 0.0F, 0.999999F);
    frame = static_cast<u32>(std::floor(alpha * static_cast<f32>(frame_count)));
  }

  switch (animation.playback) {
    case ParticleAnimationPlayback::Loop:
      return frame % frame_count;
    case ParticleAnimationPlayback::Once:
    case ParticleAnimationPlayback::ClampLastFrame:
      return std::min<usize>(frame, frame_count - 1U);
  }
  return 0U;
}

ParticleSystem::ParticleSystem(ParticleBudgetConfig budgets) : budgets_(budgets) {
}

void ParticleSystem::set_registry(const ParticleRegistry* registry) noexcept {
  registry_ = registry;
}

void ParticleSystem::set_budgets(ParticleBudgetConfig budgets) noexcept {
  budgets_ = budgets;
}

u32 ParticleSystem::spawn_burst(const ParticleBurstDesc& desc, Vec3f camera_position) {
  if (registry_ == nullptr) {
    ++stats_.rejected_unknown_type;
    return 0U;
  }
  const ParticleType* type = registry_->find(desc.type_name);
  if (type == nullptr || type->frames.empty()) {
    ++stats_.rejected_unknown_type;
    return 0U;
  }
  Rng rng{desc.seed == 0U ? 1U : desc.seed};
  const u16 requested = desc.count == 0U ? sample_count(rng, type->count) : desc.count;
  const ParticleAtlasFrame* frame_override = nullptr;
  if (!desc.texture_asset.empty()) {
    frame_override = registry_->find_frame(desc.texture_asset);
    if (frame_override == nullptr) {
      stats_.rejected_invalid += requested;
      return 0U;
    }
  }
  const u16 limited_requested = std::min(requested, budgets_.max_spawn_per_burst);
  const u16 scaled = distance_scaled_count(
      *type,
      limited_requested,
      desc.position,
      camera_position);
  if (scaled == 0U) {
    stats_.rejected_distance += limited_requested;
    return 0U;
  }
  if (particles_.size() >= budgets_.max_live_particles) {
    stats_.rejected_budget += scaled;
    stats_.live_particles = static_cast<u32>(particles_.size());
    return 0U;
  }
  const u32 remaining =
      budgets_.max_live_particles - static_cast<u32>(particles_.size());
  const u32 to_spawn = std::min<u32>(scaled, remaining);
  if (to_spawn < scaled) {
    stats_.rejected_budget += static_cast<u64>(scaled - to_spawn);
  }

  particles_.reserve(std::min<usize>(budgets_.max_live_particles, particles_.size() + to_spawn));
  for (u32 index = 0; index < to_spawn; ++index) {
    LiveParticle particle;
    particle.type_id = type->id;
    particle.position = random_position_in_shape(rng, desc);
    particle.velocity = sample_range(rng, type->velocity);
    if (desc.shape == protocol::ParticleSpawnShape::BlockFace) {
      particle.velocity = particle.velocity + safe_normal(desc.normal) * (0.25F + rng.unit() * 0.35F);
    }
    particle.lifetime_seconds =
        std::max(0.001F, sample_range(rng, type->lifetime_seconds));
    particle.size_start = sample_range(rng, type->size_start);
    particle.size_end = sample_range(rng, type->size_end);
    particle.color_start = type->color.min;
    particle.color_end = type->color.max;
    particle.rotation_radians = rng.unit() * TauRadians;
    particle.angular_velocity_radians = rng.signed_unit() * 3.0F;
    if (frame_override != nullptr) {
      particle.frame_override = *frame_override;
      particle.has_frame_override = true;
    }
    particles_.push_back(particle);
  }
  stats_.spawned_total += to_spawn;
  stats_.live_particles = static_cast<u32>(particles_.size());
  return to_spawn;
}

void ParticleSystem::update(Seconds delta_seconds) noexcept {
  if (delta_seconds <= 0.0F || particles_.empty()) {
    stats_.live_particles = static_cast<u32>(particles_.size());
    return;
  }

  const Seconds dt = delta_seconds;
  usize write = 0U;
  for (usize read = 0U; read < particles_.size(); ++read) {
    LiveParticle particle = particles_[read];
    const ParticleType* type = registry_ == nullptr ? nullptr : registry_->find(particle.type_id);
    if (type == nullptr) {
      ++stats_.expired_total;
      continue;
    }
    particle.age_seconds += dt;
    if (particle.age_seconds >= particle.lifetime_seconds) {
      ++stats_.expired_total;
      continue;
    }
    particle.velocity = particle.velocity + type->acceleration * dt;
    const f32 drag = std::pow(std::max(type->drag, 0.0F), dt * 60.0F);
    particle.velocity = particle.velocity * drag;
    particle.position = particle.position + particle.velocity * dt;
    particle.rotation_radians += particle.angular_velocity_radians * dt;
    particles_[write++] = particle;
  }
  particles_.resize(write);
  stats_.live_particles = static_cast<u32>(particles_.size());
}

void ParticleSystem::clear() noexcept {
  particles_.clear();
  stats_.live_particles = 0U;
  stats_.rendered_last_frame = 0U;
}

void ParticleSystem::append_render_items(std::vector<render::ParticleDrawItem>& out) noexcept {
  stats_.rendered_last_frame = 0U;
  if (registry_ == nullptr) {
    return;
  }
  out.reserve(out.size() + particles_.size());
  for (const LiveParticle& particle : particles_) {
    const ParticleType* type = registry_->find(particle.type_id);
    if (type == nullptr || type->frames.empty()) {
      continue;
    }
    const f32 alpha = std::clamp(
        particle.age_seconds / std::max(particle.lifetime_seconds, 0.001F),
        0.0F,
        1.0F);
    const usize frame_index = particle_animation_frame_index(
        type->animation,
        type->frames.size(),
        particle.age_seconds,
        particle.lifetime_seconds);
    const ParticleAtlasFrame& frame =
        particle.has_frame_override ? particle.frame_override : type->frames[frame_index];
    const f32 size = particle.size_start + (particle.size_end - particle.size_start) * alpha;
    const Vec4f color = lerp(particle.color_start, particle.color_end, alpha);
    if (size <= 0.0F || color.w <= 0.0F) {
      continue;
    }
    out.push_back({
        particle.position,
        size,
        color,
        frame.uv_min,
        frame.uv_max,
        frame.atlas_page,
        particle.rotation_radians,
        render_blend(type->blend_mode)});
    ++stats_.rendered_last_frame;
  }
  stats_.live_particles = static_cast<u32>(particles_.size());
}

u16 ParticleSystem::distance_scaled_count(
    const ParticleType& type,
    u16 requested,
    Vec3f position,
    Vec3f camera_position) const noexcept {
  if (!type.distance_scale || requested == 0U) {
    return requested;
  }
  const f32 d = distance(position, camera_position);
  if (!std::isfinite(d) || d > budgets_.cull_distance) {
    return 0U;
  }
  if (d <= budgets_.full_rate_distance) {
    return requested;
  }
  const f32 multiplier = d <= budgets_.half_rate_distance
                             ? 0.5F
                             : (d <= budgets_.quarter_rate_distance ? 0.25F : 0.125F);
  return static_cast<u16>(std::max<u32>(
      1U,
      static_cast<u32>(std::floor(static_cast<f32>(requested) * multiplier))));
}

} // namespace arv::client
