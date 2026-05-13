#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class WorldGen {
public:
  explicit constexpr WorldGen(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result register_feature_type(std::string_view id) const {
    return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_feature_type, id);
  }

  [[nodiscard]] Result register_noise_type(std::string_view id) const {
    return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_noise_type, id);
  }

  [[nodiscard]] Result register_density_function_type(std::string_view id) const {
    return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_density_function_type, id);
  }

private:
  using WorldGenMember = ArvResult (*)(void*, ArvResourceID);

  [[nodiscard]] Result call(WorldGenMember member, std::string_view id) const {
    if (api_ == nullptr || api_->worldgen == nullptr || member == nullptr) {
      return Result::unsupported("worldgen API is unavailable");
    }
    return Result{member(api_->worldgen_ctx, view(id))};
  }

  const ArvAPI* api_{};
};

} // namespace arv
