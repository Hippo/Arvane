#pragma once

#include "arvane/plugin/logger.hpp"
#include "arvane/plugin/scheduler.hpp"
#include "arvane/plugin/services.hpp"

namespace arv::plugin {

class Plugin {
public:
  explicit constexpr Plugin(const ArvPluginAPI* api) noexcept
      : api_(api),
        logger_(api),
        scheduler_(api),
        services_(api) {
  }

  [[nodiscard]] constexpr const ArvPluginAPI* api() const noexcept {
    return api_;
  }

  [[nodiscard]] constexpr const Logger& logger() const noexcept {
    return logger_;
  }

  [[nodiscard]] constexpr const Scheduler& scheduler() const noexcept {
    return scheduler_;
  }

  [[nodiscard]] constexpr const Services& services() const noexcept {
    return services_;
  }

private:
  const ArvPluginAPI* api_{};
  Logger logger_;
  Scheduler scheduler_;
  Services services_;
};

} // namespace arv::plugin
