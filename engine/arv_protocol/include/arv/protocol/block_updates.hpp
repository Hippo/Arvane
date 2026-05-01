#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/voxel/block.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

struct BlockUpdate {
  BlockPos pos{};
  voxel::BlockVariantID new_state{};
  u32 update_flags{};
  u64 server_tick{};
};

struct BlockUpdateBatch {
  WorldID world_id{};
  u64 server_tick{};
  std::vector<BlockUpdate> updates{};
};

inline constexpr u32 MaxBlockUpdatesPerBatch = 4096;

std::vector<u8> serialize_block_update_batch(const BlockUpdateBatch& batch);
Expected<BlockUpdateBatch, ProtocolError> deserialize_block_update_batch(std::span<const u8> bytes);

} // namespace arv::protocol
