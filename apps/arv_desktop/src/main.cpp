#include "arv/client/client_runtime.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

arv::io::ConfigLoadOptions parse_config_options(int argc, char** argv) {
  arv::io::ConfigLoadOptions options;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--config") {
      options.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--asset-root") {
      options.explicit_asset_root = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.missing_config_is_error = explicit_config;
  return options;
}

arv::client::InputFrame to_client_input(const arv::platform::InputSnapshot& input) {
  return {
      input.move_forward,
      input.move_backward,
      input.move_left,
      input.move_right,
      input.jump,
      input.move_down,
      input.fast,
      input.mouse_delta_x,
      input.mouse_delta_y};
}

struct ChunkViewState {
  std::unordered_map<arv::ChunkCoord, arv::render::ChunkDrawItem> draw_items;
};

arv::ChunkCoord camera_chunk_coord(const arv::client::CameraShell& camera) {
  const arv::Vec3f& position = camera.position();
  return arv::block_to_chunk({{
      static_cast<arv::i32>(std::floor(position.x)),
      static_cast<arv::i32>(std::floor(position.y)),
      static_cast<arv::i32>(std::floor(position.z))}});
}

std::pair<arv::i32, arv::i32> visible_worldgen_chunk_y_range(
    const arv::world::WorldGenerationConfig& config) {
  const arv::f32 height_extent =
      std::abs(static_cast<arv::f32>(config.height_scale)) * config.terrain_amplitude + 4.0F;
  const arv::i32 surface_min =
      static_cast<arv::i32>(std::floor(static_cast<arv::f32>(config.base_height) - height_extent));
  const arv::i32 surface_max =
      static_cast<arv::i32>(std::ceil(static_cast<arv::f32>(config.base_height) + height_extent));
  const arv::i32 min_y = std::max(config.min_y, surface_min - arv::ChunkSize);
  const arv::i32 max_y = std::min(config.max_y, surface_max);
  return {arv::floor_div(min_y, arv::ChunkSize), arv::floor_div(max_y, arv::ChunkSize)};
}

bool is_inside_horizontal_distance(arv::ChunkCoord coord, arv::ChunkCoord center, arv::i32 radius) {
  return std::abs(coord.value.x - center.value.x) <= radius &&
         std::abs(coord.value.z - center.value.z) <= radius;
}

void remove_draw_items_outside_view(
    ChunkViewState& view,
    arv::ChunkCoord center,
    arv::i32 render_distance,
    arv::i32 min_chunk_y,
    arv::i32 max_chunk_y) {
  for (auto iter = view.draw_items.begin(); iter != view.draw_items.end();) {
    const arv::ChunkCoord coord = iter->first;
    const bool inside = coord.value.y >= min_chunk_y && coord.value.y <= max_chunk_y &&
                        is_inside_horizontal_distance(coord, center, render_distance);
    if (!inside) {
      iter = view.draw_items.erase(iter);
    } else {
      ++iter;
    }
  }
}

