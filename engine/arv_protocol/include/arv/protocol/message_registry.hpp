#pragma once

#include "arv/protocol/message_type.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace arv::protocol {

struct MessageDescriptor {
  MessageType type{};
  std::string name{};
  MessageDirection direction{MessageDirection::Bidirectional};
  ReliabilityClass reliability{ReliabilityClass::ReliableOrdered};
};

class MessageRegistry {
public:
  bool register_message(MessageDescriptor descriptor) {
    const MessageType type = descriptor.type;
    return descriptors_.emplace(type, std::move(descriptor)).second;
  }

  [[nodiscard]] std::optional<MessageDescriptor> lookup(MessageType type) const {
    const auto found = descriptors_.find(type);
    if (found == descriptors_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  [[nodiscard]] usize size() const noexcept {
    return descriptors_.size();
  }

  static MessageRegistry make_core_registry() {
    MessageRegistry registry;
    registry.register_message({MessageType::ClientHello, "ClientHello",
                               MessageDirection::ClientToServer, ReliabilityClass::ReliableOrdered});
    registry.register_message({MessageType::ServerHello, "ServerHello",
                               MessageDirection::ServerToClient, ReliabilityClass::ReliableOrdered});
    registry.register_message({MessageType::DisconnectReason, "DisconnectReason",
                               MessageDirection::Bidirectional, ReliabilityClass::ReliableOrdered});
    registry.register_message({MessageType::Ping, "Ping",
                               MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent});
    registry.register_message({MessageType::Pong, "Pong",
                               MessageDirection::Bidirectional, ReliabilityClass::UnreliableEvent});
    return registry;
  }

private:
  std::unordered_map<MessageType, MessageDescriptor> descriptors_;
};

} // namespace arv::protocol
