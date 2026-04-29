#include "app_config.hpp"
#include "asset_helpers.hpp"
#include "chunk_view.hpp"
#include "input_adapter.hpp"
#include "renderer_bootstrap.hpp"
#include "save_state.hpp"
#include "ui_model.hpp"

#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/ui.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/persistence/save.hpp"
#include "arv/player/item.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
  arv::core::Logger logger;
  logger.info("desktop", "starting Arvane desktop client");

  auto config_result = arv::io::load_config(arv::desktop::parse_config_options(argc, argv));
  if (!config_result) {
    logger.error("config", config_result.error().message);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();
  arv::asset::ResourceManager resources(config.paths.assets_path());

  arv::platform::GlfwLibrary glfw;
  if (!glfw.initialized()) {
    logger.error("platform", "failed to initialize GLFW");
    return 1;
  }

  auto window_result = arv::platform::OpenGLWindow::create({
      config.window.width,
      config.window.height,
      config.window.title});
  if (!window_result) {
    logger.error("platform", window_result.error().message);
    return 1;
  }
  arv::platform::OpenGLWindow window = std::move(window_result.value());
  const auto input_bindings_result =
      arv::io::load_input_bindings_file_or_defaults(config.paths.input_bindings_file);
  if (!input_bindings_result) {
    logger.warn("input", input_bindings_result.error().message);
  } else {
    window.set_input_bindings(input_bindings_result.value());
  }

  auto transport_pair = arv::net::make_in_process_transport_pair();

  arv::render::OpenGLRenderer renderer;
  if (!arv::desktop::configure_renderer(renderer, resources, config, logger)) {
    return 1;
  }

  auto framebuffer = window.framebuffer_size();
  renderer.resize(framebuffer.width, framebuffer.height);

  auto block_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "startup block definitions");
  if (!block_definition_text) {
    logger.error("blocks", block_definition_text.error().message);
    return 1;
  }

  auto block_definitions = arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    logger.error("blocks", block_definitions.error().message);
    return 1;
  }

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    logger.error("blocks", block_registry_result.error().message);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry = std::move(block_registry_result.value());
  const arv::u64 block_registry_hash = arv::persistence::hash_block_registry(block_registry);

  auto block_atlas_result = arv::asset::build_block_texture_atlas(resources, block_registry);
  if (!block_atlas_result) {
    logger.error("assets", block_atlas_result.error().message);
    return 1;
  }
  const arv::asset::BlockTextureAtlas block_atlas = std::move(block_atlas_result.value());
  if (block_atlas.atlas.pages.empty()) {
    logger.error("assets", "block texture atlas did not produce a page");
    return 1;
  }
  renderer.set_chunk_texture_atlas(
      arv::desktop::texture_upload_from_image(block_atlas.atlas.pages.front().image));

  arv::asset::FontSystem font_system;
  auto ui_font = font_system.load_face(
      resources.resolve_asset_path({"arvane:font/font"}, arv::asset::AssetKind::Font),
      18,
      {"arvane:font/font"});
  if (!ui_font) {
    logger.error("assets", ui_font.error().message);
    return 1;
  }
  std::vector<arv::asset::FontFaceHandle> ui_faces{ui_font.value()};
  for (const arv::asset::AssetID fallback : {
           arv::asset::AssetID{"arvane:font/symbol"},
           arv::asset::AssetID{"arvane:font/arabic"},
           arv::asset::AssetID{"arvane:font/emoji"},
       }) {
    const std::filesystem::path path =
        resources.resolve_asset_path(fallback, arv::asset::AssetKind::Font);
    if (!std::filesystem::exists(path)) {
      continue;
    }
    auto face = font_system.load_face(path, 18, fallback);
    if (face) {
      ui_faces.push_back(face.value());
    } else {
      logger.warn("assets", face.error().message);
    }
  }
  const arv::asset::FontStack ui_font_stack = font_system.make_stack(ui_faces);
  arv::client::FontUiTextMeasurer ui_text_measurer(font_system, ui_font_stack);
  arv::client::UiTree ui_tree;

  auto item_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "items.json",
      "startup item definitions");
  if (!item_definition_text) {
    logger.error("items", item_definition_text.error().message);
    return 1;
  }
  auto item_definitions = arv::player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    logger.error("items", item_definitions.error().message);
    return 1;
  }

  auto item_registry_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(item_definitions.value()));
  if (!item_registry_result) {
    logger.error("items", item_registry_result.error().message);
    return 1;
  }
  const arv::player::ItemRegistry item_registry = std::move(item_registry_result.value());
  const arv::u64 item_registry_hash = arv::persistence::hash_item_registry(item_registry);

  auto item_atlas_result = arv::desktop::build_item_texture_atlas(resources, item_registry);
  if (!item_atlas_result) {
    logger.error("assets", item_atlas_result.error().message);
    return 1;
  }
  const arv::asset::TextureAtlas item_atlas = std::move(item_atlas_result.value());
  if (item_atlas.pages.empty()) {
    logger.error("assets", "item texture atlas did not produce a page");
    return 1;
  }
  renderer.set_ui_item_atlas(
      arv::desktop::texture_upload_from_image(item_atlas.pages.front().image));

  const std::filesystem::path save_root = arv::desktop::default_save_root(config);
  auto loaded_save_result =
      arv::persistence::load_server_root(save_root, block_registry, item_registry);
  if (!loaded_save_result) {
    logger.error("save", loaded_save_result.error().message);
    return 1;
  }
  const arv::persistence::LoadedSaveData loaded_save =
      std::move(loaded_save_result.value());

  auto worldgen_text = arv::io::read_text_file(
      config.paths.config_path() / "worldgen/overworld.yaml",
      "overworld generator definition");
  if (!worldgen_text) {
    logger.error("worldgen", worldgen_text.error().message);
    return 1;
  }

  auto worldgen_config = arv::world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    logger.error("worldgen", worldgen_config.error().message);
    return 1;
  }

  arv::world::WorldGenerationConfig generation_config = std::move(worldgen_config.value());
  arv::u64 world_seed = 1;
  std::string world_save_name{"default"};
  std::pair<arv::i32, arv::i32> visible_chunk_y =
      arv::desktop::visible_worldgen_chunk_y_range(generation_config);
  if (loaded_save.has_world_metadata) {
    generation_config = loaded_save.world.generation_config;
    world_seed = loaded_save.world.seed;
    world_save_name = loaded_save.world.name;
    visible_chunk_y = {loaded_save.world.min_chunk_y, loaded_save.world.max_chunk_y};
  }

  auto worldgen_palette = arv::world::resolve_world_generation_palette(
      generation_config,
      block_registry);
  if (!worldgen_palette) {
    logger.error("worldgen", worldgen_palette.error().message);
    return 1;
  }

  const arv::world::WorldGenerationPalette generation_palette =
      std::move(worldgen_palette.value());
  const auto [min_chunk_y, max_chunk_y] = visible_chunk_y;

  arv::server::ServerChunkStreamingConfig chunk_streaming;
  chunk_streaming.max_view_distance = config.chunks.load_distance;
  chunk_streaming.unload_margin = config.chunks.unload_margin;
  chunk_streaming.max_chunks_sent_per_player_tick =
      std::max<arv::u32>(config.chunk_pipeline.max_client_chunk_applies_per_frame, 1U);
  chunk_streaming.max_unloads_sent_per_player_tick = 64;

  arv::server::ServerChunkGenerationSettings chunk_generation;
  chunk_generation.enabled = true;
  chunk_generation.seed = world_seed;
  chunk_generation.config = generation_config;
  chunk_generation.palette = generation_palette;

  arv::server::ServerRuntime integrated_server(std::move(transport_pair.server));
  auto server_start = integrated_server.start({
      arv::server::ServerMode::Integrated,
      config.simulation.base_tps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition(),
      chunk_streaming,
      chunk_generation,
      config.chunk_pipeline.max_generation_results_per_tick,
      config.chunk_pipeline.max_lighting_results_per_tick,
      config.chunk_pipeline.max_generation_jobs_per_frame});
  if (!server_start) {
    logger.error("server", server_start.error().message);
    return 1;
  }

  if (loaded_save.has_world_metadata) {
    integrated_server.voxel_world().set_tick_index(loaded_save.world.tick_index);
  }
  for (const arv::persistence::SavedChunkSection& chunk : loaded_save.chunks) {
    integrated_server.voxel_world().restore_saved_chunk(
        chunk.coord,
        chunk.blocks,
        chunk.lights);
  }

  arv::client::ClientRuntime client(std::move(transport_pair.client));
  client.set_chunk_view_distance(config.chunks.stream_distance);
  auto connect_result = client.connect_integrated();
  if (!connect_result) {
    logger.error("client", connect_result.error().message);
    return 1;
  }

  arv::world::WorldRuntime client_world{arv::server::DefaultWorldID};
  arv::desktop::ChunkViewState chunk_view;
  std::deque<arv::protocol::ChunkUnload> pending_client_chunk_unloads;
  std::deque<arv::protocol::ChunkData> pending_client_chunk_data;
  auto collect_client_chunk_stream = [&]() {
    std::vector<arv::protocol::ChunkUnload> drained_unloads =
        client.drain_received_chunk_unloads();
    for (const arv::protocol::ChunkUnload& unload : drained_unloads) {
      pending_client_chunk_unloads.push_back(unload);
    }

    std::vector<arv::protocol::ChunkData> drained_chunks =
        client.drain_received_chunk_data();
    for (arv::protocol::ChunkData& data : drained_chunks) {
      pending_client_chunk_data.push_back(std::move(data));
    }
  };
  auto apply_pending_client_chunk_stream = [&](arv::u32 chunk_budget, arv::u32 unload_budget) {
    bool changed = false;
    arv::u32 unloads_applied = 0;
    while (unloads_applied < unload_budget && !pending_client_chunk_unloads.empty()) {
      const arv::protocol::ChunkUnload unload = pending_client_chunk_unloads.front();
      pending_client_chunk_unloads.pop_front();
      ++unloads_applied;
      if (unload.world_id != client_world.id()) {
        continue;
      }
      if (client_world.unload_chunk(unload.coord)) {
        renderer.remove_chunk_mesh({unload.world_id, unload.coord});
        arv::desktop::remove_chunk_view_chunk(chunk_view, unload.coord);
        changed = true;
      }
    }

    arv::u32 chunks_applied = 0;
    while (chunks_applied < chunk_budget && !pending_client_chunk_data.empty()) {
      arv::protocol::ChunkData data = std::move(pending_client_chunk_data.front());
      pending_client_chunk_data.pop_front();
      ++chunks_applied;
      if (data.world_id != client_world.id()) {
        continue;
      }

      std::optional<std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume>> lights;
      if (data.has_lighting) {
        lights = std::move(data.lights);
      }
      client_world.apply_chunk_snapshot(
          data.coord,
          std::move(data.blocks),
          std::move(lights));
      changed = true;
    }

    if (changed) {
      arv::desktop::request_chunk_view_mesh_rescan(chunk_view);
    }
  };

  integrated_server.tick();
  client.fixed_tick(integrated_server.tick_index());
  collect_client_chunk_stream();
  apply_pending_client_chunk_stream(
      config.chunk_pipeline.max_client_chunk_applies_per_frame,
      config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  arv::desktop::apply_loaded_player_saves(
      integrated_server,
      world_save_name,
      loaded_save.players);
  if (!loaded_save.players.empty()) {
    integrated_server.tick();
    client.fixed_tick(integrated_server.tick_index());
    collect_client_chunk_stream();
    apply_pending_client_chunk_stream(
        config.chunk_pipeline.max_client_chunk_applies_per_frame,
        config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  }

  arv::sched::WorkerThreadConfig worker_thread_config;
  worker_thread_config.worker_thread_count = config.jobs.worker_threads;
  worker_thread_config.reserved_thread_count = config.jobs.reserved_threads;
  worker_thread_config.min_worker_thread_count = config.jobs.min_worker_threads;
  worker_thread_config.max_worker_thread_count = config.jobs.max_worker_threads;
  arv::sched::Scheduler world_jobs{worker_thread_config};
  {
    std::ostringstream message;
    message << "worker_threads=" << world_jobs.worker_count()
            << " reserved_threads=" << config.jobs.reserved_threads;
    logger.info("jobs", message.str());
  }
  if (!arv::desktop::update_chunk_view(
          client_world,
          renderer,
          world_jobs,
          chunk_view,
          config.chunks,
          config.chunk_pipeline,
          arv::desktop::camera_chunk_coord(client.camera()),
          min_chunk_y,
          max_chunk_y,
          block_registry,
          block_atlas.render_data,
          logger)) {
    return 1;
  }

  arv::core::FixedTimestepAccumulator ticks(config.simulation.base_tps);
  auto previous_time = std::chrono::steady_clock::now();
  auto last_fps_display_time = previous_time;
  arv::u32 displayed_fps = 0;
  arv::u32 fps_sample_frames = 0;
  arv::Seconds fps_sample_seconds = 0.0F;
  constexpr auto FpsHudRefreshInterval = std::chrono::milliseconds(250);
  std::vector<arv::client::UiItemStackView> inventory_slots;
  std::vector<arv::client::PlayerListEntryView> tab_players;
  bool ui_snapshot_initialized = false;
  auto refresh_inventory_slots = [&]() {
    inventory_slots.clear();
    const std::vector<arv::SessionID> ui_session_ids =
        integrated_server.sessions().session_ids();
    if (const arv::server::ServerSession* session =
            ui_session_ids.empty()
                ? nullptr
                : integrated_server.sessions().find(ui_session_ids.front());
        session != nullptr) {
      if (const arv::player::PlayerState* player_state =
              integrated_server.voxel_world().find_player(session->player_id);
          player_state != nullptr) {
        inventory_slots = arv::desktop::inventory_slots_for_ui(
            player_state->inventory,
            item_registry,
            item_atlas,
            block_atlas);
      }
    }
  };

  while (!window.should_close()) {
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<arv::Seconds> delta = now - previous_time;
    previous_time = now;
    const double frame_delta_ms =
        std::chrono::duration<double, std::milli>(delta).count();
    ++fps_sample_frames;
    fps_sample_seconds += delta.count();
    if (displayed_fps == 0U ||
        now - last_fps_display_time >= FpsHudRefreshInterval) {
      displayed_fps = fps_sample_seconds > 0.0F
                          ? static_cast<arv::u32>(
                                std::round(static_cast<double>(fps_sample_frames) /
                                           static_cast<double>(fps_sample_seconds)))
                          : 0U;
      last_fps_display_time = now;
      fps_sample_frames = 0;
      fps_sample_seconds = 0.0F;
    }
    if (frame_delta_ms > static_cast<double>(config.chunk_pipeline.frame_spike_log_ms)) {
      std::ostringstream message;
      message << "frame spike " << frame_delta_ms << "ms"
              << " queued_mesh=" << chunk_view.pending_meshes.size()
              << " pending_uploads=" << renderer.pending_mesh_upload_count()
              << " uploads_last_frame=" << renderer.mesh_uploads_last_frame();
      logger.warn("chunk_profile", message.str());
    }

    arv::platform::OpenGLWindow::poll_events();
    const arv::platform::InputSnapshot platform_input = window.poll_input();
    if (platform_input.close_requested) {
      window.request_close();
    }
    client.submit_input(arv::desktop::to_client_input(platform_input));

    ticks.add_time(delta.count());
    bool fixed_tick_ran = false;
    while (ticks.consume_tick()) {
      integrated_server.tick();
      client.fixed_tick(integrated_server.tick_index());
      collect_client_chunk_stream();
      fixed_tick_ran = true;
    }
    apply_pending_client_chunk_stream(
        config.chunk_pipeline.max_client_chunk_applies_per_frame,
        config.chunk_pipeline.max_client_chunk_unloads_per_frame);

    if (!arv::desktop::update_chunk_view(
            client_world,
            renderer,
            world_jobs,
            chunk_view,
            config.chunks,
            config.chunk_pipeline,
            arv::desktop::camera_chunk_coord(client.camera()),
            min_chunk_y,
            max_chunk_y,
            block_registry,
            block_atlas.render_data,
            logger)) {
      return 1;
    }

    const auto next_framebuffer = window.framebuffer_size();
    if (next_framebuffer.width != framebuffer.width ||
        next_framebuffer.height != framebuffer.height) {
      framebuffer = next_framebuffer;
      renderer.resize(framebuffer.width, framebuffer.height);
    }
    arv::render::FramePacket frame = client.build_frame_packet(
        ticks.interpolation_alpha(),
        framebuffer.width,
        framebuffer.height);
    frame.opaque_chunks = arv::desktop::chunk_draw_items(chunk_view);

    if (!ui_snapshot_initialized || fixed_tick_ran) {
      refresh_inventory_slots();
      if (client.tab_menu_visible()) {
        tab_players = arv::desktop::player_list_for_ui(integrated_server);
      } else {
        tab_players.clear();
      }
      ui_snapshot_initialized = true;
    } else if (client.tab_menu_visible() && tab_players.empty()) {
      tab_players = arv::desktop::player_list_for_ui(integrated_server);
    } else if (!client.tab_menu_visible() && !tab_players.empty()) {
      tab_players.clear();
    }
    arv::client::build_gameplay_ui(
        ui_tree,
        {
            {
                client.selected_hotbar_slot(),
                arv::player::DefaultHotbarSlotCount,
                {
                    static_cast<arv::f32>(framebuffer.width),
                    static_cast<arv::f32>(framebuffer.height),
                },
                std::span<const arv::client::UiItemStackView>{
                    inventory_slots.data(),
                    std::min<arv::usize>(
                        inventory_slots.size(),
                        arv::player::DefaultHotbarSlotCount)},
            },
            {
                client.inventory_open(),
                "Inventory",
                client.selected_hotbar_slot(),
                arv::player::DefaultHotbarSlotCount,
                {
                    static_cast<arv::f32>(framebuffer.width),
                    static_cast<arv::f32>(framebuffer.height),
                },
                std::span<const arv::client::UiItemStackView>{
                    inventory_slots.data(),
                    inventory_slots.size()},
            },
            {
                true,
                client.chat_open(),
                {},
                {},
            },
            {
                client.tab_menu_visible(),
                "Players",
                "",
                std::span<const arv::client::PlayerListEntryView>{
                    tab_players.data(),
                    tab_players.size()},
            },
            {},
            {true, displayed_fps},
        });
    ui_tree.layout(
        {
            static_cast<arv::f32>(framebuffer.width),
            static_cast<arv::f32>(framebuffer.height),
        },
        &ui_text_measurer);
    arv::client::append_ui_draw_commands(ui_tree, font_system, ui_font_stack, frame);
    if (font_system.atlas_dirty()) {
      renderer.set_ui_font_atlas(
          arv::desktop::texture_upload_from_image(font_system.atlas_image()));
      font_system.clear_atlas_dirty();
    }
    renderer.submit_frame(std::move(frame));
    renderer.render();
    window.swap_buffers();
  }

  const std::vector<arv::persistence::SavedChunkSection> saved_chunks =
      arv::desktop::capture_saved_chunks(integrated_server.voxel_world());
  const std::vector<arv::persistence::PlayerSaveData> player_saves =
      arv::desktop::capture_player_saves(integrated_server, world_save_name);
  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      block_registry_hash,
      item_registry_hash};
  const arv::persistence::WorldMetadata world_metadata{
      world_save_name,
      world_seed,
      integrated_server.voxel_world().tick_index(),
      generation_config,
      min_chunk_y,
      max_chunk_y,
      block_registry_hash,
      item_registry_hash};
  auto save_result = arv::persistence::save_server_root(
      save_root,
      save_metadata,
      world_metadata,
      saved_chunks,
      player_saves,
      block_registry,
      item_registry);

  renderer.begin_shutdown();
  integrated_server.shutdown();
  if (!save_result) {
    logger.error("save", save_result.error().message);
    return 1;
  }
  logger.info("desktop", "shutdown complete");
  return 0;
}