bool refresh_chunk_view(
    arv::world::WorldRuntime& world,
    arv::render::OpenGLRenderer& renderer,
    arv::sched::Scheduler& scheduler,
    ChunkViewState& view,
    const arv::io::ChunkDistanceConfig& distances,
    arv::ChunkCoord center,
    arv::i32 min_chunk_y,
    arv::i32 max_chunk_y,
    const arv::world::WorldGenerationConfig& generation_config,
    const arv::world::WorldGenerationPalette& generation_palette,
    const arv::voxel::BlockRegistry& block_registry,
    arv::core::Logger& logger) {
  struct PendingGeneration {
    arv::sched::JobHandle<arv::world::GeneratedChunkResult> job;
  };

  std::vector<PendingGeneration> pending_generations;
  for (arv::i32 z = center.value.z - distances.load_distance;
       z <= center.value.z + distances.load_distance;
       ++z) {
    for (arv::i32 x = center.value.x - distances.load_distance;
         x <= center.value.x + distances.load_distance;
         ++x) {
      for (arv::i32 y = min_chunk_y; y <= max_chunk_y; ++y) {
        const arv::ChunkCoord coord{{x, y, z}};
        if (world.find_chunk(coord) != nullptr) {
          continue;
        }

        arv::world::ChunkGenerationRequest request;
        request.world_id = arv::server::DefaultWorldID;
        request.seed = 1;
        request.coord = coord;
        request.config = generation_config;
        request.palette = generation_palette;
        auto job = scheduler.submit("view_chunk_generation", [request] {
          return arv::world::generate_chunk(request);
        });
        pending_generations.push_back({std::move(job)});
      }
    }
  }

  for (PendingGeneration& pending : pending_generations) {
    auto generated = pending.job.get();
    if (!generated) {
      logger.error("worldgen", generated.error().message);
      return false;
    }
    if (!world.publish_generated_chunk(std::move(generated.value()))) {
      logger.error("worldgen", "failed to publish generated view chunk");
      return false;
    }
  }

  // TODO(arvane): Apply stream_distance and unload_margin once networking and
  // save-aware chunk unload candidates exist.
  world.update_simulation_radius(center, distances.simulation_distance);
  (void)world.process_lighting(block_registry);

  remove_draw_items_outside_view(
      view,
      center,
      distances.render_distance,
      min_chunk_y,
      max_chunk_y);

  for (arv::i32 z = center.value.z - distances.render_distance;
       z <= center.value.z + distances.render_distance;
       ++z) {
    for (arv::i32 x = center.value.x - distances.render_distance;
         x <= center.value.x + distances.render_distance;
         ++x) {
      for (arv::i32 y = min_chunk_y; y <= max_chunk_y; ++y) {
        const arv::ChunkCoord coord{{x, y, z}};
        const arv::voxel::ChunkSection* chunk = world.find_chunk(coord);
        if (chunk == nullptr) {
          continue;
        }
        if (!chunk->is_dirty(arv::voxel::ChunkDirtyFlags::NeedsMesh) &&
            view.draw_items.find(coord) != view.draw_items.end()) {
          continue;
        }

        const std::optional<arv::voxel::ChunkSnapshot> snapshot =
            world.make_meshing_snapshot(
                coord,
                arv::world::MissingNeighborPolicy::TreatMissingAsAir);
        if (!snapshot) {
          continue;
        }

        arv::render::ChunkMeshData mesh =
            arv::render::build_chunk_mesh(arv::server::DefaultWorldID, snapshot.value(), block_registry);
        if (!world.validate_mesh_result(mesh.coord, mesh.source_version)) {
          continue;
        }

        arv::render::ChunkDrawItem draw_item;
        draw_item.key = {mesh.world_id, mesh.coord};
        draw_item.expected_version = mesh.source_version;
        arv::render::ChunkMeshUpload upload;
        upload.mesh = std::move(mesh);
        renderer.enqueue_chunk_mesh_upload(std::move(upload));
        (void)world.publish_mesh_result(draw_item.key.coord, draw_item.expected_version);
        view.draw_items[coord] = draw_item;
      }
    }
  }

  return true;
}

std::vector<arv::render::ChunkDrawItem> collect_draw_items(const ChunkViewState& view) {
  std::vector<arv::render::ChunkDrawItem> items;
  items.reserve(view.draw_items.size());
  for (const auto& [coord, item] : view.draw_items) {
    (void)coord;
    items.push_back(item);
  }
  std::sort(items.begin(), items.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.key.coord.value.z != rhs.key.coord.value.z) {
      return lhs.key.coord.value.z < rhs.key.coord.value.z;
    }
    if (lhs.key.coord.value.x != rhs.key.coord.value.x) {
      return lhs.key.coord.value.x < rhs.key.coord.value.x;
    }
    return lhs.key.coord.value.y < rhs.key.coord.value.y;
  });
  return items;
}

} // namespace

