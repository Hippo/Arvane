#include "client_chunk_inbox.hpp"

#include <utility>

namespace arv::client {

void ClientChunkInbox::push_chunk_data(protocol::ChunkData data) {
  received_chunk_data_.emplace_back(std::move(data));
}

void ClientChunkInbox::push_chunk_unload(protocol::ChunkUnload unload) {
  received_chunk_unloads_.emplace_back(unload);
}

void ClientChunkInbox::push_block_updates(protocol::BlockUpdateBatch updates) {
  received_block_updates_.emplace_back(std::move(updates));
}

std::vector<protocol::ChunkData> ClientChunkInbox::drain_chunk_data() {
  std::vector<protocol::ChunkData> drained = std::move(received_chunk_data_);
  received_chunk_data_.clear();
  return drained;
}

std::vector<protocol::ChunkUnload> ClientChunkInbox::drain_chunk_unloads() {
  std::vector<protocol::ChunkUnload> drained = std::move(received_chunk_unloads_);
  received_chunk_unloads_.clear();
  return drained;
}

std::vector<protocol::BlockUpdateBatch> ClientChunkInbox::drain_block_updates() {
  std::vector<protocol::BlockUpdateBatch> drained = std::move(received_block_updates_);
  received_block_updates_.clear();
  return drained;
}

void ClientChunkInbox::clear() noexcept {
  received_chunk_data_.clear();
  received_chunk_unloads_.clear();
  received_block_updates_.clear();
}

} // namespace arv::client
