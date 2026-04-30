#include "arv/render/render_types.hpp"

#include <cstring>

namespace arv::render {

void append_chunk_vertex(CpuMeshBuffer& buffer, ChunkVertex vertex) {
  const auto* bytes = reinterpret_cast<const u8*>(&vertex);
  const usize offset = buffer.vertex_bytes.size();
  buffer.vertex_bytes.resize(offset + sizeof(ChunkVertex));
  std::memcpy(buffer.vertex_bytes.data() + offset, bytes, sizeof(ChunkVertex));
}

ChunkVertex chunk_vertex_at(const CpuMeshBuffer& buffer, usize index) noexcept {
  ChunkVertex vertex;
  std::memcpy(&vertex, buffer.vertex_bytes.data() + index * sizeof(ChunkVertex), sizeof(ChunkVertex));
  return vertex;
}

} // namespace arv::render
