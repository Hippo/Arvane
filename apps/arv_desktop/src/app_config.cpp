#include "app_config.hpp"

#include <string_view>

namespace arv::desktop {

io::ConfigLoadOptions parse_config_options(int argc, char** argv) {
  io::ConfigLoadOptions options;
  options.profile = io::ConfigProfile::Client;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--config") {
      options.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.missing_config_is_error = explicit_config;
  return options;
}

std::filesystem::path default_save_root(const io::EngineConfig& config) {
  return config.paths.server_save_path(io::DefaultServerSaveName);
}

} // namespace arv::desktop
