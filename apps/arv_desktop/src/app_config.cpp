#include "app_config.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace arv::desktop {
namespace {

[[nodiscard]] double parse_positive_double_or(
    std::string_view text,
    double fallback) {
  std::string copy{text};
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(copy.c_str(), &end);
  if (end != copy.c_str() + copy.size() ||
      errno == ERANGE ||
      !std::isfinite(value) ||
      value <= 0.0) {
    return fallback;
  }
  return value;
}

} // namespace

DesktopLaunchOptions parse_desktop_launch_options(int argc, char** argv) {
  DesktopLaunchOptions launch;
  launch.config.profile = io::ConfigProfile::Client;
  bool explicit_config = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--validate-chunk-streaming") {
      launch.chunk_streaming_validation.enabled = true;
    } else if (arg == "--chunk-validation-keep-save") {
      launch.chunk_streaming_validation.cleanup_save = false;
    } else if (arg == "--config" && i + 1 < argc) {
      launch.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home" && i + 1 < argc) {
      launch.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root" && i + 1 < argc) {
      launch.config.project_root = std::filesystem::path{argv[++i]};
    } else if (arg == "--chunk-validation-seconds" && i + 1 < argc) {
      launch.chunk_streaming_validation.duration_seconds =
          parse_positive_double_or(
              argv[++i],
              launch.chunk_streaming_validation.duration_seconds);
    } else if (arg == "--chunk-validation-flyspeed" && i + 1 < argc) {
      launch.chunk_streaming_validation.fly_speed_multiplier =
          parse_positive_double_or(
              argv[++i],
              launch.chunk_streaming_validation.fly_speed_multiplier);
    } else if (arg == "--chunk-validation-report" && i + 1 < argc) {
      launch.chunk_streaming_validation.report_path = std::filesystem::path{argv[++i]};
    }
  }

  launch.config.missing_config_is_error = explicit_config;
  return launch;
}

io::ConfigLoadOptions parse_config_options(int argc, char** argv) {
  return parse_desktop_launch_options(argc, argv).config;
}

} // namespace arv::desktop
