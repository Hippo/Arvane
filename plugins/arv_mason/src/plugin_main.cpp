#include "arv_mason/mason_plugin.hpp"

#include <memory>

namespace {

std::unique_ptr<arv_mason::MasonPlugin> g_mason;

} // namespace

ARV_MAIN(plugin) {
  g_mason = std::make_unique<arv_mason::MasonPlugin>(plugin);
  return g_mason->initialize();
}
