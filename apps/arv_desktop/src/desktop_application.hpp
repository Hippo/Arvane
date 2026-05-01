#pragma once

namespace arv::desktop {

class DesktopApplication {
public:
  [[nodiscard]] int run(int argc, char** argv);
};

[[nodiscard]] int run_desktop_client(int argc, char** argv);

} // namespace arv::desktop
