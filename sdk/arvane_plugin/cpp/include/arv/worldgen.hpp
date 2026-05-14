#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class WorldGen {
public:
  explicit WorldGen(const ArvAPI* api) noexcept;

  [[nodiscard]] Result register_feature_type(std::string_view id) const;

  [[nodiscard]] Result register_noise_type(std::string_view id) const;

  [[nodiscard]] Result register_density_function_type(std::string_view id) const;

private:
  using WorldGenMember = ArvResult (*)(void*, ArvResourceID);

  [[nodiscard]] Result call(WorldGenMember member, std::string_view id) const;

  const ArvAPI* api_{};
};

} // namespace arv
