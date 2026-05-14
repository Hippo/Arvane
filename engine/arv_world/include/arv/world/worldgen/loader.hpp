#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/world/worldgen/registry.hpp"

namespace arv::world::worldgen {

[[nodiscard]] RegistryResult load_worldgen_registry_from_packs(
    const asset::PackManager& packs);

} // namespace arv::world::worldgen
