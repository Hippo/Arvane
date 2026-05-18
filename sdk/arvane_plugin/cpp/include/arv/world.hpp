#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

#include <cstdint>

namespace arv {

class World {
public:
  explicit World(const ArvAPI* api) noexcept;

  [[nodiscard]] ValueResult<std::uint32_t> get_block(
      ArvWorldHandle world,
      ArvBlockPos pos) const;

  [[nodiscard]] Result set_block(
      ArvWorldHandle world,
      ArvBlockPos pos,
      std::uint32_t block_variant) const;

  [[nodiscard]] ValueResult<ArvTickIndex> current_tick(ArvWorldHandle world) const;

  [[nodiscard]] ValueResult<std::uint64_t> seed(ArvWorldHandle world) const;

  [[nodiscard]] ValueResult<ArvLocation> get_world_spawn(
      ArvWorldHandle world) const;

  [[nodiscard]] Result set_world_spawn(
      ArvWorldHandle world,
      ArvLocation spawn) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
