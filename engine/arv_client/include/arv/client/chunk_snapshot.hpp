#pragma once

#include "arv/protocol/chunk_streaming.hpp"
#include "arv/voxel/storage.hpp"

namespace arv::client {

[[nodiscard]] voxel::ChunkSnapshot chunk_snapshot_from_section_data(
    const protocol::ClientboundChunkSectionData& section);

} // namespace arv::client
