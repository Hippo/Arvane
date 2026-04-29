#pragma once

#include "arv/io/config.hpp"

#include <filesystem>

namespace arv::desktop {

[[nodiscard]] io::ConfigLoadOptions parse_config_options(int argc, char** argv);

[[nodiscard]] std::filesystem::path default_save_root(const io::EngineConfig& config);

} // namespace arv::desktop
