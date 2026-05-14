#include "arv/worldgen.hpp"

namespace arv {

WorldGen::WorldGen(const ArvAPI* api) noexcept : api_(api) {
}

Result WorldGen::register_feature_type(std::string_view id) const {
  return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_feature_type, id);
}

Result WorldGen::register_noise_type(std::string_view id) const {
  return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_noise_type, id);
}

Result WorldGen::register_density_function_type(std::string_view id) const {
  return call(api_ == nullptr || api_->worldgen == nullptr ? nullptr : api_->worldgen->register_density_function_type, id);
}

Result WorldGen::call(WorldGenMember member, std::string_view id) const {
  if (api_ == nullptr || api_->worldgen == nullptr || member == nullptr) {
    return Result::unsupported("worldgen API is unavailable");
  }
  return Result{member(api_->worldgen_ctx, view(id))};
}

} // namespace arv
