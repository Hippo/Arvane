#pragma once

#include "arv/net/transport.hpp"

#include <span>

namespace arv::testsupport {

inline const net::NetworkEvent* find_event(
    std::span<const net::NetworkEvent> events,
    net::NetworkEventType type) {
  for (const net::NetworkEvent& event : events) {
    if (event.type == type) {
      return &event;
    }
  }
  return nullptr;
}

} // namespace arv::testsupport
