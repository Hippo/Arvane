#include "client_chunk_inbox.hpp"

#include <utility>

namespace arv::client {

void ClientChunkInbox::push_chunk_section_data(protocol::ClientboundChunkSectionData data) {
  received_chunk_section_data_.emplace_back(std::move(data));
}

void ClientChunkInbox::push_chunk_section_unload(protocol::ClientboundChunkSectionUnload unload) {
  received_chunk_section_unloads_.emplace_back(unload);
}

void ClientChunkInbox::push_block_updates(protocol::ClientboundBlockUpdateBatch updates) {
  received_block_updates_.emplace_back(std::move(updates));
}

std::vector<protocol::ClientboundChunkSectionData> ClientChunkInbox::drain_chunk_section_data() {
  std::vector<protocol::ClientboundChunkSectionData> drained = std::move(received_chunk_section_data_);
  received_chunk_section_data_.clear();
  return drained;
}

std::vector<protocol::ClientboundChunkSectionUnload> ClientChunkInbox::drain_chunk_section_unloads() {
  std::vector<protocol::ClientboundChunkSectionUnload> drained = std::move(received_chunk_section_unloads_);
  received_chunk_section_unloads_.clear();
  return drained;
}

std::vector<protocol::ClientboundBlockUpdateBatch> ClientChunkInbox::drain_block_updates() {
  std::vector<protocol::ClientboundBlockUpdateBatch> drained = std::move(received_block_updates_);
  received_block_updates_.clear();
  return drained;
}

void ClientChunkInbox::clear() noexcept {
  received_chunk_section_data_.clear();
  received_chunk_section_unloads_.clear();
  received_block_updates_.clear();
}

} // namespace arv::client
