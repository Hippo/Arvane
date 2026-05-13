#include <arv/plugin.hpp>

ARV_MAIN(plugin) {
  plugin.logger().info("C++ plugin loaded.");
  return true;
}
