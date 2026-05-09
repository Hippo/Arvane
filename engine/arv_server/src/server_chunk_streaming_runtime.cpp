#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"

#include <utility>

namespace arv::server::detail {

void ServerRuntimeImpl::tick_chunk_streaming() {
  queued_chunk_stream_messages_.clear();
  chunk_streamer_.tick_streaming_budget(
      default_world_,
      queued_chunk_stream_messages_);

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
