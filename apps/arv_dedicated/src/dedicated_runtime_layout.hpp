#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/io/config.hpp"

namespace arv::dedicated {

[[nodiscard]] Expected<void, DedicatedError> ensure_dedicated_runtime_layout(
    const io::EngineConfig& config);

} // namespace arv::dedicated
