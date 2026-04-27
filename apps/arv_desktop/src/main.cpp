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
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    } else if (arg == "--arvane-home") {
      options.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
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
      input.jump_pressed,
      input.jump_held,
      input.sneak_held,
      input.sprint_held,
      input.primary_pressed,
      input.primary_held,
      input.secondary_pressed,
      input.secondary_held,
      input.toggle_fly_pressed,
      input.open_inventory_pressed,
      input.open_chat_pressed,
      input.tab_menu_held,
      input.hotbar_select,
      input.hotbar_scroll_steps,
      input.mouse_delta_x,
      input.mouse_delta_y};
}

std::filesystem::path default_save_root(const arv::io::EngineConfig& config) {
  return config.paths.server_save_path(arv::io::DefaultServerSaveName);
}

arv::render::TextureUploadData texture_upload_from_image(const arv::asset::Image& image) {
  return {image.width, image.height, image.pixels};
}

std::vector<arv::persistence::SavedChunkSection> capture_saved_chunks(
    const arv::world::WorldRuntime& world) {
  std::vector<arv::ChunkCoord> coords = world.loaded_chunk_coords();
  std::sort(coords.begin(), coords.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.value.x != rhs.value.x) {
      return lhs.value.x < rhs.value.x;
    }
    if (lhs.value.z != rhs.value.z) {
      return lhs.value.z < rhs.value.z;
    }
    return lhs.value.y < rhs.value.y;
  });

  std::vector<arv::persistence::SavedChunkSection> chunks;
  chunks.reserve(coords.size());
  for (const arv::ChunkCoord coord : coords) {
    const arv::voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }

    arv::persistence::SavedChunkSection saved;
    saved.coord = coord;
    saved.blocks = chunk->blocks();
    saved.lights = chunk->lights();
    saved.lighting_valid = !chunk->is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
    chunks.push_back(std::move(saved));
  }
  return chunks;
}

std::vector<arv::persistence::PlayerSaveData> capture_player_saves(
    const arv::server::ServerRuntime& server,
    std::string_view world_name) {
  std::vector<arv::SessionID> session_ids = server.sessions().session_ids();
  std::sort(session_ids.begin(), session_ids.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.value < rhs.value;
  });

  std::vector<arv::persistence::PlayerSaveData> players;
  players.reserve(session_ids.size());
  for (const arv::SessionID session_id : session_ids) {
    const arv::server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const arv::player::PlayerState* player_state = server.voxel_world().find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    arv::persistence::PlayerSaveData save;
    save.profile_name = session->profile_name;
    save.world_name = std::string{world_name};
    save.position = player_state->position;
    save.velocity = player_state->velocity;
    save.yaw_radians = player_state->yaw_radians;
    save.pitch_radians = player_state->pitch_radians;
    save.grounded = player_state->grounded;
    save.movement_mode = player_state->movement_mode;
    save.game_mode_id = player_state->game_mode_id;
    save.selected_hotbar_slot = player_state->selected_hotbar_slot;
    save.inventory = player_state->inventory;
    players.push_back(std::move(save));
  }

  return players;
}

std::vector<arv::client::UiItemStackView> inventory_slots_for_ui(
    const arv::player::Inventory& inventory,
    const arv::player::ItemRegistry& item_registry) {
  std::vector<arv::client::UiItemStackView> slots;
  slots.reserve(inventory.slot_count());
  for (arv::usize index = 0; index < inventory.slot_count(); ++index) {
    arv::client::UiItemStackView view;
    const std::optional<arv::player::ItemStack>& stack = inventory.slot(index);
    if (stack.has_value()) {
      const arv::player::ItemRecord* item = item_registry.item(stack->item);
      view.item_name = item == nullptr ? std::string{arv::player::MissingItemName} : item->name;
      view.display_name = item == nullptr ? view.item_name : item->display_name;
      view.count = stack->count;
    }
    slots.push_back(std::move(view));
  }
  return slots;
}

