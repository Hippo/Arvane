#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/voxel/block.hpp"

#include <optional>

namespace arv::base {

voxel::FluidDefinition make_base_water_fluid_definition(
    voxel::FluidTypeID type) noexcept {
  voxel::FluidDefinition definition;
  definition.type = type;
  return definition;
}

Expected<void, std::string> register_base_fluid_render_data(
    render::FluidRenderData& render_data,
    const voxel::BlockRegistry& block_registry) {
  const std::optional<voxel::BlockVariantID> water_variant =
      block_registry.find_variant(ids::WaterBlockID);
  if (!water_variant.has_value()) {
    return Expected<void, std::string>::failure(
        "missing base fluid render block variant: " + std::string{ids::WaterBlockID});
  }

  render_data.set_fluid_variant(
      voxel::DefaultWaterFluidTypeID,
      water_variant.value());
  return Expected<void, std::string>::success();
}

} // namespace arv::base
