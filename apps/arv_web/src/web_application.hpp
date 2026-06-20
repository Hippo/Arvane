#pragma once

#include "arv/client/block_container_animation.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/item_rendering.hpp"
#include "arv/client/remote_chunk_stream.hpp"
#include "arv/net/transport.hpp"
#include "arv/platform/input_bindings.hpp"
#include "arv/world/world_time.hpp"
#include "web_client_asset_state.hpp"
#include "web_chunk_renderer.hpp"
#include "web_input_state.hpp"
#include "web_ui_state.hpp"

#include <memory>

namespace arv::web {

class WebRenderHost;

struct WebApplication {
  int width{1};
  int height{1};
  double start_time{};
  bool pointer_locked{};
  bool focused{true};
  bool visible{true};
  bool connected{};
  bool connecting{};
  bool flushed_after_pack_ready{};
  net::ConnectionState last_connection_state{net::ConnectionState::Disconnected};
  std::unique_ptr<client::ClientRuntime> session{};
  client::RemoteChunkSectionStore chunk_stream{};
  world::WorldClock world_clock{world::make_world_clock()};
  platform::InputBindings input_bindings{platform::default_input_bindings()};
  BrowserInputState input{};
  std::unique_ptr<WebRenderHost> render_host{};
  ChunkRenderer renderer{};
  WebBlockRenderAssets block_assets{};
  WebEntityRenderAssets entity_assets{};
  WebUiState ui{};
  client::FirstPersonSneakVisualState first_person_sneak_visual{};
  client::BlockContainerAnimationState block_container_animation{};
};

extern WebApplication g_app;

void reset_input(bool keep_view = false);
[[nodiscard]] bool ui_wants_cursor();

} // namespace arv::web
