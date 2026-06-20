#pragma once

#include "arv/render/render_types.hpp"

#include <vector>

namespace arv::web {

struct WebChunkDrawState {
  render::ChunkDrawItem draw_item{};
  u64 content_version{};
  u64 light_version{};
  u64 dependency_signature{};
  bool mesh_upload_confirmed{};
};

struct ChunkRenderer {
  std::vector<WebChunkDrawState> chunk_draws{};
  bool announced_first_upload{};
  bool announced_first_entity_upload{};
};

enum class WebChunkUploadReceiptResult {
  Ignored,
  Accepted,
  Rejected,
};

[[nodiscard]] inline bool web_chunk_draw_state_is_current(
    const WebChunkDrawState& state,
    u64 content_version,
    u64 light_version,
    u64 dependency_signature) noexcept {
  return state.mesh_upload_confirmed && state.content_version == content_version &&
         state.light_version == light_version &&
         state.dependency_signature == dependency_signature;
}

[[nodiscard]] inline bool web_chunk_upload_receipt_matches_state(
    const WebChunkDrawState& state,
    const render::ChunkMeshUploadReceipt& receipt) noexcept {
  return state.draw_item.key == receipt.key &&
         state.draw_item.expected_version == receipt.source_version &&
         state.draw_item.expected_light_version == receipt.source_light_version &&
         state.content_version == receipt.source_content_version &&
         state.light_version == receipt.source_light_version;
}

inline WebChunkUploadReceiptResult apply_web_chunk_upload_receipt(
    WebChunkDrawState& state,
    const render::ChunkMeshUploadReceipt& receipt) noexcept {
  if (!web_chunk_upload_receipt_matches_state(state, receipt)) {
    return WebChunkUploadReceiptResult::Ignored;
  }
  state.mesh_upload_confirmed = receipt.accepted;
  return receipt.accepted ? WebChunkUploadReceiptResult::Accepted
                          : WebChunkUploadReceiptResult::Rejected;
}

void sync_web_chunk_meshes();
void append_web_chunk_draws_to_frame(render::FramePacket& frame);

} // namespace arv::web
