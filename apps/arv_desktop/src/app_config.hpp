#pragma once

#include "arv/io/config.hpp"

#include <filesystem>

namespace arv::desktop {

struct DesktopChunkStreamingValidationOptions {
  bool enabled{};
  double duration_seconds{8.0};
  double fly_speed_multiplier{10.0};
  std::filesystem::path report_path{};
  bool cleanup_save{true};
};

struct DesktopLaunchOptions {
  io::ConfigLoadOptions config{};
  DesktopChunkStreamingValidationOptions chunk_streaming_validation{};
};

[[nodiscard]] DesktopLaunchOptions parse_desktop_launch_options(int argc, char** argv);
[[nodiscard]] io::ConfigLoadOptions parse_config_options(int argc, char** argv);

} // namespace arv::desktop
