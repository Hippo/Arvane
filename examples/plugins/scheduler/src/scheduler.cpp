#include <arv/plugin.hpp>

namespace {

arv::Job tick{[] {}};

}

ARV_MAIN(plugin) {
  return static_cast<bool>(plugin.scheduler().sync_repeat_tick(1, 20, tick));
}
