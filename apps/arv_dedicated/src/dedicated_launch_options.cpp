#include "dedicated_launch_options.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace arv::dedicated {

DedicatedLaunchOptions parse_launch_options(int argc, char** argv) {
  DedicatedLaunchOptions options;
  options.config.profile = io::ConfigProfile::Server;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ticks") {
      options.max_ticks = static_cast<u64>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--config") {
      options.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.config.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.config.missing_config_is_error = explicit_config;
  return options;
}

} // namespace arv::dedicated
