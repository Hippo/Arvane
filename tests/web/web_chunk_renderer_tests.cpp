#include "web_chunk_renderer.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] arv::web::WebChunkDrawState make_draw_state() {
  arv::web::WebChunkDrawState state;
  state.draw_item.key = {arv::WorldID{1}, arv::ChunkSectionCoord{{2, 3, 4}}};
  state.draw_item.expected_version = 7;
  state.draw_item.expected_light_version = 11;
  state.draw_item.has_opaque_mesh = true;
  state.content_version = 13;
  state.light_version = 11;
  state.dependency_signature = 17;
  state.mesh_upload_confirmed = false;
  return state;
}

[[nodiscard]] arv::render::ChunkMeshUploadReceipt make_receipt(bool accepted) {
  return {{arv::WorldID{1}, arv::ChunkSectionCoord{{2, 3, 4}}},
          7,
          13,
          11,
          accepted};
}

} // namespace

TEST(WebChunkRendererTests, CurrentDrawStateRequiresAcceptedUploadReceipt) {
  arv::web::WebChunkDrawState state = make_draw_state();

  EXPECT_FALSE(arv::web::web_chunk_draw_state_is_current(state, 13, 11, 17));

  state.mesh_upload_confirmed = true;

  EXPECT_TRUE(arv::web::web_chunk_draw_state_is_current(state, 13, 11, 17));
  EXPECT_FALSE(arv::web::web_chunk_draw_state_is_current(state, 14, 11, 17));
  EXPECT_FALSE(arv::web::web_chunk_draw_state_is_current(state, 13, 12, 17));
  EXPECT_FALSE(arv::web::web_chunk_draw_state_is_current(state, 13, 11, 18));
}

TEST(WebChunkRendererTests, AcceptedReceiptConfirmsMatchingUpload) {
  arv::web::WebChunkDrawState state = make_draw_state();

  const arv::web::WebChunkUploadReceiptResult result =
      arv::web::apply_web_chunk_upload_receipt(state, make_receipt(true));

  EXPECT_EQ(result, arv::web::WebChunkUploadReceiptResult::Accepted);
  EXPECT_TRUE(state.mesh_upload_confirmed);
}

TEST(WebChunkRendererTests, RejectedReceiptLeavesDrawStateForRetry) {
  arv::web::WebChunkDrawState state = make_draw_state();
  state.mesh_upload_confirmed = true;
  const arv::render::ChunkDrawItem before = state.draw_item;

  const arv::web::WebChunkUploadReceiptResult result =
      arv::web::apply_web_chunk_upload_receipt(state, make_receipt(false));

  EXPECT_EQ(result, arv::web::WebChunkUploadReceiptResult::Rejected);
  EXPECT_FALSE(state.mesh_upload_confirmed);
  EXPECT_EQ(state.draw_item.key, before.key);
  EXPECT_EQ(state.draw_item.expected_version, before.expected_version);
  EXPECT_EQ(state.draw_item.expected_light_version, before.expected_light_version);
  EXPECT_EQ(state.draw_item.has_opaque_mesh, before.has_opaque_mesh);
  EXPECT_EQ(state.draw_item.has_transparent_mesh, before.has_transparent_mesh);
}

TEST(WebChunkRendererTests, StaleReceiptDoesNotChangeCurrentState) {
  arv::web::WebChunkDrawState state = make_draw_state();
  state.mesh_upload_confirmed = true;
  arv::render::ChunkMeshUploadReceipt stale = make_receipt(false);
  stale.source_version = 6;

  const arv::web::WebChunkUploadReceiptResult result =
      arv::web::apply_web_chunk_upload_receipt(state, stale);

  EXPECT_EQ(result, arv::web::WebChunkUploadReceiptResult::Ignored);
  EXPECT_TRUE(state.mesh_upload_confirmed);
}
