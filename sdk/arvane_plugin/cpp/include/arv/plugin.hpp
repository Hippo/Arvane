#pragma once

#include "arv/api.h"
#include "arv/commands.hpp"
#include "arv/config.hpp"
#include "arv/context.hpp"
#include "arv/data.hpp"
#include "arv/entities.hpp"
#include "arv/events.hpp"
#include "arv/gamemode.hpp"
#include "arv/logger.hpp"
#include "arv/metrics.hpp"
#include "arv/network.hpp"
#include "arv/permissions.hpp"
#include "arv/persistent_data.hpp"
#include "arv/players.hpp"
#include "arv/registries.hpp"
#include "arv/regions.hpp"
#include "arv/resources.hpp"
#include "arv/scheduler.hpp"
#include "arv/services.hpp"
#include "arv/text.hpp"
#include "arv/world.hpp"
#include "arv/worldgen.hpp"

namespace arv {

class Plugin {
public:
  explicit Plugin(const ArvAPI* api) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] const ArvAPI* api() const noexcept;

  [[nodiscard]] const Context& context() const noexcept;

  [[nodiscard]] const Logger& logger() const noexcept;

  [[nodiscard]] const Registries& registries() const noexcept;

  [[nodiscard]] const Resources& resources() const noexcept;

  [[nodiscard]] const World& world() const noexcept;

  [[nodiscard]] const Entities& entities() const noexcept;

  [[nodiscard]] const Events& events() const noexcept;

  [[nodiscard]] const Scheduler& scheduler() const noexcept;

  [[nodiscard]] const Commands& commands() const noexcept;

  [[nodiscard]] const Services& services() const noexcept;

  [[nodiscard]] const Data& data() const noexcept;

  [[nodiscard]] const Config& config() const noexcept;

  [[nodiscard]] const WorldGen& worldgen() const noexcept;

  [[nodiscard]] const Metrics& metrics() const noexcept;

  [[nodiscard]] const Permissions& permissions() const noexcept;

  [[nodiscard]] const PersistentData& persistent_data() const noexcept;

  [[nodiscard]] const Network& network() const noexcept;

  [[nodiscard]] const Text& text() const noexcept;

  [[nodiscard]] const Regions& regions() const noexcept;

  [[nodiscard]] const GameModes& gamemodes() const noexcept;

  [[nodiscard]] const Players& players() const noexcept;

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
  Data data_;
  Config config_;
  WorldGen worldgen_;
  Metrics metrics_;
  Permissions permissions_;
  PersistentData persistent_data_;
  Network network_;
  Text text_;
  Regions regions_;
  GameModes gamemodes_;
  Players players_;
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
