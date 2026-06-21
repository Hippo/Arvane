#include "web_client_host.hpp"

#include "arv/client/client_runtime.hpp"
#include "arv/client/client_world_time.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/net/websocket_client_transport.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "web_application.hpp"
#include "web_canvas_context.hpp"
#include "web_client_assets.hpp"
#include "web_platform.hpp"
#include "web_profile.hpp"
#include "web_render_host.hpp"
#include "web_ui_host.hpp"

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arv::web {
namespace {

void apply_web_world_time_update(const protocol::ClientboundWorldTime& time) {
  if (g_app.session != nullptr &&
      g_app.session->connection_state() == net::ConnectionState::InGame &&
      time.world_id != g_app.session->camera().world_id()) {
    return;
  }
  g_app.world_clock = client::world_clock_from_time_packet(time);
}

void apply_web_world_time_updates() {
  if (g_app.session == nullptr) {
    return;
  }
  if (const std::optional<protocol::ClientboundWorldTime> time =
          g_app.session->take_world_time_update();
      time.has_value()) {
    apply_web_world_time_update(time.value());
  }
}

const char* connection_state_label(net::ConnectionState state) {
  switch (state) {
    case net::ConnectionState::Disconnected:
      return "disconnected";
    case net::ConnectionState::Connecting:
      return "connecting WebSocket";
    case net::ConnectionState::TransportConnected:
      return "transport connected";
    case net::ConnectionState::ProtocolHandshake:
      return "protocol handshake";
    case net::ConnectionState::RegistrySync:
      return "registry sync";
    case net::ConnectionState::WorldJoin:
      return "loading world";
    case net::ConnectionState::InGame:
      return "in game";
    case net::ConnectionState::Disconnecting:
      return "disconnecting";
  }
  return "unknown";
}

WebShellState shell_state_for_connection(net::ConnectionState state) noexcept {
  switch (state) {
    case net::ConnectionState::InGame:
      return WebShellState::Game;
    case net::ConnectionState::Disconnected:
      return WebShellState::Connect;
    case net::ConnectionState::Connecting:
    case net::ConnectionState::TransportConnected:
    case net::ConnectionState::ProtocolHandshake:
    case net::ConnectionState::RegistrySync:
    case net::ConnectionState::WorldJoin:
    case net::ConnectionState::Disconnecting:
      return WebShellState::Connecting;
  }
  return WebShellState::Connect;
}

[[nodiscard]] WorldID active_web_world_id() noexcept {
  return g_app.session == nullptr
             ? WorldID{}
             : g_app.session->camera().world_id();
}

void reset_web_session_host_state() {
  clear_block_render_assets();
  g_app.chunk_stream.clear();
  g_app.ui.screen_manager.clear();
  g_app.world_clock = world::make_world_clock();
  reset_input(true);
  g_app.first_person_sneak_visual = {};
  g_app.block_container_animation = {};
}

} // namespace

void initialize_web_client_host() noexcept {
  set_web_ui_disconnect_callback(disconnect_web_session);
}

void collect_web_runtime_chunk_stream() {
  if (g_app.session == nullptr) {
    return;
  }

  client::ClientChunkStreamPackets packets =
      g_app.session->drain_chunk_stream_packets();
  (void)client::apply_remote_chunk_stream_packets(
      g_app.chunk_stream,
      active_web_world_id(),
      std::move(packets));
}

void poll_web_session() {
  if (g_app.session == nullptr) {
    return;
  }

  const net::ConnectionState state = g_app.session->connection_state();
  g_app.connected = state != net::ConnectionState::Disconnected &&
                    state != net::ConnectionState::Connecting;
  g_app.connecting = state == net::ConnectionState::Connecting ||
                     state == net::ConnectionState::ProtocolHandshake;
  set_shell_state(shell_state_for_connection(state));
  if (state != g_app.last_connection_state) {
    g_app.last_connection_state = state;
    if (state == net::ConnectionState::Disconnected &&
        g_app.session->disconnect_reason().has_value() &&
        !g_app.session->disconnect_reason()->empty()) {
      set_status("disconnected: " + *g_app.session->disconnect_reason());
    } else {
      set_status(connection_state_label(state));
    }
  }
  apply_web_world_time_updates();
  if (!g_app.flushed_after_pack_ready &&
      (state == net::ConnectionState::WorldJoin ||
       state == net::ConnectionState::InGame) &&
      !g_app.session->multiplayer_registry_snapshot_json().empty()) {
    g_app.flushed_after_pack_ready = true;
    set_status(
        "server registry verified; syncing " +
        std::to_string(g_app.session->multiplayer_pack_cache_paths().size()) +
        " cached pack root(s)");
    flush_storage();
  }
  if (state == net::ConnectionState::Disconnected) {
    g_app.flushed_after_pack_ready = false;
    reset_web_session_host_state();
  } else if (state == net::ConnectionState::InGame &&
             g_app.ui.screen_manager.empty()) {
    reset_gameplay_screen_stack();
  }
}

void connect_web_session(const char* url) {
  if (!storage_ready()) {
    set_status("browser storage is not ready");
    return;
  }
  if (url == nullptr || std::strlen(url) == 0U) {
    set_status("missing WebSocket URL");
    return;
  }

  if (g_app.session != nullptr) {
    disconnect_web_session();
  }
  reset_web_session_host_state();
  auto transport = std::make_unique<net::WebSocketClientTransport>();
  auto identity = load_web_local_identity();
  if (!identity) {
    set_shell_state(WebShellState::Connect);
    set_status("profile load failed: " + identity.error().message);
    return;
  }
  g_app.session = std::make_unique<client::ClientRuntime>(std::move(transport));
  g_app.session->set_local_identity(std::move(identity.value()));
  apply_web_ui_settings_to_session(*g_app.session);
  net::NetworkAddress address;
  address.host = url;
  address.port = 0U;
  auto connected = g_app.session->connect(address);
  if (!connected) {
    g_app.connected = false;
    g_app.connecting = false;
    g_app.session.reset();
    set_shell_state(WebShellState::Connect);
    set_status("WebSocket connect failed: " + connected.error().message);
    return;
  }

  g_app.connected = false;
  g_app.connecting = true;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = net::ConnectionState::Connecting;
  set_shell_state(WebShellState::Connecting);
  set_status("connecting WebSocket");
}

void disconnect_web_session() {
  if (g_app.session != nullptr) {
    g_app.session->disconnect();
    g_app.session.reset();
  }
  reset_web_session_host_state();
  flush_storage();
  g_app.connected = false;
  g_app.connecting = false;
  g_app.flushed_after_pack_ready = false;
  g_app.last_connection_state = net::ConnectionState::Disconnected;
  set_shell_state(WebShellState::Connect);
  set_status("disconnected");
  if (web_canvas_context_ready()) {
    clear_web_render_frame(0.09F, 0.12F, 0.14F, 1.0F);
  }
}

} // namespace arv::web
