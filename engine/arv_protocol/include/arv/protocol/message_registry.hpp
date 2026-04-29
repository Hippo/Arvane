#pragma once

#include "arv/core/types.hpp"
#include "arv/protocol/message_type.hpp"

#include <optional>
#include <string>

namespace arv::protocol {

struct MessageDescriptor {
  MessageType type{};
  std::string name{};
  MessageDirection direction{MessageDirection::Bidirectional};
  ReliabilityClass reliability{ReliabilityClass::ReliableOrdered};
};

class MessageRegistry {
public:
  bool register_message(MessageDescriptor descriptor);

  [[nodiscard]] std::optional<MessageDescriptor> lookup(MessageType type) const;

  [[nodiscard]] usize size() const noexcept {
    return descriptors_.size();
  }

  static MessageRegistry make_core_registry();

private:
  HashMap<MessageType, MessageDescriptor> descriptors_;
};

} // namespace arv::protocol
