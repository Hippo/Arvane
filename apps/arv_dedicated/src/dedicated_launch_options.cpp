#include "dedicated_launch_options.hpp"

#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace arv::dedicated {
namespace {

[[nodiscard]] std::filesystem::path executable_directory(int argc, char** argv) {
  if (argc <= 0 || argv == nullptr || argv[0] == nullptr || std::string_view{argv[0]}.empty()) {
    return std::filesystem::current_path();
  }

  std::filesystem::path executable_path{argv[0]};
  if (executable_path.is_relative()) {
    executable_path = std::filesystem::current_path() / executable_path;
  }
  std::error_code ec;
  std::filesystem::path canonical = std::filesystem::weakly_canonical(executable_path, ec);
  if (ec) {
    canonical = executable_path.lexically_normal();
  }
  return canonical.has_parent_path() ? canonical.parent_path() : std::filesystem::current_path();
}

} // namespace

DedicatedLaunchOptions parse_launch_options(int argc, char** argv) {
  DedicatedLaunchOptions options;
  options.config.profile = io::ConfigProfile::Server;
  bool explicit_config = false;
  bool explicit_project_root = false;
  bool explicit_server_root = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ticks") {
      options.max_ticks = static_cast<u64>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--config") {
      options.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--server-root") {
      options.config.explicit_server_root_path = std::filesystem::path{argv[++i]};
      explicit_server_root = true;
    } else if (arg == "--project-root") {
      options.config.project_root = std::filesystem::path{argv[++i]};
      explicit_project_root = true;
    }
  }

  if (!explicit_config && !explicit_project_root && !explicit_server_root) {
    options.config.explicit_server_root_path = executable_directory(argc, argv);
  }
  options.config.missing_config_is_error = explicit_config;
  return options;
}

} // namespace arv::dedicated
