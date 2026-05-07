#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/server/skin_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace arv::server::detail {
namespace {

void configure_skin_transfer_state(
    SkinTransferSessionState& state,
    const io::SkinConfig& skins) {
  if (state.limiters_initialized) {
    return;
  }
  state.upload_begin_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute),
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute) / 60.0};
  state.completed_upload_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute),
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute) / 60.0};
  state.completed_upload_hour_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour),
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour) / 3600.0};
  state.upload_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour),
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour) / 3600.0};
  state.failed_decode_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute),
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute) / 60.0};
  state.request_second_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_second),
      static_cast<double>(skins.downloads.max_requests_per_second)};
  state.request_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_minute),
      static_cast<double>(skins.downloads.max_requests_per_minute) / 60.0};
  state.download_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_bytes_per_minute),
      static_cast<double>(skins.downloads.max_bytes_per_minute) / 60.0};
  state.limiters_initialized = true;
}

[[nodiscard]] TickIndex seconds_to_ticks(u32 seconds, TickRate tick_rate) noexcept {
  const TickIndex ticks_per_second = std::max<TickIndex>(1U, tick_rate);
  if (seconds > std::numeric_limits<TickIndex>::max() / ticks_per_second) {
    return std::numeric_limits<TickIndex>::max();
  }
  return std::max<TickIndex>(1U, static_cast<TickIndex>(seconds) * ticks_per_second);
}

[[nodiscard]] bool elapsed_ticks_exceeds(
    TickIndex now,
    TickIndex then,
    TickIndex limit) noexcept {
  return now < then || now - then > limit;
}

[[nodiscard]] bool skin_upload_timed_out(
    const ActiveSkinUpload& upload,
    TickIndex tick_index,
    const io::SkinConfig& skins,
    TickRate tick_rate) noexcept {
  return elapsed_ticks_exceeds(
             tick_index,
             upload.started_tick,
             seconds_to_ticks(skins.uploads.timeout_seconds, tick_rate)) ||
         elapsed_ticks_exceeds(
             tick_index,
             upload.last_packet_tick,
             seconds_to_ticks(skins.uploads.idle_timeout_seconds, tick_rate));
}

void clear_active_skin_upload(
    SkinTransferSessionState& state,
    u32& active_skin_uploads_global) noexcept {
  if (!state.active_upload.has_value()) {
    return;
  }
  state.active_upload.reset();
  if (active_skin_uploads_global > 0U) {
    --active_skin_uploads_global;
  }
}

[[nodiscard]] std::optional<u64> regular_file_size(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return std::nullopt;
  }
  error.clear();
  const u64 size = std::filesystem::file_size(path, error);
  if (error) {
    return std::nullopt;
  }
  return size;
}

[[nodiscard]] std::string_view skin_request_deny_reason_name(
    protocol::SkinRequestDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinRequestDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinRequestDenyReason::InvalidHash:
      return "InvalidHash";
    case protocol::SkinRequestDenyReason::NotFound:
      return "NotFound";
    case protocol::SkinRequestDenyReason::NotAdvertisedToClient:
      return "NotAdvertisedToClient";
    case protocol::SkinRequestDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinRequestDenyReason::TooLarge:
      return "TooLarge";
    case protocol::SkinRequestDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] std::string_view skin_upload_deny_reason_name(
    protocol::SkinUploadDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinUploadDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinUploadDenyReason::UploadsDisabled:
      return "UploadsDisabled";
    case protocol::SkinUploadDenyReason::NotAuthenticated:
      return "NotAuthenticated";
    case protocol::SkinUploadDenyReason::InvalidFormat:
      return "InvalidFormat";
    case protocol::SkinUploadDenyReason::InvalidResolution:
      return "InvalidResolution";
    case protocol::SkinUploadDenyReason::ResolutionNotAllowed:
      return "ResolutionNotAllowed";
    case protocol::SkinUploadDenyReason::UploadTooLarge:
      return "UploadTooLarge";
    case protocol::SkinUploadDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinUploadDenyReason::UploadAlreadyPending:
      return "UploadAlreadyPending";
    case protocol::SkinUploadDenyReason::ServerStorageFull:
      return "ServerStorageFull";
    case protocol::SkinUploadDenyReason::DecodeFailed:
      return "DecodeFailed";
    case protocol::SkinUploadDenyReason::HashMismatch:
      return "HashMismatch";
    case protocol::SkinUploadDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] std::optional<std::vector<u8>> read_binary_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::vector<u8>{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

} // namespace

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload) {
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  return transport.send(
      {connection_id,
       reliability,
       std::move(packet),
       message_type,
       lane});
}

