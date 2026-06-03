#pragma once

#include "arv/player/item.hpp"
#include "arv/protocol/message_writer.hpp"

namespace arv::protocol {

[[nodiscard]] Expected<void, ProtocolError> write_item_stack(
    MessageWriter& writer,
    const player::ItemStack& stack);
[[nodiscard]] Expected<player::ItemStack, ProtocolError> read_item_stack(MessageReader& reader);

} // namespace arv::protocol
