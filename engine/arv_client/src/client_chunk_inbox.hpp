#pragma once

#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <vector>

namespace arv::client {

class ClientChunkInbox {
public:
  void push_chunk_section_data(protocol::ChunkSectionData data);
  void push_chunk_section_unload(protocol::ChunkSectionUnload unload);
  void push_block_updates(protocol::BlockUpdateBatch updates);

  [[nodiscard]] std::vector<protocol::ChunkSectionData> drain_chunk_section_data();
  [[nodiscard]] std::vector<protocol::ChunkSectionUnload> drain_chunk_section_unloads();
  [[nodiscard]] std::vector<protocol::BlockUpdateBatch> drain_block_updates();

  void clear() noexcept;

private:
  std::vector<protocol::ChunkSectionData> received_chunk_section_data_{};
  std::vector<protocol::ChunkSectionUnload> received_chunk_section_unloads_{};
  std::vector<protocol::BlockUpdateBatch> received_block_updates_{};
};

} // namespace arv::client