void log_send_failure_if_dedicated(
    ServerMode mode,
    net::ConnectionID connection_id,
    const net::NetworkResult& send_result) {
  if (send_result || mode != ServerMode::Dedicated) {
    return;
  }

  core::warn(
      "dedicated",
      "network send failed: connection={} reason='{}'",
      connection_id.value,
      send_result.error().message);
}

void ServerRuntimeImpl::expire_timed_out_skin_uploads() {
  for (auto& [session_id, transfer] : skin_transfers_) {
    (void)session_id;
    if (transfer.active_upload.has_value() &&
        skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
      clear_active_skin_upload(transfer, active_skin_uploads_global_);
    }
  }
}

void ServerRuntimeImpl::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      handle_connection_lost(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      if (mode_ == ServerMode::Dedicated) {
        core::warn(
            "dedicated",
            "network send failed: connection={} reason='{}'",
            event.connection_id.value,
            event.reason);
      }
      return;
  }
}

void ServerRuntimeImpl::handle_message(
    net::ConnectionID connection_id,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ClientHello: {
      auto client_hello = protocol::deserialize_client_hello(decoded.value().payload);
      if (!client_hello) {
        return;
      }
      handle_client_hello(connection_id, client_hello.value());
      return;
    }
    case protocol::MessageType::AuthResponse: {
      auto response = protocol::deserialize_auth_response(decoded.value().payload);
      if (!response) {
        return;
      }
      handle_auth_response(connection_id, response.value());
      return;
    }
    case protocol::MessageType::ClientInputBatch: {
      auto batch = protocol::deserialize_client_input_batch(decoded.value().payload);
      if (!batch) {
        return;
      }
      handle_input_batch(connection_id, batch.value());
      return;
    }
    case protocol::MessageType::ChunkInterestUpdate: {
      auto interest = protocol::deserialize_chunk_interest_update(decoded.value().payload);
      if (!interest) {
        return;
      }
      handle_chunk_interest_update(connection_id, interest.value());
      return;
    }
    case protocol::MessageType::InventoryActionRequest: {
      auto request = protocol::deserialize_inventory_action_request(decoded.value().payload);
      if (!request) {
        return;
      }
      handle_inventory_action_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::SkinRequest: {
      auto request = protocol::deserialize_skin_request(decoded.value().payload);
      if (!request) {
        return;
      }
      handle_skin_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::SkinRequestBatch: {
      auto batch = protocol::deserialize_skin_request_batch(decoded.value().payload);
      if (!batch) {
        return;
      }
      handle_skin_request_batch(connection_id, std::move(batch.value()));
      return;
    }
    case protocol::MessageType::SkinUploadBegin: {
      auto begin = protocol::deserialize_skin_upload_begin(decoded.value().payload);
      if (!begin) {
        return;
      }
      handle_skin_upload_begin(connection_id, begin.value());
      return;
    }
    case protocol::MessageType::SkinUploadData: {
      auto data = protocol::deserialize_skin_upload_data(decoded.value().payload);
      if (!data) {
        return;
      }
      handle_skin_upload_data(connection_id, std::move(data.value()));
      return;
    }
    case protocol::MessageType::SkinUploadChunk: {
      auto chunk = protocol::deserialize_skin_upload_chunk(decoded.value().payload);
      if (!chunk) {
        return;
      }
      handle_skin_upload_chunk(connection_id, std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::SkinUploadEnd: {
      auto end = protocol::deserialize_skin_upload_end(decoded.value().payload);
      if (!end) {
        return;
      }
      handle_skin_upload_end(connection_id, end.value());
      return;
    }
    default:
      return;
  }
}

void ServerRuntimeImpl::handle_client_hello(
    net::ConnectionID connection_id,
    const protocol::ClientHello& client_hello) {
  protocol::HandshakePolicy policy = handshake_policy_;
  if (!client_hello.auth_begin.has_value()) {
    protocol::ServerHello rejected = protocol::make_server_hello(client_hello, policy);
    rejected.accepted = false;
    rejected.reject_reason = "authentication required";
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ServerHello,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_server_hello(rejected));
    return;
  }

  const protocol::ServerboundAuthBegin& auth_begin = client_hello.auth_begin.value();
  PlayerProfileOpenResult profile_result = profiles_.open_or_create(
      auth_begin.public_key,
      auth_begin.display_name,
      auth_begin.claimed_profile_id);
  if (profile_result.status == PlayerProfileOpenStatus::InvalidClaimedProfileID ||
      profile_result.status == PlayerProfileOpenStatus::PublicKeyMismatch ||
      profile_result.profile == nullptr) {
    protocol::ServerHello rejected = protocol::make_server_hello(client_hello, policy);
    rejected.accepted = false;
    rejected.reject_reason = "authentication failed";
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ServerHello,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_server_hello(rejected));
    return;
  }

  SessionProfileIdentity profile_identity;
  profile_identity.profile_id = profile_result.profile->profile_id;
  profile_identity.display_name = profile_result.profile->display_name;
  profile_identity.public_key = profile_result.profile->public_key;
  profile_identity.has_public_key = true;

  if (ServerSession* existing = sessions_.find_by_connection(connection_id); existing != nullptr) {
    policy.assigned_session_id = existing->session_id;
    policy.assigned_player_id = existing->player_id;
    policy.initial_world_id = existing->world_id;
  } else {
    const ReservedSessionIdentity identity = sessions_.reserve_identity();
    policy.assigned_session_id = identity.session_id;
    policy.assigned_player_id = identity.player_id;
    policy.initial_world_id = DefaultWorldID;
  }

  protocol::ServerHello server_hello = protocol::make_server_hello(client_hello, policy);
  if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
    ServerSession& session = sessions_.create(
        connection_id,
        {server_hello.assigned_session_id, server_hello.assigned_player_id},
        server_hello.initial_world_id,
        std::move(profile_identity),
        mode_ == ServerMode::Integrated);
    if (mode_ == ServerMode::Integrated) {
      complete_authenticated_session(session);
    }
  }

  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ServerHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_server_hello(server_hello));
  if (server_hello.accepted) {
    if (ServerSession* session = sessions_.find_by_connection(connection_id);
        session != nullptr) {
      if (mode_ == ServerMode::Integrated) {
        send_auth_result(*session, true);
        if (const player::PlayerState* player_state =
                default_world_.find_player(session->player_id);
            player_state != nullptr) {
          send_skin_rules(*session, *player_state);
          send_world_join_result(*session, *player_state);
          session->connection_state = net::ConnectionState::InGame;
          sessions_.for_each_session([&](const ServerSession& subject) {
            if (subject.authenticated && subject.world_id == session->world_id) {
              send_player_profile_info_to(*session, subject);
            }
          });
          broadcast_player_profile_info(*session);
        }
      } else if (!session->authenticated) {
        send_auth_challenge(*session);
      }
    }
  }
}

