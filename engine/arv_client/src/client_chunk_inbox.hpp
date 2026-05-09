#pragma once

#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <vector>

namespace arv::client {

class ClientChunkInbox {
public:
  void push_chunk_section_data(protocol::ClientboundChunkSectionData data);
  void push_chunk_section_unload(protocol::ClientboundChunkSectionUnload unload);
  void push_block_updates(protocol::ClientboundBlockUpdateBatch updates);

  [[nodiscard]] std::vector<protocol::ClientboundChunkSectionData> drain_chunk_section_data();
  [[nodiscard]] std::vector<protocol::ClientboundChunkSectionUnload> drain_chunk_section_unloads();
  [[nodiscard]] std::vector<protocol::ClientboundBlockUpdateBatch> drain_block_updates();

  void clear() noexcept;

private:
  std::vector<protocol::ClientboundChunkSectionData> received_chunk_section_data_{};
  std::vector<protocol::ClientboundChunkSectionUnload> received_chunk_section_unloads_{};
  std::vector<protocol::ClientboundBlockUpdateBatch> received_block_updates_{};
};

} // namespace arv::client
