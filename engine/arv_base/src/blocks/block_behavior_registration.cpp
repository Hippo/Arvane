#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/voxel/block.hpp"

#include <cassert>
#include <string>

namespace arv::base {

void register_base_block_behaviors(voxel::BlockBehaviorRegistry& registry) {
  auto fluid_result = registry.register_handler(std::string{ids::FluidBlockBehaviorID});
  assert(fluid_result && "base fluid block behavior registration failed");
  (void)fluid_result;
}

} // namespace arv::base