void ServerRuntimeImpl::handle_auth_response(
    net::ConnectionID connection_id,
    const protocol::ServerboundAuthResponse& response) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || session->authenticated || !session->challenge.has_value() ||
      !session->has_public_key) {
    return;
  }

  const PendingAuthChallenge challenge = session->challenge.value();
  session->challenge.reset();
  if (tick_index_ > challenge.expires_server_tick) {
    send_auth_result(*session, false, "authentication failed");
    return;
  }

  const std::vector<u8> payload_bytes = protocol::build_auth_signed_payload(
      {protocol::CurrentProtocolVersion,
       challenge.server_instance_id,
       session->session_id,
       session->profile_id,
       session->public_key,
       challenge.nonce,
       challenge.expires_server_tick});
  const auto* first = reinterpret_cast<const std::byte*>(payload_bytes.data());
  const bool verified = crypto::verify_ed25519(
      std::span<const std::byte>{first, payload_bytes.size()},
      response.signature,
      session->public_key);
  if (!verified) {
    send_auth_result(*session, false, "authentication failed");
    return;
  }

  complete_authenticated_session(*session);
  send_auth_result(*session, true);
  if (const player::PlayerState* player_state = default_world_.find_player(session->player_id);
      player_state != nullptr) {
    send_skin_rules(*session, *player_state);
    send_world_join_result(*session, *player_state);
    session->connection_state = net::ConnectionState::InGame;
    sessions_.for_each_session([&](const ServerSession& subject) {
      if (subject.authenticated && subject.world_id == session->world_id) {
        send_player_profile_info_to(*session, subject);
      }
    });
    broadcast_player_profile_info(*session);
  }
}

