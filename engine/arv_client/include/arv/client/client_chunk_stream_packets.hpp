#pragma once

#include "arv/core/types.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <utility>
#include <vector>

namespace arv::client {

struct ClientChunkStreamPackets {
  std::vector<protocol::ClientboundChunkSectionUnload> unloads{};
  std::vector<protocol::ClientboundChunkSectionData> sections{};
  std::vector<protocol::ClientboundBlockUpdateBatch> block_updates{};
};

struct ClientChunkStreamPacketCounts {
  usize unloads{};
  usize sections{};
  usize block_update_batches{};
  usize block_updates{};
};

[[nodiscard]] inline ClientChunkStreamPacketCounts count_client_chunk_stream_packets(
    const ClientChunkStreamPackets& packets) noexcept {
  ClientChunkStreamPacketCounts counts;
  counts.unloads = packets.unloads.size();
  counts.sections = packets.sections.size();
  counts.block_update_batches = packets.block_updates.size();
  for (const protocol::ClientboundBlockUpdateBatch& batch : packets.block_updates) {
    counts.block_updates += batch.updates.size();
  }
  return counts;
}

template <typename Visitor>
void visit_client_chunk_stream_packets(
    const ClientChunkStreamPackets& packets,
    Visitor&& visitor) {
  auto&& visit = visitor;
  for (const protocol::ClientboundChunkSectionUnload& unload : packets.unloads) {
    visit(unload);
  }
  for (const protocol::ClientboundChunkSectionData& section : packets.sections) {
    visit(section);
  }
  for (const protocol::ClientboundBlockUpdateBatch& batch : packets.block_updates) {
    visit(batch);
  }
}

template <typename Visitor>
void consume_client_chunk_stream_packets(
    ClientChunkStreamPackets& packets,
    Visitor&& visitor) {
  auto&& visit = visitor;
  for (const protocol::ClientboundChunkSectionUnload& unload : packets.unloads) {
    visit(unload);
  }
  for (protocol::ClientboundChunkSectionData& section : packets.sections) {
    visit(std::move(section));
  }
  for (protocol::ClientboundBlockUpdateBatch& batch : packets.block_updates) {
    visit(std::move(batch));
  }
}

} // namespace arv::client
