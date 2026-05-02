#pragma once

#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <vector>

namespace arv::client {

class ClientChunkInbox {
public:
  void push_chunk_data(protocol::ChunkData data);
  void push_chunk_unload(protocol::ChunkUnload unload);
  void push_block_updates(protocol::BlockUpdateBatch updates);

  [[nodiscard]] std::vector<protocol::ChunkData> drain_chunk_data();
  [[nodiscard]] std::vector<protocol::ChunkUnload> drain_chunk_unloads();
  [[nodiscard]] std::vector<protocol::BlockUpdateBatch> drain_block_updates();

  void clear() noexcept;

private:
  std::vector<protocol::ChunkData> received_chunk_data_{};
  std::vector<protocol::ChunkUnload> received_chunk_unloads_{};
  std::vector<protocol::BlockUpdateBatch> received_block_updates_{};
};

} // namespace arv::client
