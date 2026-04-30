#include "arv/net/transport.hpp"

#include <utility>

namespace arv::net {

std::vector<TransportMessage> ITransport::poll() {
  std::vector<TransportMessage> messages;
  std::vector<NetworkEvent> events = poll_events();
  messages.reserve(events.size());
  for (NetworkEvent& event : events) {
    if (event.type != NetworkEventType::MessageReceived) {
      continue;
    }
    messages.emplace_back(
        TransportMessage{event.connection_id, event.reliability, std::move(event.payload)});
  }
  return messages;
}

} // namespace arv::net