void ServerRuntimeImpl::handle_input_batch(
    net::ConnectionID connection_id,
    const protocol::ClientInputBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  for (usize index = 0; index < batch.inputs.size(); ++index) {
    const TickIndex input_tick = batch.first_input_tick + index;
    player::PlayerCommand command =
        protocol::input_sample_to_command(batch.inputs[index], input_tick);
    player::PlayerCommand& pending = pending_player_commands_[session->player_id];
    if (command.client_tick >= pending.client_tick) {
      pending = std::move(command);
    }
  }
}

void ServerRuntimeImpl::handle_skin_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinRequestDenyReason reason) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(reason));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        reason};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (session == nullptr || !session->authenticated) {
    return;
  }
  if (!skins_.enabled) {
    send_deny(protocol::SkinRequestDenyReason::SkinsDisabled);
    return;
  }
  if (!protocol::is_valid_skin_hash(request.skin_hash)) {
    send_deny(protocol::SkinRequestDenyReason::InvalidHash);
    return;
  }

  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (!transfer.request_second_limiter.try_consume(1.0, now_seconds) ||
      !transfer.request_minute_limiter.try_consume(1.0, now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (transfer.in_flight_request_ids.find(request.request_id) !=
          transfer.in_flight_request_ids.end() ||
      transfer.in_flight_skin_hashes.find(request.skin_hash) !=
          transfer.in_flight_skin_hashes.end() ||
      transfer.in_flight_request_ids.size() >=
          skins_.downloads.max_in_flight_per_connection) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (skins_.downloads.require_advertised_hash &&
      transfer.advertised_skin_hashes.find(request.skin_hash) ==
          transfer.advertised_skin_hashes.end()) {
    send_deny(protocol::SkinRequestDenyReason::NotAdvertisedToClient);
    return;
  }
  auto descriptor_result = load_stored_skin_descriptor(server_save_root_, request.skin_hash);
  if (!descriptor_result) {
    send_deny(descriptor_result.error());
    return;
  }
  const protocol::SkinDescriptor descriptor = descriptor_result.value();
  const io::SkinResolutionConfig* resolution =
      descriptor.width == descriptor.height
          ? find_skin_resolution_rule(skins_, descriptor.width)
          : nullptr;
  if (resolution == nullptr || descriptor.width == 0U || descriptor.height == 0U) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }
  const std::filesystem::path path =
      server_skin_storage_path(server_save_root_, request.skin_hash);
  const std::optional<u64> file_size = regular_file_size(path);
  if (!file_size.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (file_size.value() == 0U ||
      file_size.value() > resolution->max_upload_bytes ||
      file_size.value() > protocol::MaxSkinImagePayloadBytes) {
    send_deny(protocol::SkinRequestDenyReason::TooLarge);
    return;
  }
  if (!transfer.download_byte_limiter.try_consume(
          static_cast<double>(file_size.value()),
          now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }

  std::optional<std::vector<u8>> bytes = read_binary_file(path);
  if (!bytes.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (bytes->size() != file_size.value()) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }

  transfer.in_flight_request_ids.insert(request.request_id);
  transfer.in_flight_skin_hashes.insert(request.skin_hash);
  auto finish_request = [&]() {
    transfer.in_flight_request_ids.erase(request.request_id);
    transfer.in_flight_skin_hashes.erase(request.skin_hash);
  };
  const bool chunked =
      bytes->size() >= skins_.uploads.chunk_threshold_bytes ||
      descriptor.width >= 512U ||
      descriptor.height >= 512U;
  if (!chunked) {
    protocol::ClientboundSkinData data;
    data.request_id = request.request_id;
    data.skin_hash = request.skin_hash;
    data.format = descriptor.format;
    data.width = descriptor.width;
    data.height = descriptor.height;
    data.compressed_image_bytes = std::move(*bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinData,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data(data));
    finish_request();
    return;
  }

  protocol::ClientboundSkinDataBeginChunked begin;
  begin.request_id = request.request_id;
  begin.skin_hash = request.skin_hash;
  begin.format = descriptor.format;
  begin.width = descriptor.width;
  begin.height = descriptor.height;
  begin.total_bytes = static_cast<u32>(bytes->size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::SkinDataBeginChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_begin_chunked(begin));
  usize offset = 0;
  while (offset < bytes->size()) {
    const usize chunk_size = std::min<usize>(
        skins_.uploads.max_chunk_bytes,
        bytes->size() - offset);
    protocol::ClientboundSkinDataChunk chunk;
    chunk.request_id = request.request_id;
    chunk.chunk.insert(
        chunk.chunk.end(),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinDataChunk,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data_chunk(chunk));
    offset += chunk_size;
  }
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::SkinDataEndChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_end_chunked({request.request_id}));
  finish_request();
}

void ServerRuntimeImpl::handle_skin_request_batch(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequestBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  auto send_deny = [&](const protocol::SkinRequestEntry& request) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(protocol::SkinRequestDenyReason::RateLimited));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        protocol::SkinRequestDenyReason::RateLimited};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (batch.requests.size() > skins_.downloads.max_batch_size) {
    for (const protocol::SkinRequestEntry& request : batch.requests) {
      send_deny(request);
    }
    return;
  }

  for (const protocol::SkinRequestEntry& entry : batch.requests) {
    handle_skin_request(connection_id, {entry.request_id, entry.skin_hash});
  }
}

