#include "arv/protocol/block_updates.hpp"

#include <utility>

namespace arv::protocol {
namespace {

void write_block_pos(MessageWriter& writer, BlockPos pos) {
  writer.write<i32>(pos.value.x);
  writer.write<i32>(pos.value.y);
  writer.write<i32>(pos.value.z);
}

Expected<BlockPos, ProtocolError> read_block_pos(MessageReader& reader) {
  auto x = reader.read<i32>();
  auto y = reader.read<i32>();
  auto z = reader.read<i32>();
  if (!x || !y || !z) {
    return Expected<BlockPos, ProtocolError>::failure(ProtocolError{"failed to decode block position"});
  }
  return Expected<BlockPos, ProtocolError>::success({{x.value(), y.value(), z.value()}});
}

Expected<void, ProtocolError> require_no_trailing_bytes(MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(ProtocolError{"trailing bytes in block update message"});
  }
  return Expected<void, ProtocolError>::success();
}

} // namespace

std::vector<u8> serialize_block_update_batch(const BlockUpdateBatch& batch) {
  MessageWriter writer;
  writer.write<u32>(batch.world_id.value);
  writer.write<u64>(batch.server_tick);
  writer.write<u32>(static_cast<u32>(batch.updates.size()));
  for (const BlockUpdate& update : batch.updates) {
    write_block_pos(writer, update.pos);
    writer.write<u32>(update.new_state.value);
    writer.write<u32>(update.update_flags);
    writer.write<u64>(update.server_tick);
  }
  return std::move(writer).take_bytes();
}

Expected<BlockUpdateBatch, ProtocolError> deserialize_block_update_batch(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto server_tick = reader.read<u64>();
  auto count = reader.read<u32>();
  if (!world_id || !server_tick || !count) {
    return Expected<BlockUpdateBatch, ProtocolError>::failure(
        ProtocolError{"failed to decode block update batch header"});
  }
  if (count.value() > MaxBlockUpdatesPerBatch) {
    return Expected<BlockUpdateBatch, ProtocolError>::failure(
        ProtocolError{"too many block updates in batch"});
  }

  BlockUpdateBatch batch;
  batch.world_id = WorldID{world_id.value()};
  batch.server_tick = server_tick.value();
  batch.updates.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto pos = read_block_pos(reader);
    auto state = reader.read<u32>();
    auto flags = reader.read<u32>();
    auto tick = reader.read<u64>();
    if (!pos || !state || !flags || !tick) {
      return Expected<BlockUpdateBatch, ProtocolError>::failure(
          ProtocolError{"failed to decode block update entry"});
    }
    batch.updates.push_back({
        pos.value(),
        voxel::BlockVariantID{state.value()},
        flags.value(),
        tick.value()});
  }
  auto trailing = require_no_trailing_bytes(reader);
  if (!trailing) {
    return Expected<BlockUpdateBatch, ProtocolError>::failure(trailing.error());
  }
  return Expected<BlockUpdateBatch, ProtocolError>::success(std::move(batch));
}

} // namespace arv::protocol
