#include "arv/server/server_runtime.hpp"

#include <utility>

namespace arv::server {

BlockBreakBehaviorRegistry::Result BlockBreakBehaviorRegistry::register_handler(
    std::string id,
    Handler handler) {
  if (id.empty()) {
    return Result::failure({"block break behavior id is empty"});
  }
  if (!handler) {
    return Result::failure({"block break behavior handler is empty: " + id});
  }
  if (contains(id)) {
    return Result::failure({"duplicate block break behavior id: " + id});
  }
  handlers_.push_back({std::move(id), std::move(handler)});
  return Result::success();
}

const BlockBreakBehaviorRegistry::Handler* BlockBreakBehaviorRegistry::find(
    std::string_view id) const noexcept {
  for (const auto& [handler_id, handler] : handlers_) {
    if (handler_id == id) {
      return &handler;
    }
  }
  return nullptr;
}

bool BlockBreakBehaviorRegistry::contains(std::string_view id) const noexcept {
  return find(id) != nullptr;
}

} // namespace arv::server
