#include "arv/base/base_module.hpp"

namespace arv::base {

voxel::FluidDefinition make_base_water_fluid_definition(
    voxel::FluidTypeID type) noexcept {
  voxel::FluidDefinition definition;
  definition.type = type;
  return definition;
}

} // namespace arv::base
