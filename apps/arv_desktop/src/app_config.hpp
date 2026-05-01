#pragma once

#include "arv/io/config.hpp"

namespace arv::desktop {

[[nodiscard]] io::ConfigLoadOptions parse_config_options(int argc, char** argv);

} // namespace arv::desktop
