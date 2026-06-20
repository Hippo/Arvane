#pragma once

#include "arv/client/client_chunk_stream_packets.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <span>
#include <vector>

namespace arv::client {

using RemoteChunkSectionIndex = HashMap<
    ChunkSectionCoord,
    const protocol::ClientboundChunkSectionData*>;

struct RemoteChunkStreamPacketApplyResult {
  u32 unloaded_sections{};
  u32 loaded_sections{};
  u32 block_update_batches{};
  bool changed{};
};

void mark_remote_chunk_section_content_changed(
    protocol::ClientboundChunkSectionData& section,
    u64 server_tick) noexcept;

class RemoteChunkSectionStore {
public:
  [[nodiscard]] const std::vector<protocol::ClientboundChunkSectionData>& sections()
      const noexcept {
    return sections_;
  }

  [[nodiscard]] std::span<const protocol::ClientboundChunkSectionData> section_span()
      const noexcept {
    return sections_;
  }

  [[nodiscard]] const protocol::ClientboundChunkSectionData* find_section(
      WorldID world_id,
      ChunkSectionCoord coord) const noexcept;

  [[nodiscard]] RemoteChunkSectionIndex build_section_index(
      WorldID world_id) const;

  void clear() noexcept;

  [[nodiscard]] bool apply_section_data(
      WorldID active_world,
      protocol::ClientboundChunkSectionData data);
  [[nodiscard]] bool apply_section_unload(
      WorldID active_world,
      const protocol::ClientboundChunkSectionUnload& unload);
  [[nodiscard]] bool apply_block_update_batch(
      WorldID active_world,
      const protocol::ClientboundBlockUpdateBatch& batch);

private:
  std::vector<protocol::ClientboundChunkSectionData> sections_{};
};

[[nodiscard]] RemoteChunkStreamPacketApplyResult apply_remote_chunk_stream_packets(
    RemoteChunkSectionStore& store,
    WorldID active_world,
    ClientChunkStreamPackets packets);

} // namespace arv::client
