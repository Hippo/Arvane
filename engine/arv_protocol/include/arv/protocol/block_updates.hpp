#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <span>
#include <vector>

namespace arv::protocol {

struct BlockUpdate {
  BlockPos pos{};
  voxel::BlockVariantID new_state{};
  u32 update_flags{};
  u64 server_tick{};
  bool has_fluid{};
  voxel::FluidCell new_fluid{};
};

struct ClientboundBlockUpdateBatch {
  WorldID world_id{};
  u64 server_tick{};
  std::vector<BlockUpdate> updates{};
};

inline constexpr u32 MaxBlockUpdatesPerBatch = 4096;

std::vector<u8> serialize_clientbound_block_update_batch(const ClientboundBlockUpdateBatch& batch);
Expected<ClientboundBlockUpdateBatch, ProtocolError> deserialize_clientbound_block_update_batch(std::span<const u8> bytes);

} // namespace arv::protocol
