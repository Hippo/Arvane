#include <arv/plugin.hpp>

ARV_MAIN(plugin) {
  auto service = plugin.services().get("service_provider:example", 1, 1);
  if (service) {
    plugin.logger().info("Found example service.");
  }
  return true;
}