std::vector<arv::client::PlayerListEntryView> player_list_for_ui(
    const arv::server::ServerRuntime& server) {
  std::vector<arv::client::PlayerListEntryView> players;
  for (const arv::SessionID session_id : server.sessions().session_ids()) {
    const arv::server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }
    players.push_back({session->profile_name, std::nullopt});
  }
  return players;
}

void apply_loaded_player_saves(
    arv::server::ServerRuntime& server,
    std::string_view world_name,
    const std::vector<arv::persistence::PlayerSaveData>& loaded_players) {
  for (const arv::SessionID session_id : server.sessions().session_ids()) {
    const arv::server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const auto found = std::find_if(
        loaded_players.begin(),
        loaded_players.end(),
        [&](const arv::persistence::PlayerSaveData& player) {
          return player.profile_name == session->profile_name &&
                 player.world_name == world_name;
        });
    if (found == loaded_players.end()) {
      continue;
    }

    arv::player::PlayerState& player_state = server.voxel_world().ensure_player(session->player_id);
    player_state.position = found->position;
    player_state.velocity = found->velocity;
    player_state.yaw_radians = found->yaw_radians;
    player_state.pitch_radians = found->pitch_radians;
    player_state.grounded = found->grounded;
    player_state.movement_mode = found->movement_mode;
    player_state.game_mode_id = found->game_mode_id;
    player_state.selected_hotbar_slot = found->selected_hotbar_slot;
    player_state.inventory = found->inventory;
  }
}

struct ChunkViewState {
  struct TimedGeneratedChunkResult {
    arv::world::GeneratedChunkResult result;
    double duration_ms{};
  };

  struct TimedChunkMeshResult {
    arv::render::ChunkMeshData mesh;
    double duration_ms{};
  };

  struct TimedLightingResult {
    arv::world::LightingResult result;
    double duration_ms{};
  };

  struct PendingGeneration {
    arv::ChunkCoord coord{};
    arv::sched::JobHandle<TimedGeneratedChunkResult> job;
  };

  struct PendingMesh {
    arv::ChunkCoord coord{};
    arv::u64 source_version{};
    arv::sched::JobHandle<TimedChunkMeshResult> job;
  };

