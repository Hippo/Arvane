#pragma once

#include "arv/core/types.hpp"

#include <array>
#include <chrono>

namespace arv::world::worldgen {

enum class WorldGenerationStage : u8 {
  TerrainProfile,
  SurfaceSelection,
  BaseFillAndSurface,
  Carvers,
  Features,
  Structures,
  Count,
};

inline constexpr std::array<const char*, static_cast<usize>(WorldGenerationStage::Count)>
    WorldGenerationStageNames{{
        "terrain_profile",
        "surface_selection",
        "base_fill_surface",
        "carvers",
        "features",
        "structures",
    }};

struct WorldGenerationSectionStats {
  u64 generated_sections{};
  u64 skipped_sections{};
  u64 all_air_sections{};
  u64 bulk_solid_sections{};
  u64 bulk_fluid_sections{};
  u64 mixed_sections{};
};

struct WorldGenerationCandidateStats {
  u64 placed_features_considered{};
  u64 feature_origin_chunks_considered{};
  u64 feature_candidates_considered{};
  u64 feature_candidates_rejected{};
  u64 feature_candidates_applied{};
  u64 structure_cells_inspected{};
  u64 structure_candidates_applied{};
};

struct WorldGenerationNoiseStats {
  u64 terrain_lattice_samples{};
  u64 climate_lattice_samples{};
  u64 direct_2d_noise_samples{};
  u64 direct_3d_noise_samples{};
};

struct WorldGenerationStats {
  u64 total_ns{};
  std::array<u64, static_cast<usize>(WorldGenerationStage::Count)> stage_ns{};
  WorldGenerationSectionStats sections{};
  WorldGenerationCandidateStats candidates{};
  WorldGenerationNoiseStats noise{};

  void reset() noexcept {
    *this = {};
  }

  void add_stage_time(WorldGenerationStage stage, u64 nanoseconds) noexcept {
    stage_ns[static_cast<usize>(stage)] += nanoseconds;
  }
};

class ScopedWorldGenerationStageTimer {
public:
  ScopedWorldGenerationStageTimer(
      WorldGenerationStats* stats,
      WorldGenerationStage stage) noexcept
      : stats_(stats),
        stage_(stage),
        start_(std::chrono::steady_clock::now()) {
  }

  ScopedWorldGenerationStageTimer(const ScopedWorldGenerationStageTimer&) = delete;
  ScopedWorldGenerationStageTimer& operator=(const ScopedWorldGenerationStageTimer&) = delete;

  ~ScopedWorldGenerationStageTimer() {
    if (stats_ == nullptr) {
      return;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    stats_->add_stage_time(
        stage_,
        static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
  }

private:
  WorldGenerationStats* stats_{};
  WorldGenerationStage stage_{};
  std::chrono::steady_clock::time_point start_{};
};

} // namespace arv::world::worldgen
