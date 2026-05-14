#include "arv/world/worldgen/noise.hpp"

#include "arv/world/worldgen/seeds.hpp"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <cmath>

namespace arv::world::worldgen {
namespace {

[[nodiscard]] f32 lerp(f32 a, f32 b, f32 t) noexcept {
  return a + (b - a) * t;
}

[[nodiscard]] f32 interpolate(
    f32 lower_left,
    f32 lower_right,
    f32 upper_left,
    f32 upper_right,
    f32 t_x,
    f32 t_z) noexcept {
  return lerp(lerp(lower_left, lower_right, t_x), lerp(upper_left, upper_right, t_x), t_z);
}

} // namespace

struct NoiseSampler::Impl {
  NoiseFieldConfig config{};
  int seed{};
  FastNoise::SmartNode<> generator{};
};

std::optional<NoiseType> noise_type_from_string(std::string_view value) noexcept {
  if (value == "arvane:noise/simplex_2d" || value == "simplex_2d" ||
      value == "open_simplex_2d") {
    return NoiseType::Simplex2D;
  }
  if (value == "arvane:noise/simplex_3d" || value == "simplex_3d" ||
      value == "open_simplex_3d") {
    return NoiseType::Simplex3D;
  }
  return std::nullopt;
}

NoiseSampler::NoiseSampler(const NoiseFieldConfig& config, u64 seed)
    : impl_(std::make_shared<Impl>()) {
  Impl& impl = *impl_;
  impl.config = config;
  impl.seed = static_cast<int>(salted_seed(seed, config.seed_salt) & 0x7FFFFFFFULL);
  auto simplex = FastNoise::New<FastNoise::Simplex>(FastSIMD::FeatureSet::Max);
  if (!simplex) {
    return;
  }
  impl.generator = simplex;
  if (config.octaves > 1U) {
    auto fractal = FastNoise::New<FastNoise::FractalFBm>(FastSIMD::FeatureSet::Max);
    if (fractal) {
      fractal->SetSource(simplex);
      fractal->SetOctaveCount(static_cast<int>(config.octaves));
      fractal->SetLacunarity(config.lacunarity);
      fractal->SetGain(config.gain);
      impl.generator = fractal;
    }
  }
}

f32 NoiseSampler::sample_2d(f32 x, f32 z) const noexcept {
  if (impl_ == nullptr || !impl_->generator) {
    return 0.0F;
  }
  return std::clamp(
      impl_->generator->GenSingle2D(
          x * impl_->config.frequency,
          z * impl_->config.frequency,
          impl_->seed) *
          impl_->config.amplitude,
      -1.0F,
      1.0F);
}

f32 NoiseSampler::sample_3d(f32 x, f32 y, f32 z) const noexcept {
  if (impl_ == nullptr || !impl_->generator) {
    return 0.0F;
  }
  return std::clamp(
      impl_->generator->GenSingle3D(
          x * impl_->config.frequency,
          y * impl_->config.frequency,
          z * impl_->config.frequency,
          impl_->seed) *
          impl_->config.amplitude,
      -1.0F,
      1.0F);
}

ChunkNoiseLattice2D::ChunkNoiseLattice2D(
    const NoiseSampler& sampler,
    i32 origin_x,
    i32 origin_z) {
  static_assert(ChunkSize % Step == 0);
  for (i32 grid_z = 0; grid_z < GridSide; ++grid_z) {
    for (i32 grid_x = 0; grid_x < GridSide; ++grid_x) {
      lattice_[grid_index(grid_x, grid_z)] = sampler.sample_2d(
          static_cast<f32>(origin_x + grid_x * Step),
          static_cast<f32>(origin_z + grid_z * Step));
    }
  }
}

f32 ChunkNoiseLattice2D::sample(i32 local_x, i32 local_z) const noexcept {
  const i32 grid_x = local_x / Step;
  const i32 grid_z = local_z / Step;
  const f32 t_x = static_cast<f32>(local_x % Step) / static_cast<f32>(Step);
  const f32 t_z = static_cast<f32>(local_z % Step) / static_cast<f32>(Step);
  return interpolate(
      lattice_[grid_index(grid_x, grid_z)],
      lattice_[grid_index(grid_x + 1, grid_z)],
      lattice_[grid_index(grid_x, grid_z + 1)],
      lattice_[grid_index(grid_x + 1, grid_z + 1)],
      t_x,
      t_z);
}

} // namespace arv::world::worldgen
