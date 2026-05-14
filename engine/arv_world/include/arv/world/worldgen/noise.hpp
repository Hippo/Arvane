#pragma once

#include "arv/world/worldgen/common.hpp"

#include <array>
#include <memory>
#include <string>

namespace arv::world::worldgen {

enum class NoiseType : u8 {
  Simplex2D,
  Simplex3D,
};

struct NoiseFieldConfig {
  NoiseType type{NoiseType::Simplex2D};
  std::string type_id{"arvane:noise/simplex_2d"};
  std::string seed_salt{};
  f32 frequency{0.01F};
  u32 octaves{1};
  f32 amplitude{1.0F};
  f32 lacunarity{2.0F};
  f32 gain{0.5F};
};

struct NoiseFileConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  HashMap<std::string, NoiseFieldConfig> fields{};
};

class NoiseSampler {
public:
  NoiseSampler() = default;
  NoiseSampler(const NoiseFieldConfig& config, u64 seed);

  [[nodiscard]] f32 sample_2d(f32 x, f32 z) const noexcept;
  [[nodiscard]] f32 sample_3d(f32 x, f32 y, f32 z) const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_{};
};

class ChunkNoiseLattice2D {
public:
  static constexpr i32 Step = DefaultLatticeStep;
  static constexpr i32 GridSide = (ChunkSize / Step) + 1;

  ChunkNoiseLattice2D() = default;
  ChunkNoiseLattice2D(const NoiseSampler& sampler, i32 origin_x, i32 origin_z);

  [[nodiscard]] f32 sample(i32 local_x, i32 local_z) const noexcept;

private:
  [[nodiscard]] static constexpr usize grid_index(i32 grid_x, i32 grid_z) noexcept {
    return static_cast<usize>(grid_x) + static_cast<usize>(GridSide) * static_cast<usize>(grid_z);
  }

  std::array<f32, static_cast<usize>(GridSide * GridSide)> lattice_{};
};

[[nodiscard]] std::optional<NoiseType> noise_type_from_string(std::string_view value) noexcept;

} // namespace arv::world::worldgen
