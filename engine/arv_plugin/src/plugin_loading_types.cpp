#include "arv/plugin/plugin_loading.hpp"

#include <algorithm>
#include <utility>

namespace arv::plugin {

bool NativePluginPolicy::allows(std::string_view pack_id) const noexcept {
  return std::find(allowed_packs.begin(), allowed_packs.end(), pack_id) !=
         allowed_packs.end();
}

NativePluginPolicy make_native_plugin_policy(
    bool enabled,
    std::vector<std::string> allowed_packs) {
  return {enabled, std::move(allowed_packs)};
}

} // namespace arv::plugin
