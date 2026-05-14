#include "arv/plugin.hpp"

namespace arv {

Plugin::Plugin(const ArvAPI* api) noexcept
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
      data_(api),
      config_(api),
      worldgen_(api),
      metrics_(api),
      permissions_(api),
      persistent_data_(api),
      network_(api),
      text_(api),
      regions_(api),
      gamemodes_(api),
      players_(api) {
}

bool Plugin::valid() const noexcept {
  return api_ != nullptr && api_->api_version == ARV_API_VERSION;
}

const ArvAPI* Plugin::api() const noexcept {
  return api_;
}

const Context& Plugin::context() const noexcept {
  return context_;
}

const Logger& Plugin::logger() const noexcept {
  return logger_;
}

const Registries& Plugin::registries() const noexcept {
  return registries_;
}

const Resources& Plugin::resources() const noexcept {
  return resources_;
}

const World& Plugin::world() const noexcept {
  return world_;
}

const Entities& Plugin::entities() const noexcept {
  return entities_;
}

const Events& Plugin::events() const noexcept {
  return events_;
}

const Scheduler& Plugin::scheduler() const noexcept {
  return scheduler_;
}

const Commands& Plugin::commands() const noexcept {
  return commands_;
}

const Services& Plugin::services() const noexcept {
  return services_;
}

const Data& Plugin::data() const noexcept {
  return data_;
}

const Config& Plugin::config() const noexcept {
  return config_;
}

const WorldGen& Plugin::worldgen() const noexcept {
  return worldgen_;
}

const Metrics& Plugin::metrics() const noexcept {
  return metrics_;
}

const Permissions& Plugin::permissions() const noexcept {
  return permissions_;
}

const PersistentData& Plugin::persistent_data() const noexcept {
  return persistent_data_;
}

const Network& Plugin::network() const noexcept {
  return network_;
}

const Text& Plugin::text() const noexcept {
  return text_;
}

const Regions& Plugin::regions() const noexcept {
  return regions_;
}

const GameModes& Plugin::gamemodes() const noexcept {
  return gamemodes_;
}

const Players& Plugin::players() const noexcept {
  return players_;
}

} // namespace arv
