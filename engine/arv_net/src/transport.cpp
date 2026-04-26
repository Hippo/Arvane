#include "arv/net/transport.hpp"

#include <utility>

namespace arv::net {

std::vector<TransportMessage> ITransport::poll() {
  std::vector<TransportMessage> messages;
  for (NetworkEvent& event : poll_events()) {
    if (event.type != NetworkEventType::MessageReceived) {
      continue;
    }
    messages.push_back({event.connection_id,
                        event.reliability,
                        std::move(event.payload)});
  }
  return messages;
}

} // namespace arv::net
