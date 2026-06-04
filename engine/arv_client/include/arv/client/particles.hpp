#pragma once

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/particles.hpp"
#include "arv/render/render_types.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

struct ParticleTypeTag {};
using ParticleTypeID = ID<ParticleTypeTag, u32>;

enum class ParticleBlendMode : u8 {
  Alpha,
  Additive,
};

enum class ParticlePriority : u8 {
  Low,
  Normal,
  High,
  Critical,
};

enum class ParticleAnimationSampleMode : u8 {
  LifetimeScaled,
  FixedFps,
};

enum class ParticleAnimationPlayback : u8 {
  Once,
  Loop,
  ClampLastFrame,
};

struct ParticleLoadError {
  std::string source{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

struct ParticleScalarRange {
  f32 min{};
  f32 max{};
};

struct ParticleCountRange {
  u16 min{};
  u16 max{};
};

struct ParticleVec3Range {
  ParticleScalarRange x{};
  ParticleScalarRange y{};
  ParticleScalarRange z{};
};

struct ParticleVec4Range {
  Vec4f min{};
  Vec4f max{};
};

struct ParticleAnimation {
  ParticleAnimationSampleMode sample_mode{ParticleAnimationSampleMode::LifetimeScaled};
  ParticleAnimationPlayback playback{ParticleAnimationPlayback::Once};
  f32 frames_per_second{30.0F};
};

struct ParticleTypeDefinition {
  std::string id{};
  std::vector<std::string> texture_frames{};
  ParticleAnimation animation{};
  ParticleCountRange count{1U, 1U};
  ParticleScalarRange lifetime_seconds{0.5F, 0.5F};
  ParticleScalarRange size_start{0.12F, 0.12F};
  ParticleScalarRange size_end{0.04F, 0.04F};
  ParticleVec3Range velocity{};
  Vec3f acceleration{};
  f32 drag{0.96F};
  ParticleVec4Range color{{1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 0.0F}};
  ParticleBlendMode blend_mode{ParticleBlendMode::Alpha};
  ParticlePriority priority{ParticlePriority::Normal};
  bool distance_scale{true};
};

struct ParticleAtlasFrame {
  std::string asset_id{};
  Vec2f uv_min{};
  Vec2f uv_max{};
  u32 atlas_page{};
  u32 width{};
  u32 height{};
};

struct ParticleType {
  ParticleTypeID id{};
  std::string name{};
  std::vector<ParticleAtlasFrame> frames{};
  ParticleAnimation animation{};
  ParticleCountRange count{1U, 1U};
  ParticleScalarRange lifetime_seconds{0.5F, 0.5F};
  ParticleScalarRange size_start{0.12F, 0.12F};
  ParticleScalarRange size_end{0.04F, 0.04F};
  ParticleVec3Range velocity{};
  Vec3f acceleration{};
  f32 drag{0.96F};
  ParticleVec4Range color{{1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 0.0F}};
  ParticleBlendMode blend_mode{ParticleBlendMode::Alpha};
  ParticlePriority priority{ParticlePriority::Normal};
  bool distance_scale{true};
};

class ParticleRegistry {
public:
  [[nodiscard]] ParticleTypeID add(ParticleType type);
  void add_frame_reference(const ParticleAtlasFrame& frame);
  [[nodiscard]] const ParticleType* find(std::string_view name) const noexcept;
  [[nodiscard]] const ParticleType* find(ParticleTypeID id) const noexcept;
  [[nodiscard]] const ParticleAtlasFrame* find_frame(std::string_view asset_id) const noexcept;
  [[nodiscard]] std::span<const ParticleType> types() const noexcept {
    return types_;
  }
  [[nodiscard]] usize size() const noexcept {
    return types_.size();
  }
  void clear() noexcept;

private:
  std::vector<ParticleType> types_{};
  std::vector<ParticleAtlasFrame> frames_{};
  HashMap<std::string, ParticleTypeID> by_name_{};
  HashMap<std::string, u32> frame_by_asset_{};
};

struct ParticleAssets {
  ParticleRegistry registry{};
  asset::TextureAtlas atlas{};
};

using ParticleDefinitionResult = Expected<ParticleTypeDefinition, ParticleLoadError>;
using ParticleAssetsResult = Expected<ParticleAssets, ParticleLoadError>;

[[nodiscard]] asset::TextureAtlasBuildOptions default_particle_atlas_options() noexcept;
[[nodiscard]] ParticleDefinitionResult parse_particle_definition_yaml(
    std::string_view text,
    std::string source = {});
[[nodiscard]] ParticleAssetsResult load_particle_assets_from_packs(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    asset::TextureAtlasBuildOptions options,
    std::span<const std::string> extra_texture_assets = {});

[[nodiscard]] usize particle_animation_frame_index(
    const ParticleAnimation& animation,
    usize frame_count,
    Seconds age_seconds,
    Seconds lifetime_seconds) noexcept;

struct ParticleBudgetConfig {
  u32 max_live_particles{8192U};
  u16 max_spawn_per_burst{1024U};
  f32 full_rate_distance{32.0F};
  f32 half_rate_distance{56.0F};
  f32 quarter_rate_distance{84.0F};
  f32 cull_distance{128.0F};
};

struct ParticleStats {
  u32 live_particles{};
  u32 rendered_last_frame{};
  u64 spawned_total{};
  u64 expired_total{};
  u64 rejected_unknown_type{};
  u64 rejected_budget{};
  u64 rejected_distance{};
  u64 rejected_invalid{};
};

struct ParticleBurstDesc {
  std::string type_name{};
  std::string texture_asset{};
  Vec3f position{};
  Vec3f normal{0.0F, 1.0F, 0.0F};
  Vec3f extents{};
  f32 radius{};
  u16 count{};
  u32 seed{1U};
  protocol::ParticleSpawnShape shape{protocol::ParticleSpawnShape::Point};
};

class ParticleSystem {
public:
  explicit ParticleSystem(ParticleBudgetConfig budgets = {});

  void set_registry(const ParticleRegistry* registry) noexcept;
  void set_budgets(ParticleBudgetConfig budgets) noexcept;
  [[nodiscard]] const ParticleBudgetConfig& budgets() const noexcept {
    return budgets_;
  }

  [[nodiscard]] u32 spawn_burst(const ParticleBurstDesc& desc, Vec3f camera_position);
  void update(Seconds delta_seconds) noexcept;
  void clear() noexcept;

  void append_render_items(std::vector<render::ParticleDrawItem>& out) noexcept;

  [[nodiscard]] ParticleStats stats() const noexcept {
    return stats_;
  }
  [[nodiscard]] usize live_count() const noexcept {
    return particles_.size();
  }

private:
  struct LiveParticle {
    ParticleTypeID type_id{};
    Vec3f position{};
    Vec3f velocity{};
    Seconds age_seconds{};
    Seconds lifetime_seconds{0.5F};
    f32 size_start{0.1F};
    f32 size_end{0.0F};
    Vec4f color_start{1.0F, 1.0F, 1.0F, 1.0F};
    Vec4f color_end{1.0F, 1.0F, 1.0F, 0.0F};
    f32 rotation_radians{};
    f32 angular_velocity_radians{};
    ParticleAtlasFrame frame_override{};
    bool has_frame_override{};
  };

  [[nodiscard]] u16 distance_scaled_count(
      const ParticleType& type,
      u16 requested,
      Vec3f position,
      Vec3f camera_position) const noexcept;

  const ParticleRegistry* registry_{};
  ParticleBudgetConfig budgets_{};
  std::vector<LiveParticle> particles_{};
  ParticleStats stats_{};
};

} // namespace arv::client