int main(int argc, char** argv) {
  arv::core::Logger logger;
  logger.info("desktop", "starting Arvane desktop client");

  auto config_result = arv::io::load_config(parse_config_options(argc, argv));
  if (!config_result) {
    logger.error("config", config_result.error().message);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();

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

  auto transport_pair = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime integrated_server(std::move(transport_pair.server));
  auto server_start = integrated_server.start({
      arv::server::ServerMode::Integrated,
      config.simulation.base_tps,
      {"in-process", 0}});
  if (!server_start) {
    logger.error("server", server_start.error().message);
    return 1;
  }

  arv::client::ClientRuntime client(std::move(transport_pair.client));
  auto connect_result = client.connect_integrated();
  if (!connect_result) {
    logger.error("client", connect_result.error().message);
    return 1;
  }

  arv::render::OpenGLRenderer renderer;
  auto chunk_vertex_shader = arv::io::read_text_file(
      config.paths.asset_root / "shaders/world/chunk.vert",
      "chunk vertex shader");
  if (!chunk_vertex_shader) {
    logger.error("assets", chunk_vertex_shader.error().message);
    return 1;
  }
  auto chunk_fragment_shader = arv::io::read_text_file(
      config.paths.asset_root / "shaders/world/chunk.frag",
      "chunk fragment shader");
  if (!chunk_fragment_shader) {
    logger.error("assets", chunk_fragment_shader.error().message);
    return 1;
  }
  renderer.set_chunk_shader_sources(
      std::move(chunk_vertex_shader.value()),
      std::move(chunk_fragment_shader.value()));

  auto framebuffer = window.framebuffer_size();
  renderer.resize(framebuffer.width, framebuffer.height);

  auto block_definition_text = arv::io::read_text_file(
      config.paths.asset_root / "config/blocks.json",
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

  auto block_registry_result = arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    logger.error("blocks", block_registry_result.error().message);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry = std::move(block_registry_result.value());

  auto worldgen_text = arv::io::read_text_file(
      config.paths.asset_root / "config/worldgen/overworld.yaml",
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

  auto worldgen_palette = arv::world::resolve_world_generation_palette(
      worldgen_config.value(),
      block_registry);
  if (!worldgen_palette) {
    logger.error("worldgen", worldgen_palette.error().message);
    return 1;
  }

  const arv::world::WorldGenerationConfig generation_config = std::move(worldgen_config.value());
  const arv::world::WorldGenerationPalette generation_palette = std::move(worldgen_palette.value());
  const auto [min_chunk_y, max_chunk_y] = visible_worldgen_chunk_y_range(generation_config);
  arv::sched::Scheduler world_jobs;
  ChunkViewState chunk_view;

  if (!refresh_chunk_view(
          integrated_server.voxel_world(),
          renderer,
          world_jobs,
          chunk_view,
          config.chunks,
          camera_chunk_coord(client.camera()),
          min_chunk_y,
          max_chunk_y,
          generation_config,
          generation_palette,
          block_registry,
          logger)) {
    return 1;
  }

  arv::core::FixedTimestepAccumulator ticks(config.simulation.base_tps);
  auto previous_time = std::chrono::steady_clock::now();

  while (!window.should_close()) {
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<arv::Seconds> delta = now - previous_time;
    previous_time = now;

    arv::platform::OpenGLWindow::poll_events();
    const arv::platform::InputSnapshot platform_input = window.poll_input();
    if (platform_input.close_requested) {
      window.request_close();
    }
    client.submit_input(to_client_input(platform_input));
    client.update_freecam(delta.count());
    if (!refresh_chunk_view(
            integrated_server.voxel_world(),
            renderer,
            world_jobs,
            chunk_view,
            config.chunks,
            camera_chunk_coord(client.camera()),
            min_chunk_y,
            max_chunk_y,
            generation_config,
            generation_palette,
            block_registry,
            logger)) {
      return 1;
    }

    ticks.add_time(delta.count());
    while (ticks.consume_tick()) {
      integrated_server.tick();
      (void)integrated_server.voxel_world().tick(
          block_registry,
          arv::voxel::make_water_fluid_definition());
      client.fixed_tick(integrated_server.tick_index());
    }

    framebuffer = window.framebuffer_size();
    renderer.resize(framebuffer.width, framebuffer.height);
    arv::render::FramePacket frame = client.build_frame_packet(
        ticks.interpolation_alpha(),
        framebuffer.width,
        framebuffer.height);
    frame.opaque_chunks = collect_draw_items(chunk_view);
    renderer.submit_frame(std::move(frame));
    renderer.render();
    window.swap_buffers();
  }

  renderer.begin_shutdown();
  integrated_server.shutdown();
  logger.info("desktop", "shutdown complete");
  return 0;
}
