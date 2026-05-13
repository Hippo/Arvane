#include <arv/plugin.hpp>

namespace {

arv::Job tick{[] {}};

}

ARV_MAIN(plugin) {
  return static_cast<bool>(plugin.scheduler().sync_repeat(1, 20, tick));
}