void ServerRuntimeImpl::handle_skin_upload_begin(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadBegin& begin) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload denied: connection={} reason={} width={} height={} bytes={}",
        connection_id.value,
        skin_upload_deny_reason_name(reason),
        begin.width,
        begin.height,
        begin.total_upload_bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinUploadDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_deny({reason}));
  };
  if (session == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  if (!session->authenticated) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  if (transfer.active_upload.has_value() &&
      skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  const bool rate_limited =
      !transfer.upload_begin_limiter.try_consume(1.0, now_seconds) ||
      transfer.failed_decode_limiter.available(now_seconds) < 1.0;
  const auto accepted = validate_skin_upload_begin(
      skins_,
      player_state->resolved_permissions,
      session->authenticated,
      transfer.active_upload.has_value(),
      rate_limited,
      active_skin_uploads_global_ >= skins_.uploads.max_active_global,
      begin);
  if (!accepted) {
    send_deny(accepted.error());
    return;
  }

  const u32 upload_id = transfer.next_upload_id++;
  if (!begin.client_skin_hash_hint.empty()) {
    auto existing_descriptor =
        load_stored_skin_descriptor(server_save_root_, begin.client_skin_hash_hint);
    if (existing_descriptor &&
        existing_descriptor.value().width == begin.width &&
        existing_descriptor.value().height == begin.height) {
      set_selected_skin_hash_for_session(*session, existing_descriptor.value().skin_hash);
      core::info(
          "skin",
          "skin upload complete: profile={} skin={} bytes=0",
          session->profile_id.string(),
          existing_descriptor.value().skin_hash);
      (void)send_protocol_message(
          *transport_,
          connection_id,
          protocol::MessageType::SkinUploadComplete,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Session,
          protocol::serialize_skin_upload_complete(
              {upload_id, std::move(existing_descriptor.value())}));
      broadcast_player_profile_info(*session);
      return;
    }
  }

  if (!transfer.upload_byte_limiter.try_consume(
          static_cast<double>(begin.total_upload_bytes),
          now_seconds)) {
    send_deny(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload;
  upload.upload_id = upload_id;
  upload.begin = begin;
  upload.mode = accepted.value().upload_mode;
  upload.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  upload.started_tick = tick_index_;
  upload.last_packet_tick = tick_index_;
  upload.session_id = session->session_id;
  upload.bytes.reserve(begin.total_upload_bytes);
  transfer.active_upload = std::move(upload);
  ++active_skin_uploads_global_;

  protocol::ClientboundSkinUploadAccept packet;
  packet.upload_id = upload_id;
  packet.upload_mode = accepted.value().upload_mode;
  packet.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::SkinUploadAccept,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_accept(packet));
}

void ServerRuntimeImpl::handle_skin_upload_data(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadData& data) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        data.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({data.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != data.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Full) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (data.compressed_image_bytes.size() !=
          transfer.active_upload->begin.total_upload_bytes ||
      data.compressed_image_bytes.size() >
          transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      data.compressed_image_bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_hash_for_session(*session, descriptor.value().skin_hash);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      data.compressed_image_bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::SkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({data.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_skin_upload_chunk(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadChunk& chunk) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        chunk.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({chunk.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != chunk.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (chunk.image_chunk_bytes.empty() ||
      chunk.image_chunk_bytes.size() > transfer.active_upload->max_chunk_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  if (transfer.active_upload->bytes.size() + chunk.image_chunk_bytes.size() >
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  transfer.active_upload->bytes.insert(
      transfer.active_upload->bytes.end(),
      chunk.image_chunk_bytes.begin(),
      chunk.image_chunk_bytes.end());
  transfer.active_upload->last_packet_tick = tick_index_;
}

void ServerRuntimeImpl::handle_skin_upload_end(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadEnd& end) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        end.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::SkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({end.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != end.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (transfer.active_upload->bytes.size() !=
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }
  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      upload.bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_hash_for_session(*session, descriptor.value().skin_hash);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      upload.bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::SkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({end.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_chunk_interest_update(
    net::ConnectionID connection_id,
    const protocol::ChunkInterestUpdate& interest) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    return;
  }

  const ChunkSectionCoord authoritative_center = player_chunk_coord(*player_state);
  chunk_streamer_.update_player_interest(
      *session,
      authoritative_center,
      default_world_.height_settings(),
      interest);
  if (mode_ == ServerMode::Dedicated &&
      logged_chunk_interest_sessions_.insert(session->session_id).second) {
    core::info(
        "dedicated",
        "client '{}' requested chunk stream: session={} player={} center=({}, {}, {}) requested_view={} server_max_view={}",
        session->profile_name,
        session->session_id.value,
        session->player_id.value,
        authoritative_center.value.x,
        authoritative_center.value.y,
        authoritative_center.value.z,
        interest.client_view_distance,
        max_view_distance_);
  }
}

void ServerRuntimeImpl::handle_inventory_action_request(
    net::ConnectionID connection_id,
    const protocol::InventoryActionRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated ||
      request.inventory_id != protocol::PlayerInventoryID) {
    return;
  }

  switch (request.kind) {
    case protocol::InventoryActionKind::SlotClick:
      (void)apply_player_inventory_click(
          session->player_id,
          request.slot_index,
          request.button);
      return;
    case protocol::InventoryActionKind::DropCursor:
      (void)drop_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::DropOneCursor:
      (void)drop_one_from_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::CloseMenu:
      (void)close_player_inventory_menu(session->player_id);
      return;
  }
}

} // namespace arv::server::detail
