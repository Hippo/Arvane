#pragma once

#include "arv/api.h"
#include "arv/commands.hpp"
#include "arv/config.hpp"
#include "arv/context.hpp"
#include "arv/entities.hpp"
#include "arv/events.hpp"
#include "arv/logger.hpp"
#include "arv/metrics.hpp"
#include "arv/network.hpp"
#include "arv/permissions.hpp"
#include "arv/persistent_data.hpp"
#include "arv/registries.hpp"
#include "arv/resources.hpp"
#include "arv/scheduler.hpp"
#include "arv/services.hpp"
#include "arv/text.hpp"
#include "arv/world.hpp"
#include "arv/worldgen.hpp"

namespace arv {

class Plugin {
public:
  explicit constexpr Plugin(const ArvAPI* api) noexcept
      : api_(api),
        context_(api),
        logger_(api),
        registries_(api),
        resources_(api),
        world_(api),
        entities_(api),
        events_(api),
        scheduler_(api),
        commands_(api),
        services_(api),
        config_(api),
        worldgen_(api),
        metrics_(api),
        permissions_(api),
        persistent_data_(api),
        network_(api),
        text_(api) {
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    return api_ != nullptr && api_->api_version == ARV_API_VERSION;
  }

  [[nodiscard]] constexpr const ArvAPI* api() const noexcept {
    return api_;
  }

  [[nodiscard]] constexpr const Context& context() const noexcept {
    return context_;
  }

  [[nodiscard]] constexpr const Logger& logger() const noexcept {
    return logger_;
  }

  [[nodiscard]] constexpr const Registries& registries() const noexcept {
    return registries_;
  }

  [[nodiscard]] constexpr const Resources& resources() const noexcept {
    return resources_;
  }

  [[nodiscard]] constexpr const World& world() const noexcept {
    return world_;
  }

  [[nodiscard]] constexpr const Entities& entities() const noexcept {
    return entities_;
  }

  [[nodiscard]] constexpr const Events& events() const noexcept {
    return events_;
  }

  [[nodiscard]] constexpr const Scheduler& scheduler() const noexcept {
    return scheduler_;
  }

  [[nodiscard]] constexpr const Commands& commands() const noexcept {
    return commands_;
  }

  [[nodiscard]] constexpr const Services& services() const noexcept {
    return services_;
  }

  [[nodiscard]] constexpr const Config& config() const noexcept {
    return config_;
  }

  [[nodiscard]] constexpr const WorldGen& worldgen() const noexcept {
    return worldgen_;
  }

  [[nodiscard]] constexpr const Metrics& metrics() const noexcept {
    return metrics_;
  }

  [[nodiscard]] constexpr const Permissions& permissions() const noexcept {
    return permissions_;
  }

  [[nodiscard]] constexpr const PersistentData& persistent_data() const noexcept {
    return persistent_data_;
  }

  [[nodiscard]] constexpr const Network& network() const noexcept {
    return network_;
  }

  [[nodiscard]] constexpr const Text& text() const noexcept {
    return text_;
  }

private:
  const ArvAPI* api_{};
  Context context_;
  Logger logger_;
  Registries registries_;
  Resources resources_;
  World world_;
  Entities entities_;
  Events events_;
  Scheduler scheduler_;
  Commands commands_;
  Services services_;
  Config config_;
  WorldGen worldgen_;
  Metrics metrics_;
  Permissions permissions_;
  PersistentData persistent_data_;
  Network network_;
  Text text_;
};

} // namespace arv

#define ARV_MAIN(plugin_variable)                                        \
  static bool arvane_main(arv::Plugin& plugin_variable);                 \
  extern "C" ARV_EXPORT bool arvane_plugin_main(const ArvAPI* arv_api) { \
    if (arv_api == nullptr || arv_api->api_version != ARV_API_VERSION) { \
      return false;                                                      \
    }                                                                    \
    arv::Plugin plugin_variable{arv_api};                                \
    return arvane_main(plugin_variable);                                 \
  }                                                                      \
  static bool arvane_main(arv::Plugin& plugin_variable)
