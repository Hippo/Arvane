#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/core/profiling.hpp"

#include <utility>

namespace arv::server::detail {

void ServerRuntimeImpl::tick_chunk_streaming() {
  queued_chunk_stream_messages_.clear();
  chunk_streamer_.tick_streaming_budget(
      default_world_,
      queued_chunk_stream_messages_);
  send_queued_chunk_stream_messages();
}

void ServerRuntimeImpl::tick_chunk_streaming_maintenance() {
  queued_chunk_stream_messages_.clear();
  chunk_streamer_.tick_streaming_maintenance(
      default_world_,
      queued_chunk_stream_messages_,
      chunk_streaming_config_.max_missing_columns_requested_per_player_tick);
  send_queued_chunk_stream_messages();
}

void ServerRuntimeImpl::pump_chunk_streaming_frame(bool send_ready_chunks) {
  if (state_ != ServerRuntimeState::Running ||
      !transport_ ||
      mode_ != ServerMode::Integrated ||
      !chunk_streaming_config_.integrated_frame_pump_enabled) {
    return;
  }

  ARV_PROFILE_ZONE("Integrated chunk streaming frame pump");
  world_jobs_.drain_generation_jobs(
      default_world_,
      chunk_streaming_config_.max_generation_results_per_frame,
      chunk_streaming_config_.max_generation_sections_published_per_frame);
  world_jobs_.schedule_generation_jobs(
      default_world_,
      chunk_streamer_,
      chunk_generation_,
      chunk_streaming_config_.max_generation_schedules_per_frame,
      true);
  if (!send_ready_chunks) {
    return;
  }
  queued_chunk_stream_messages_.clear();
  chunk_streamer_.tick_ready_chunk_budget(
      default_world_,
      queued_chunk_stream_messages_,
      chunk_streaming_config_.max_ready_chunks_sent_per_player_frame);
  send_queued_chunk_stream_messages();
}

void ServerRuntimeImpl::send_queued_chunk_stream_messages() {
  for (QueuedChunkStreamMessage& message : queued_chunk_stream_messages_) {
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        message.connection_id,
        message.message_type,
        message.reliability,
        message.lane,
        std::move(message.payload));
    log_send_failure_if_dedicated(mode_, message.connection_id, send_result);
    if (send_result && message.tracks_sent_chunk) {
      (void)chunk_streamer_.confirm_chunk_sent(
          message.session_id,
          message.world_id,
          message.coord,
          message.block_version,
          message.light_version,
          message.lighting_valid);
    }
    if (send_result &&
        mode_ == ServerMode::Dedicated &&
        message.message_type == protocol::MessageType::ClientboundChunkSectionData) {
      if (const ServerSession* session = sessions_.find_by_connection(message.connection_id);
          session != nullptr &&
          logged_chunk_delivery_sessions_.insert(session->session_id).second) {
        core::info(
            "dedicated",
            "streaming first chunk payload to '{}': session={} player={}",
            session->profile_name,
            session->session_id.value,
            session->player_id.value);
      }
    }
  }
}

} // namespace arv::server::detail
