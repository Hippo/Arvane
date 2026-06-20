#include "arv/client/remote_chunk_stream.hpp"

#include "arv/voxel/storage.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace arv::client {

void mark_remote_chunk_section_content_changed(
    protocol::ClientboundChunkSectionData& section,
    u64 server_tick) noexcept {
  const u64 next_content_version =
      section.chunk_content_version == std::numeric_limits<u64>::max()
          ? section.chunk_content_version
          : section.chunk_content_version + 1U;
  section.chunk_content_version = std::max(next_content_version, server_tick);
  section.chunk_version = std::max(section.chunk_version, section.chunk_content_version);
}

const protocol::ClientboundChunkSectionData* RemoteChunkSectionStore::find_section(
    WorldID world_id,
    ChunkSectionCoord coord) const noexcept {
  const auto found = std::find_if(
      sections_.begin(),
      sections_.end(),
      [&](const protocol::ClientboundChunkSectionData& section) {
        return section.world_id == world_id && section.coord == coord;
      });
  return found == sections_.end() ? nullptr : &*found;
}

RemoteChunkSectionIndex RemoteChunkSectionStore::build_section_index(
    WorldID world_id) const {
  RemoteChunkSectionIndex index;
  index.reserve(sections_.size());
  for (const protocol::ClientboundChunkSectionData& section : sections_) {
    if (section.world_id == world_id) {
      index[section.coord] = &section;
    }
  }
  return index;
}

void RemoteChunkSectionStore::clear() noexcept {
  sections_.clear();
}

bool RemoteChunkSectionStore::apply_section_data(
    WorldID active_world,
    protocol::ClientboundChunkSectionData data) {
  if (data.world_id != active_world) {
    return false;
  }
  auto found = std::find_if(
      sections_.begin(),
      sections_.end(),
      [&](const protocol::ClientboundChunkSectionData& existing) {
        return existing.world_id == data.world_id && existing.coord == data.coord;
      });
  if (found == sections_.end()) {
    sections_.push_back(std::move(data));
  } else {
    *found = std::move(data);
  }
  return true;
}

bool RemoteChunkSectionStore::apply_section_unload(
    WorldID active_world,
    const protocol::ClientboundChunkSectionUnload& unload) {
  if (unload.world_id != active_world) {
    return false;
  }
  const usize old_size = sections_.size();
  const auto new_end = std::remove_if(
      sections_.begin(),
      sections_.end(),
      [&](const protocol::ClientboundChunkSectionData& existing) {
        return existing.world_id == unload.world_id && existing.coord == unload.coord;
      });
  sections_.erase(new_end, sections_.end());
  return sections_.size() != old_size;
}

bool RemoteChunkSectionStore::apply_block_update_batch(
    WorldID active_world,
    const protocol::ClientboundBlockUpdateBatch& batch) {
  if (batch.world_id != active_world) {
    return false;
  }

  bool changed = false;
  for (const protocol::BlockUpdate& update : batch.updates) {
    const ChunkSectionCoord coord = block_to_chunk(update.pos);
    auto found = std::find_if(
        sections_.begin(),
        sections_.end(),
        [&](const protocol::ClientboundChunkSectionData& existing) {
          return existing.world_id == batch.world_id && existing.coord == coord;
        });
    if (found == sections_.end()) {
      continue;
    }

    const usize index = voxel::local_index(block_to_local(update.pos));
    found->blocks[index] = update.new_state;
    if (update.has_fluid) {
      found->has_fluids = true;
      found->fluids[index] = update.new_fluid;
    }
    mark_remote_chunk_section_content_changed(*found, update.server_tick);
    changed = true;
  }
  return changed;
}

RemoteChunkStreamPacketApplyResult apply_remote_chunk_stream_packets(
    RemoteChunkSectionStore& store,
    WorldID active_world,
    ClientChunkStreamPackets packets) {
  RemoteChunkStreamPacketApplyResult result;
  struct ApplyPacket {
    RemoteChunkSectionStore& store;
    WorldID active_world;
    RemoteChunkStreamPacketApplyResult& result;

    void operator()(const protocol::ClientboundChunkSectionUnload& unload) const {
      if (store.apply_section_unload(active_world, unload)) {
        ++result.unloaded_sections;
        result.changed = true;
      }
    }

    void operator()(protocol::ClientboundChunkSectionData&& section) const {
      if (store.apply_section_data(active_world, std::move(section))) {
        ++result.loaded_sections;
        result.changed = true;
      }
    }

    void operator()(const protocol::ClientboundBlockUpdateBatch& batch) const {
      if (store.apply_block_update_batch(active_world, batch)) {
        ++result.block_update_batches;
        result.changed = true;
      }
    }
  };
  consume_client_chunk_stream_packets(
      packets,
      ApplyPacket{
          .store = store,
          .active_world = active_world,
          .result = result,
      });
  return result;
}

} // namespace arv::client