  std::unordered_map<arv::ChunkCoord, arv::render::ChunkDrawItem> draw_items;
  std::vector<PendingGeneration> pending_generations;
  std::vector<PendingMesh> pending_meshes;
  std::unordered_set<arv::ChunkCoord> generation_in_flight;
  std::unordered_set<arv::ChunkCoord> mesh_in_flight;
  std::optional<arv::sched::JobHandle<TimedLightingResult>> lighting_job;
  std::chrono::steady_clock::time_point last_profile_log{};
  arv::u64 frames{};
  arv::u32 generated_completed_last_frame{};
  arv::u32 mesh_completed_last_frame{};
  arv::u32 lighting_completed_last_frame{};
  double generation_ms_last_frame{};
  double mesh_ms_last_frame{};
  double lighting_ms_last_frame{};
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
    const arv::io::ChunkPipelineConfig& pipeline,
    arv::ChunkCoord center,
    arv::i32 min_chunk_y,
    arv::i32 max_chunk_y,
    arv::u64 world_seed,
    const arv::world::WorldGenerationConfig& generation_config,
    const arv::world::WorldGenerationPalette& generation_palette,
    const arv::voxel::BlockRegistry& block_registry,
    const arv::voxel::BlockRenderData& block_render_data,
    arv::core::Logger& logger) {
  const auto refresh_start = std::chrono::steady_clock::now();
  ++view.frames;
  view.generated_completed_last_frame = 0;
  view.mesh_completed_last_frame = 0;
  view.lighting_completed_last_frame = 0;
  view.generation_ms_last_frame = 0.0;
  view.mesh_ms_last_frame = 0.0;
  view.lighting_ms_last_frame = 0.0;

  for (arv::usize index = 0; index < view.pending_generations.size();) {
    auto& pending = view.pending_generations[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    auto timed = pending.job.get();
    view.generation_in_flight.erase(pending.coord);
    view.generated_completed_last_frame++;
    view.generation_ms_last_frame += timed.duration_ms;
    if (!timed.result) {
      logger.error("worldgen", timed.result.error().message);
      return false;
    }
    if (world.find_chunk(timed.result.value().coord) == nullptr &&
        !world.publish_generated_chunk(std::move(timed.result.value()))) {
      logger.error("worldgen", "failed to publish generated view chunk");
      return false;
    }

    view.pending_generations.erase(view.pending_generations.begin() + static_cast<std::ptrdiff_t>(index));
  }

  if (view.lighting_job && view.lighting_job->is_complete()) {
    auto timed = view.lighting_job->get();
    const arv::world::LightingPublishStats stats =
        world.publish_lighting_result(std::move(timed.result));
    view.lighting_completed_last_frame = stats.accepted_chunks;
    view.lighting_ms_last_frame = timed.duration_ms;
    if (stats.stale_chunks > 0U) {
      logger.warn("lighting", "discarded stale async lighting chunks");
    }
    view.lighting_job.reset();
  }

  for (arv::usize index = 0; index < view.pending_meshes.size();) {
    auto& pending = view.pending_meshes[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    auto timed = pending.job.get();
    view.mesh_in_flight.erase(pending.coord);
    view.mesh_completed_last_frame++;
    view.mesh_ms_last_frame += timed.duration_ms;
    if (world.validate_mesh_result(timed.mesh.coord, timed.mesh.source_version)) {
      arv::render::ChunkDrawItem draw_item;
      draw_item.key = {timed.mesh.world_id, timed.mesh.coord};
      draw_item.expected_version = timed.mesh.source_version;
      arv::render::ChunkMeshUpload upload;
      upload.mesh = std::move(timed.mesh);
      renderer.enqueue_chunk_mesh_upload(std::move(upload));
      (void)world.publish_mesh_result(draw_item.key.coord, draw_item.expected_version);
      view.draw_items[draw_item.key.coord] = draw_item;
    }

    view.pending_meshes.erase(view.pending_meshes.begin() + static_cast<std::ptrdiff_t>(index));
  }

  arv::u32 generation_jobs_scheduled = 0;
  for (arv::i32 z = center.value.z - distances.load_distance;
       z <= center.value.z + distances.load_distance;
       ++z) {
    for (arv::i32 x = center.value.x - distances.load_distance;
         x <= center.value.x + distances.load_distance;
         ++x) {
      for (arv::i32 y = min_chunk_y; y <= max_chunk_y; ++y) {
        const arv::ChunkCoord coord{{x, y, z}};
        if (generation_jobs_scheduled >= pipeline.max_generation_jobs_per_frame ||
            world.find_chunk(coord) != nullptr ||
            view.generation_in_flight.find(coord) != view.generation_in_flight.end()) {
          continue;
        }

        arv::world::ChunkGenerationRequest request;
        request.world_id = arv::server::DefaultWorldID;
        request.seed = world_seed;
        request.coord = coord;
        request.config = generation_config;
        request.palette = generation_palette;
        auto job = scheduler.submit("view_chunk_generation", [request] {
          const auto start = std::chrono::steady_clock::now();
          auto result = arv::world::generate_chunk(request);
          const auto end = std::chrono::steady_clock::now();
          return ChunkViewState::TimedGeneratedChunkResult{
              std::move(result),
              std::chrono::duration<double, std::milli>(end - start).count()};
        });
        view.generation_in_flight.insert(coord);
        view.pending_generations.push_back({coord, std::move(job)});
        ++generation_jobs_scheduled;
      }
    }
  }

  // TODO(arvane): Apply stream_distance and unload_margin once networking and
  // save-aware chunk unload candidates exist.
  world.update_simulation_radius(center, distances.simulation_distance);

  if (!view.lighting_job && world.has_lighting_work()) {
    const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();
    view.lighting_job = scheduler.submit("view_chunk_lighting", [snapshot, &block_registry] {
      const auto start = std::chrono::steady_clock::now();
      auto result = arv::world::WorldRuntime::build_lighting_result(snapshot, block_registry);
      const auto end = std::chrono::steady_clock::now();
      return ChunkViewState::TimedLightingResult{
          std::move(result),
          std::chrono::duration<double, std::milli>(end - start).count()};
    });
  }

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
        arv::voxel::ChunkSection* chunk = world.find_chunk(coord);
        if (chunk == nullptr) {
          continue;
        }
        if (view.pending_meshes.size() >= pipeline.max_mesh_jobs_per_frame ||
            chunk->is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight) ||
            view.mesh_in_flight.find(coord) != view.mesh_in_flight.end()) {
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

        const arv::u64 source_version = snapshot->source_version;
        auto job = scheduler.submit(
            "view_chunk_meshing",
            [snapshot = snapshot.value(), &block_registry, &block_render_data] {
          const auto start = std::chrono::steady_clock::now();
          auto mesh = arv::render::build_chunk_mesh(
              arv::server::DefaultWorldID,
              snapshot,
              block_registry,
              &block_render_data);
          const auto end = std::chrono::steady_clock::now();
          return ChunkViewState::TimedChunkMeshResult{
              std::move(mesh),
              std::chrono::duration<double, std::milli>(end - start).count()};
        });
        chunk->mark_mesh_queued();
        view.mesh_in_flight.insert(coord);
        view.pending_meshes.push_back({coord, source_version, std::move(job)});
      }
    }
  }

  const auto refresh_end = std::chrono::steady_clock::now();
  const double refresh_ms =
      std::chrono::duration<double, std::milli>(refresh_end - refresh_start).count();
  if (refresh_ms > static_cast<double>(pipeline.frame_spike_log_ms)) {
    std::ostringstream message;
    message << "chunk refresh spike " << refresh_ms << "ms"
            << " queued_generation=" << view.pending_generations.size()
            << " queued_mesh=" << view.pending_meshes.size()
            << " lighting_in_flight=" << (view.lighting_job ? 1 : 0);
    logger.warn("chunk_profile", message.str());
  }

  if (view.last_profile_log.time_since_epoch().count() == 0) {
    view.last_profile_log = refresh_end;
  }
  if (std::chrono::duration_cast<std::chrono::milliseconds>(refresh_end - view.last_profile_log)
          .count() >= static_cast<long long>(pipeline.profile_log_interval_ms)) {
    /*std::ostringstream message;
    message << "workers queued_generation=" << view.pending_generations.size()
            << " queued_mesh=" << view.pending_meshes.size()
            << " completed_generation=" << view.generated_completed_last_frame
            << " generation_ms=" << view.generation_ms_last_frame
            << " completed_lighting_chunks=" << view.lighting_completed_last_frame
            << " lighting_ms=" << view.lighting_ms_last_frame
            << " completed_mesh=" << view.mesh_completed_last_frame
            << " mesh_ms=" << view.mesh_ms_last_frame
            << " pending_uploads=" << renderer.pending_mesh_upload_count()
            << " uploads_last_frame=" << renderer.mesh_uploads_last_frame()
            << " upload_ms=" << renderer.mesh_upload_time_ms_last_frame();
    logger.info("chunk_profile", message.str());*/
    view.last_profile_log = refresh_end;
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
  auto chunk_vertex_shader = resources.load_text({"arvane:shader/world/chunk.vert"}, arv::asset::AssetKind::ShaderSource);
  if (!chunk_vertex_shader) {
    logger.error("assets", chunk_vertex_shader.error().message);
    return 1;
  }
  auto chunk_fragment_shader = resources.load_text({"arvane:shader/world/chunk.frag"}, arv::asset::AssetKind::ShaderSource);
  if (!chunk_fragment_shader) {
    logger.error("assets", chunk_fragment_shader.error().message);
    return 1;
  }
  renderer.set_chunk_shader_sources(
      std::move(chunk_vertex_shader.value()),
      std::move(chunk_fragment_shader.value()));
  auto ui_vertex_shader = resources.load_text({"arvane:shader/ui/ui.vert"}, arv::asset::AssetKind::ShaderSource);
  if (!ui_vertex_shader) {
    logger.error("assets", ui_vertex_shader.error().message);
    return 1;
  }
  auto ui_fragment_shader = resources.load_text({"arvane:shader/ui/ui.frag"}, arv::asset::AssetKind::ShaderSource);
  if (!ui_fragment_shader) {
    logger.error("assets", ui_fragment_shader.error().message);
    return 1;
  }
  renderer.set_ui_shader_sources(
      std::move(ui_vertex_shader.value()),
      std::move(ui_fragment_shader.value()));
  renderer.set_chunk_mesh_upload_budget(config.render.max_chunk_mesh_uploads_per_frame);

  auto framebuffer = window.framebuffer_size();
  renderer.resize(framebuffer.width, framebuffer.height);

  auto block_definition_text = arv::io::read_text_file(
      config.paths.assets_path() / "config/blocks.json",
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
  renderer.set_chunk_texture_atlas(texture_upload_from_image(block_atlas.atlas.pages.front().image));

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
    const std::filesystem::path path = resources.resolve_asset_path(fallback, arv::asset::AssetKind::Font);
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

  auto item_registry_result = arv::player::ItemRegistry::build_default_block_items(block_registry);
  if (!item_registry_result) {
    logger.error("items", item_registry_result.error().message);
    return 1;
  }
  const arv::player::ItemRegistry item_registry = std::move(item_registry_result.value());
  const arv::u64 item_registry_hash = arv::persistence::hash_item_registry(item_registry);

  const std::filesystem::path save_root = default_save_root(config);
  auto loaded_save_result =
      arv::persistence::load_server_root(save_root, block_registry, item_registry);
  if (!loaded_save_result) {
    logger.error("save", loaded_save_result.error().message);
    return 1;
  }
  const arv::persistence::LoadedSaveData loaded_save = std::move(loaded_save_result.value());

  auto worldgen_text = arv::io::read_text_file(
      config.paths.assets_path() / "config/worldgen/overworld.yaml",
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
      visible_worldgen_chunk_y_range(generation_config);
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

  const arv::world::WorldGenerationPalette generation_palette = std::move(worldgen_palette.value());
  const auto [min_chunk_y, max_chunk_y] = visible_chunk_y;

  arv::server::ServerRuntime integrated_server(std::move(transport_pair.server));
  auto server_start = integrated_server.start({
      arv::server::ServerMode::Integrated,
      config.simulation.base_tps,
      {"in-process", 0},
      &block_registry,
      &item_registry,
      arv::player::default_player_tuning(),
      arv::voxel::make_water_fluid_definition()});
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
  auto connect_result = client.connect_integrated();
  if (!connect_result) {
    logger.error("client", connect_result.error().message);
    return 1;
  }
  integrated_server.tick();
  client.fixed_tick(integrated_server.tick_index());
  apply_loaded_player_saves(integrated_server, world_save_name, loaded_save.players);
  if (!loaded_save.players.empty()) {
    integrated_server.tick();
    client.fixed_tick(integrated_server.tick_index());
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
  arv::world::WorldTickPolicy tick_policy =
      integrated_server.voxel_world().tick_policy();
  tick_policy.process_lighting_synchronously = false;
  integrated_server.voxel_world().set_tick_policy(tick_policy);
  ChunkViewState chunk_view;

  if (!refresh_chunk_view(
          integrated_server.voxel_world(),
          renderer,
          world_jobs,
          chunk_view,
          config.chunks,
          config.chunk_pipeline,
          camera_chunk_coord(client.camera()),
          min_chunk_y,
          max_chunk_y,
          world_seed,
          generation_config,
          generation_palette,
          block_registry,
          block_atlas.render_data,
          logger)) {
    return 1;
  }

  arv::core::FixedTimestepAccumulator ticks(config.simulation.base_tps);
  auto previous_time = std::chrono::steady_clock::now();

  while (!window.should_close()) {
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<arv::Seconds> delta = now - previous_time;
    previous_time = now;
    const double frame_delta_ms =
        std::chrono::duration<double, std::milli>(delta).count();
    if (frame_delta_ms > static_cast<double>(config.chunk_pipeline.frame_spike_log_ms)) {
      std::ostringstream message;
      message << "frame spike " << frame_delta_ms << "ms"
              << " queued_generation=" << chunk_view.pending_generations.size()
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
    client.submit_input(to_client_input(platform_input));
    if (!refresh_chunk_view(
            integrated_server.voxel_world(),
            renderer,
            world_jobs,
            chunk_view,
            config.chunks,
            config.chunk_pipeline,
            camera_chunk_coord(client.camera()),
            min_chunk_y,
            max_chunk_y,
            world_seed,
            generation_config,
            generation_palette,
            block_registry,
            block_atlas.render_data,
            logger)) {
      return 1;
    }

    ticks.add_time(delta.count());
    while (ticks.consume_tick()) {
      integrated_server.tick();
      client.fixed_tick(integrated_server.tick_index());
    }

    framebuffer = window.framebuffer_size();
    renderer.resize(framebuffer.width, framebuffer.height);
    arv::render::FramePacket frame = client.build_frame_packet(
        ticks.interpolation_alpha(),
        framebuffer.width,
        framebuffer.height);
    frame.opaque_chunks = collect_draw_items(chunk_view);
    std::vector<arv::client::UiItemStackView> inventory_slots;
    const std::vector<arv::SessionID> ui_session_ids = integrated_server.sessions().session_ids();
    if (const arv::server::ServerSession* session =
            ui_session_ids.empty() ? nullptr : integrated_server.sessions().find(ui_session_ids.front());
        session != nullptr) {
      if (const arv::player::PlayerState* player_state =
              integrated_server.voxel_world().find_player(session->player_id);
          player_state != nullptr) {
        inventory_slots = inventory_slots_for_ui(player_state->inventory, item_registry);
      }
    }
    const std::vector<arv::client::PlayerListEntryView> tab_players =
        player_list_for_ui(integrated_server);
    arv::client::build_gameplay_ui(
        ui_tree,
        {
            {
                client.selected_hotbar_slot(),
                arv::player::DefaultHotbarSlotCount,
                {static_cast<arv::f32>(framebuffer.width), static_cast<arv::f32>(framebuffer.height)},
                std::span<const arv::client::UiItemStackView>{
                    inventory_slots.data(),
                    std::min<arv::usize>(inventory_slots.size(), arv::player::DefaultHotbarSlotCount)},
            },
            {
                client.inventory_open(),
                "Inventory",
                client.selected_hotbar_slot(),
                arv::player::DefaultHotbarSlotCount,
                {static_cast<arv::f32>(framebuffer.width), static_cast<arv::f32>(framebuffer.height)},
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
        });
    ui_tree.layout(
        {static_cast<arv::f32>(framebuffer.width), static_cast<arv::f32>(framebuffer.height)},
        &ui_text_measurer);
    arv::client::append_ui_draw_commands(ui_tree, font_system, ui_font_stack, frame);
    if (font_system.atlas_dirty()) {
      renderer.set_ui_font_atlas(texture_upload_from_image(font_system.atlas_image()));
      font_system.clear_atlas_dirty();
    }
    renderer.submit_frame(std::move(frame));
    renderer.render();
    window.swap_buffers();
  }

  const std::vector<arv::persistence::SavedChunkSection> saved_chunks =
      capture_saved_chunks(integrated_server.voxel_world());
  const std::vector<arv::persistence::PlayerSaveData> player_saves =
      capture_player_saves(integrated_server, world_save_name);
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
