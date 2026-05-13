#include <arv/plugin.hpp>

namespace {

int provider_value = 7;

}

ARV_MAIN(plugin) {
  return static_cast<bool>(plugin.services().publish(
      "service_provider:example",
      1,
      provider_value,
      nullptr,
      "Example Service",
      arv::ServiceThreading::AnyThread));
}
