#include "arv/protocol/item_stack_wire.hpp"

#include "arv/io/data_codecs.hpp"

#include <span>
#include <utility>
#include <vector>

namespace arv::protocol {

Expected<void, ProtocolError> write_item_stack(
    MessageWriter& writer,
    const player::ItemStack& stack) {
  writer.write<u32>(stack.item.value);
  writer.write<u16>(stack.count);
  writer.write_bool(player::item_stack_has_data(stack));
  if (!player::item_stack_has_data(stack)) {
    return Expected<void, ProtocolError>::success();
  }

  auto encoded = io::encode_binary(stack.data.value());
  if (!encoded) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"failed to encode item stack data: " + encoded.error().describe()});
  }
  if (encoded.value().size() > player::MaxItemStackDataBytes) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"item stack data exceeds wire limit"});
  }

  writer.write<u32>(static_cast<u32>(encoded.value().size()));
  writer.write_bytes(encoded.value());
  return Expected<void, ProtocolError>::success();
}

Expected<player::ItemStack, ProtocolError> read_item_stack(MessageReader& reader) {
  auto item = reader.read<u32>();
  auto count = reader.read<u16>();
  auto has_data = reader.read_bool();
  if (!item || !count || !has_data) {
    return Expected<player::ItemStack, ProtocolError>::failure(
        ProtocolError{"failed to decode item stack"});
  }

  player::ItemStack stack{player::ItemID{item.value()}, count.value()};
  if (!has_data.value()) {
    return Expected<player::ItemStack, ProtocolError>::success(stack);
  }

  auto data_size = reader.read<u32>();
  if (!data_size) {
    return Expected<player::ItemStack, ProtocolError>::failure(
        ProtocolError{"failed to decode item stack data size"});
  }
  if (data_size.value() > player::MaxItemStackDataBytes) {
    return Expected<player::ItemStack, ProtocolError>::failure(
        ProtocolError{"item stack data exceeds wire limit"});
  }

  std::vector<u8> bytes(data_size.value());
  auto data_bytes = reader.read_bytes(std::span<u8>{bytes.data(), bytes.size()});
  if (!data_bytes) {
    return Expected<player::ItemStack, ProtocolError>::failure(data_bytes.error());
  }
  auto decoded = io::decode_binary(bytes);
  if (!decoded) {
    return Expected<player::ItemStack, ProtocolError>::failure(
        ProtocolError{"failed to decode item stack data: " + decoded.error().describe()});
  }
  stack.data = std::move(decoded.value());
  player::normalize_item_stack_data(stack);
  return Expected<player::ItemStack, ProtocolError>::success(std::move(stack));
}

} // namespace arv::protocol
