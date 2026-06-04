#include "config_internal.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::io::detail {

bool valid_config_pack_id(std::string_view id) noexcept {
  if (id.empty()) {
    return false;
  }
  for (usize index = 0; index < id.size(); ++index) {
    const char c = id[index];
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    const bool valid = index == 0U
                           ? (lower || digit || c == '_')
                           : (lower || digit || c == '_' || c == '-' || c == '.');
    if (!valid) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_supported_skin_resolution(u16 size) noexcept {
  return size == 64U || size == 128U || size == 256U || size == 512U ||
         size == 1024U || size == 2048U;
}

std::optional<ConfigError> validate_config(const EngineConfig& config) {
  if (config.server.world.empty() || config.server.world == "." ||
      config.server.world == ".." ||
      config.server.world.find('/') != std::string::npos ||
      config.server.world.find('\\') != std::string::npos) {
    return ConfigError{"world must be a non-empty world name, not a path"};
  }

  if (config.simulation.base_tps != 20 && config.simulation.base_tps != 30 &&
      config.simulation.base_tps != 60) {
    return ConfigError{"simulation.base_tps must be one of 20, 30, or 60"};
  }
  auto validate_interval = [](u32 interval, std::string_view name) -> std::optional<ConfigError> {
    if (interval == 0U) {
      return ConfigError{std::string{name} + ".interval must be non-zero"};
    }
    return std::nullopt;
  };
  for (const auto [name, interval] : {
           std::pair{std::string_view{"simulation.systems.physics"}, config.simulation.systems.physics.interval},
           std::pair{std::string_view{"simulation.systems.entities"}, config.simulation.systems.entities.interval},
           std::pair{std::string_view{"simulation.systems.ai"}, config.simulation.systems.ai.interval},
           std::pair{std::string_view{"simulation.systems.block_updates"}, config.simulation.systems.block_updates.interval},
           std::pair{std::string_view{"simulation.systems.fluids"}, config.simulation.systems.fluids.interval},
           std::pair{std::string_view{"simulation.systems.random_ticks"}, config.simulation.systems.random_ticks.interval},
       }) {
    if (std::optional<ConfigError> error = validate_interval(interval, name);
        error.has_value()) {
      return error;
    }
  }
  if (config.simulation.systems.block_updates.budget == 0U ||
      config.simulation.systems.fluids.budget == 0U) {
    return ConfigError{"simulation system budgets for block_updates and fluids must be non-zero"};
  }
  if (config.simulation.systems.random_ticks.samples_per_chunk == 0U) {
    return ConfigError{"simulation.systems.random_ticks.samples_per_chunk must be non-zero"};
  }
  if (config.simulation.fluids.water.flow_interval_ticks == 0U ||
      config.simulation.fluids.lava.flow_interval_ticks == 0U) {
    return ConfigError{"simulation fluid flow_interval_ticks values must be non-zero"};
  }

  if (config.window.width == 0 || config.window.height == 0) {
    return ConfigError{"window dimensions must be non-zero"};
  }

  auto validate_positive_f32 = [](f32 value, std::string_view name) -> std::optional<ConfigError> {
    if (!std::isfinite(value) || value <= 0.0F) {
      return ConfigError{std::string{name} + " must be finite and > 0"};
    }
    return std::nullopt;
  };
  auto validate_non_negative_f32 = [](f32 value, std::string_view name) -> std::optional<ConfigError> {
    if (!std::isfinite(value) || value < 0.0F) {
      return ConfigError{std::string{name} + " must be finite and >= 0"};
    }
    return std::nullopt;
  };
  if (config.network.movement.mode != "client_authoritative_guarded") {
    return ConfigError{"network.movement.mode must be client_authoritative_guarded"};
  }
  if (config.network.keep_alive.interval_seconds == 0U ||
      config.network.keep_alive.client_timeout_seconds == 0U) {
    return ConfigError{"network.keep_alive values must be non-zero"};
  }
  if (config.network.keep_alive.client_timeout_seconds <=
      config.network.keep_alive.interval_seconds) {
    return ConfigError{"network.keep_alive.client_timeout_seconds must be greater than interval_seconds"};
  }
  for (const std::string& pack_id : config.native_plugins.allowed_packs) {
    if (!valid_config_pack_id(pack_id)) {
      return ConfigError{"native_plugins.allowed_packs contains an invalid pack id"};
    }
  }
  if (config.multiplayer.keep_alive.server_timeout_seconds == 0U ||
      config.multiplayer.connection_timeout_seconds == 0U) {
    return ConfigError{"multiplayer timeout values must be non-zero"};
  }
  for (const auto [name, value] : {
           std::pair{std::string_view{"network.movement.send_rate_hz"}, config.network.movement.send_rate_hz},
           std::pair{std::string_view{"network.movement.max_ground_speed_blocks_per_second"}, config.network.movement.max_ground_speed_blocks_per_second},
           std::pair{std::string_view{"network.movement.max_air_speed_blocks_per_second"}, config.network.movement.max_air_speed_blocks_per_second},
           std::pair{std::string_view{"network.movement.max_swim_speed_blocks_per_second"}, config.network.movement.max_swim_speed_blocks_per_second},
           std::pair{std::string_view{"network.movement.max_fly_speed_blocks_per_second"}, config.network.movement.max_fly_speed_blocks_per_second},
           std::pair{std::string_view{"network.movement.max_vertical_speed_blocks_per_second"}, config.network.movement.max_vertical_speed_blocks_per_second},
           std::pair{std::string_view{"network.movement.max_single_packet_distance_blocks"}, config.network.movement.max_single_packet_distance_blocks},
           std::pair{std::string_view{"network.movement.max_validation_window_seconds"}, config.network.movement.max_validation_window_seconds},
       }) {
    if (std::optional<ConfigError> error = validate_positive_f32(value, name);
        error.has_value()) {
      return error;
    }
  }
  for (const auto [name, value] : {
           std::pair{std::string_view{"network.movement.base_position_epsilon_blocks"}, config.network.movement.base_position_epsilon_blocks},
           std::pair{std::string_view{"network.movement.lag_grace_blocks"}, config.network.movement.lag_grace_blocks},
       }) {
    if (std::optional<ConfigError> error = validate_non_negative_f32(value, name);
        error.has_value()) {
      return error;
    }
  }

  if (config.skins.uploads.max_chunk_bytes == 0U ||
      config.skins.uploads.chunk_threshold_bytes == 0U ||
      config.skins.uploads.max_active_per_connection == 0U ||
      config.skins.uploads.max_active_global == 0U ||
      config.skins.uploads.timeout_seconds == 0U ||
      config.skins.uploads.idle_timeout_seconds == 0U ||
      config.skins.uploads.max_begin_attempts_per_minute == 0U ||
      config.skins.uploads.max_completed_uploads_per_minute == 0U ||
      config.skins.uploads.max_completed_uploads_per_hour == 0U ||
      config.skins.uploads.max_upload_bytes_per_hour == 0U ||
      config.skins.uploads.max_failed_decodes_per_minute == 0U) {
    return ConfigError{"skins.uploads limits must be non-zero"};
  }
  if (config.skins.uploads.idle_timeout_seconds > config.skins.uploads.timeout_seconds) {
    return ConfigError{"skins.uploads.idle_timeout_seconds must be <= timeout_seconds"};
  }
  if (config.skins.downloads.max_requests_per_second == 0U ||
      config.skins.downloads.max_requests_per_minute == 0U ||
      config.skins.downloads.max_bytes_per_minute == 0U ||
      config.skins.downloads.max_in_flight_per_connection == 0U ||
      config.skins.downloads.max_batch_size == 0U) {
    return ConfigError{"skins.downloads limits must be non-zero"};
  }
  if (config.skins.storage.max_total_skin_bytes == 0U ||
      config.skins.storage.max_skins_per_profile == 0U) {
    return ConfigError{"skins.storage limits must be non-zero"};
  }
  if (config.skins.resolutions.empty()) {
    return ConfigError{"skins.resolutions must list at least one resolution"};
  }
  std::vector<u16> seen_skin_sizes;
  seen_skin_sizes.reserve(config.skins.resolutions.size());
  for (const SkinResolutionConfig& resolution : config.skins.resolutions) {
    if (!is_supported_skin_resolution(resolution.size)) {
      return ConfigError{"skins.resolutions.size must be one of 64, 128, 256, 512, 1024, or 2048"};
    }
    if (std::find(seen_skin_sizes.begin(), seen_skin_sizes.end(), resolution.size) !=
        seen_skin_sizes.end()) {
      return ConfigError{"skins.resolutions may not contain duplicate sizes"};
    }
    seen_skin_sizes.push_back(resolution.size);
    if (resolution.max_upload_bytes == 0U) {
      return ConfigError{"skins.resolutions.max_upload_bytes must be non-zero"};
    }
    if (resolution.max_decoded_bytes == 0U) {
      return ConfigError{"skins.resolutions.max_decoded_bytes must be non-zero"};
    }
  }

  if (config.chunks.load_distance < 0 || config.chunks.simulation_distance < 0 ||
      config.chunks.render_distance < 0 || config.chunks.stream_distance < 0) {
    return ConfigError{"chunk distances must be non-negative"};
  }
  if (config.chunks.unload_margin < 0) {
    return ConfigError{"chunks.unload_margin must be non-negative"};
  }
  if (config.chunks.simulation_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.simulation_distance must be <= chunks.load_distance"};
  }
  if (config.chunks.render_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.render_distance must be <= chunks.load_distance"};
  }
  if (config.chunks.stream_distance > config.chunks.load_distance) {
    return ConfigError{"chunks.stream_distance must be <= chunks.load_distance"};
  }
  if (config.natural_spawning.interval_ticks == 0U) {
    return ConfigError{"natural_spawning.interval_ticks must be non-zero"};
  }
  if (config.natural_spawning.min_distance_from_player_blocks >
      config.natural_spawning.max_distance_from_player_blocks) {
    return ConfigError{
        "natural_spawning.min_distance_from_player_blocks must be <= max_distance_from_player_blocks"};
  }
  if (config.natural_spawning.max_successes_per_player_per_tick == 0U ||
      config.natural_spawning.max_successes_per_world_per_tick == 0U) {
    return ConfigError{"natural_spawning success budgets must be non-zero"};
  }
  if (config.natural_spawning.local_density_radius_blocks == 0U) {
    return ConfigError{"natural_spawning.local_density_radius_blocks must be non-zero"};
  }
  if (config.jobs.min_worker_threads == 0U) {
    return ConfigError{"jobs.min_worker_threads must be non-zero"};
  }
  if (config.jobs.max_worker_threads < config.jobs.min_worker_threads) {
    return ConfigError{"jobs.max_worker_threads must be >= jobs.min_worker_threads"};
  }
  for (const auto [name, value] : {
           std::pair{std::string_view{"physics.standing_eye_height"}, config.physics.standing_eye_height},
           std::pair{std::string_view{"physics.sneaking_eye_height"}, config.physics.sneaking_eye_height},
           std::pair{std::string_view{"physics.swimming_eye_height"}, config.physics.swimming_eye_height},
           std::pair{std::string_view{"physics.eye_height_lerp_per_second"}, config.physics.eye_height_lerp_per_second},
           std::pair{std::string_view{"physics.reach_distance"}, config.physics.reach_distance},
           std::pair{std::string_view{"physics.mouse_sensitivity_radians"}, config.physics.mouse_sensitivity_radians},
           std::pair{std::string_view{"physics.walk_speed"}, config.physics.walk_speed},
           std::pair{std::string_view{"physics.sprint_speed"}, config.physics.sprint_speed},
           std::pair{std::string_view{"physics.sneak_speed"}, config.physics.sneak_speed},
           std::pair{std::string_view{"physics.air_speed"}, config.physics.air_speed},
           std::pair{std::string_view{"physics.air_control_acceleration_per_second"}, config.physics.air_control_acceleration_per_second},
           std::pair{std::string_view{"physics.default_surface_slipperiness"}, config.physics.default_surface_slipperiness},
           std::pair{std::string_view{"physics.ground_acceleration_per_second"}, config.physics.ground_acceleration_per_second},
           std::pair{std::string_view{"physics.ground_deceleration_per_second"}, config.physics.ground_deceleration_per_second},
           std::pair{std::string_view{"physics.min_surface_control"}, config.physics.min_surface_control},
           std::pair{std::string_view{"physics.max_surface_control"}, config.physics.max_surface_control},
           std::pair{std::string_view{"physics.fly_speed"}, config.physics.fly_speed},
           std::pair{std::string_view{"physics.fly_vertical_speed"}, config.physics.fly_vertical_speed},
           std::pair{std::string_view{"physics.jump_velocity"}, config.physics.jump_velocity},
           std::pair{std::string_view{"physics.gravity_per_second"}, config.physics.gravity_per_second},
           std::pair{std::string_view{"physics.terminal_velocity"}, config.physics.terminal_velocity},
           std::pair{std::string_view{"physics.body_width"}, config.physics.body_width},
           std::pair{std::string_view{"physics.body_height"}, config.physics.body_height},
           std::pair{std::string_view{"physics.sneaking_body_height"}, config.physics.sneaking_body_height},
           std::pair{std::string_view{"physics.swimming_body_width"}, config.physics.swimming_body_width},
           std::pair{std::string_view{"physics.swimming_body_height"}, config.physics.swimming_body_height},
           std::pair{std::string_view{"physics.step_height"}, config.physics.step_height},
           std::pair{std::string_view{"physics.sneak_support_probe_depth"}, config.physics.sneak_support_probe_depth},
           std::pair{std::string_view{"physics.collision_epsilon"}, config.physics.collision_epsilon},
           std::pair{std::string_view{"physics.surface_swim_speed"}, config.physics.surface_swim_speed},
           std::pair{std::string_view{"physics.underwater_swim_speed"}, config.physics.underwater_swim_speed},
           std::pair{std::string_view{"physics.fast_surface_swim_speed"}, config.physics.fast_surface_swim_speed},
           std::pair{std::string_view{"physics.fast_underwater_swim_speed"}, config.physics.fast_underwater_swim_speed},
           std::pair{std::string_view{"physics.upward_swim_acceleration"}, config.physics.upward_swim_acceleration},
           std::pair{std::string_view{"physics.downward_swim_acceleration"}, config.physics.downward_swim_acceleration},
           std::pair{std::string_view{"physics.surface_float_depth"}, config.physics.surface_float_depth},
           std::pair{std::string_view{"physics.surface_buoyancy"}, config.physics.surface_buoyancy},
           std::pair{std::string_view{"physics.breath_seconds"}, config.physics.breath_seconds},
           std::pair{std::string_view{"physics.max_head_body_yaw_radians"}, config.physics.max_head_body_yaw_radians},
       }) {
    if (std::optional<ConfigError> error = validate_positive_f32(value, name);
        error.has_value()) {
      return error;
    }
  }
  if (!std::isfinite(config.physics.underwater_buoyancy)) {
    return ConfigError{"physics.underwater_buoyancy must be finite"};
  }
  if (!std::isfinite(config.physics.sprint_jump_horizontal_boost) ||
      config.physics.sprint_jump_horizontal_boost < 0.0F) {
    return ConfigError{"physics.sprint_jump_horizontal_boost must be finite and >= 0"};
  }
  if (!std::isfinite(config.physics.liquid_drag) ||
      config.physics.liquid_drag < 0.0F ||
      config.physics.liquid_drag > 1.0F) {
    return ConfigError{"physics.liquid_drag must be finite and between 0 and 1"};
  }
  if (config.physics.swimming_body_height > config.physics.body_height ||
      config.physics.sneaking_body_height > config.physics.body_height) {
    return ConfigError{"physics swimming/sneaking body heights must be <= body_height"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.base_seconds_per_hardness) ||
      config.gameplay.block_breaking.base_seconds_per_hardness < 0.0F) {
    return ConfigError{"gameplay.block_breaking.base_seconds_per_hardness must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.creative_inventory_multiplier) ||
      config.gameplay.block_breaking.creative_inventory_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.creative_inventory_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.hand_speed_multiplier) ||
      config.gameplay.block_breaking.hand_speed_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.hand_speed_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.block_breaking.wrong_tool_speed_multiplier) ||
      config.gameplay.block_breaking.wrong_tool_speed_multiplier < 0.0F) {
    return ConfigError{"gameplay.block_breaking.wrong_tool_speed_multiplier must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.dropped_items.pickup_radius) ||
      config.gameplay.dropped_items.pickup_radius < 0.0F) {
    return ConfigError{"gameplay.dropped_items.pickup_radius must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.dropped_items.pickup_delay_seconds) ||
      config.gameplay.dropped_items.pickup_delay_seconds < 0.0F) {
    return ConfigError{"gameplay.dropped_items.pickup_delay_seconds must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.default_attack_damage) ||
      config.gameplay.combat.default_attack_damage <= 0.0F) {
    return ConfigError{"gameplay.combat.default_attack_damage must be finite and > 0"};
  }
  if (!std::isfinite(config.gameplay.combat.default_attack_range) ||
      config.gameplay.combat.default_attack_range <= 0.0F) {
    return ConfigError{"gameplay.combat.default_attack_range must be finite and > 0"};
  }
  if (!std::isfinite(config.gameplay.combat.default_attack_cooldown_seconds) ||
      config.gameplay.combat.default_attack_cooldown_seconds < 0.0F) {
    return ConfigError{"gameplay.combat.default_attack_cooldown_seconds must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.critical_chance) ||
      config.gameplay.combat.critical_chance < 0.0F ||
      config.gameplay.combat.critical_chance > 100.0F) {
    return ConfigError{"gameplay.combat.critical-chance must be finite and in [0, 100]"};
  }
  if (!std::isfinite(config.gameplay.combat.critical_multiplier) ||
      config.gameplay.combat.critical_multiplier <= 0.0F) {
    return ConfigError{"gameplay.combat.critical-multiplier must be finite and > 0"};
  }
  if (!std::isfinite(config.gameplay.combat.attack_validation_grace_blocks) ||
      config.gameplay.combat.attack_validation_grace_blocks < 0.0F) {
    return ConfigError{"gameplay.combat.attack_validation_grace_blocks must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.max_attack_validation_range) ||
      config.gameplay.combat.max_attack_validation_range <= 0.0F) {
    return ConfigError{"gameplay.combat.max_attack_validation_range must be finite and > 0"};
  }
  if (!std::isfinite(config.gameplay.combat.existing_horizontal_retention) ||
      config.gameplay.combat.existing_horizontal_retention < 0.0F ||
      config.gameplay.combat.existing_horizontal_retention > 1.0F) {
    return ConfigError{"gameplay.combat.existing_horizontal_retention must be finite and in [0, 1]"};
  }
  if (!std::isfinite(config.gameplay.combat.base_horizontal_knockback) ||
      config.gameplay.combat.base_horizontal_knockback < 0.0F) {
    return ConfigError{"gameplay.combat.base_horizontal_knockback must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.sprint_horizontal_bonus) ||
      config.gameplay.combat.sprint_horizontal_bonus < 0.0F) {
    return ConfigError{"gameplay.combat.sprint_horizontal_bonus must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.base_vertical_knockback) ||
      config.gameplay.combat.base_vertical_knockback < 0.0F) {
    return ConfigError{"gameplay.combat.base_vertical_knockback must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.airborne_vertical_knockback) ||
      config.gameplay.combat.airborne_vertical_knockback < 0.0F) {
    return ConfigError{"gameplay.combat.airborne_vertical_knockback must be finite and >= 0"};
  }
  if (!std::isfinite(config.gameplay.combat.vertical_knockback_cap) ||
      config.gameplay.combat.vertical_knockback_cap < 0.0F) {
    return ConfigError{"gameplay.combat.vertical_knockback_cap must be finite and >= 0"};
  }
  if (config.render.max_chunk_mesh_uploads_per_frame == 0U) {
    return ConfigError{"render.max_chunk_mesh_uploads_per_frame must be non-zero"};
  }
  if (config.render.frame_pacing_enabled &&
      config.render.frame_pacing_target_fps == 0U) {
    return ConfigError{"render.frame_pacing_target_fps must be non-zero when frame pacing is enabled"};
  }
  if (!std::isfinite(config.render.block_outline.color_r) ||
      !std::isfinite(config.render.block_outline.color_g) ||
      !std::isfinite(config.render.block_outline.color_b) ||
      config.render.block_outline.color_r < 0.0F ||
      config.render.block_outline.color_g < 0.0F ||
      config.render.block_outline.color_b < 0.0F ||
      config.render.block_outline.color_r > 1.0F ||
      config.render.block_outline.color_g > 1.0F ||
      config.render.block_outline.color_b > 1.0F) {
    return ConfigError{"render.block_outline.color must contain finite values between 0 and 1"};
  }
  if (!std::isfinite(config.render.block_outline.thickness) ||
      config.render.block_outline.thickness <= 0.0F) {
    return ConfigError{"render.block_outline.thickness must be finite and > 0"};
  }
  if (!std::isfinite(config.render.sky.star_intensity) ||
      config.render.sky.star_intensity < 0.0F ||
      !std::isfinite(config.render.sky.cloud_intensity) ||
      config.render.sky.cloud_intensity < 0.0F) {
    return ConfigError{"render.sky intensities must be finite and >= 0"};
  }
  if (!std::isfinite(config.render.sky.cirrus_amount) ||
      !std::isfinite(config.render.sky.cumulus_amount) ||
      config.render.sky.cirrus_amount < 0.0F ||
      config.render.sky.cirrus_amount > 1.0F ||
      config.render.sky.cumulus_amount < 0.0F ||
      config.render.sky.cumulus_amount > 1.0F) {
    return ConfigError{"render.sky cloud amounts must be finite values between 0 and 1"};
  }
  if (config.render.atlases.player_skins.page_size == 0U ||
      config.render.atlases.player_skins.max_pages == 0U) {
    return ConfigError{"render.atlases.player_skins page_size and max_pages must be non-zero"};
  }
  if (config.render.atlases.player_skins.cleanup.interval_seconds == 0U ||
      config.render.atlases.player_skins.cleanup.evict_after_unused_seconds == 0U) {
    return ConfigError{"render.atlases.player_skins cleanup intervals must be non-zero"};
  }
  if (!std::isfinite(config.render.atlases.player_skins.pressure_cleanup.usage_threshold) ||
      !std::isfinite(config.render.atlases.player_skins.pressure_cleanup.target_usage) ||
      config.render.atlases.player_skins.pressure_cleanup.usage_threshold <= 0.0F ||
      config.render.atlases.player_skins.pressure_cleanup.usage_threshold > 1.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage <= 0.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage > 1.0F ||
      config.render.atlases.player_skins.pressure_cleanup.target_usage >
          config.render.atlases.player_skins.pressure_cleanup.usage_threshold) {
    return ConfigError{
        "render.atlases.player_skins pressure thresholds must be finite in (0, 1] and target <= usage"};
  }
  if (config.ui.chat.max_history_messages == 0U) {
    return ConfigError{"ui.chat.max_history_messages must be non-zero"};
  }
  if (config.ui.chat.open_max_lines == 0U) {
    return ConfigError{"ui.chat.open_max_lines must be non-zero"};
  }
  if (!std::isfinite(config.ui.chat.passive_visible_seconds) ||
      config.ui.chat.passive_visible_seconds < 0.0F) {
    return ConfigError{"ui.chat.passive_visible_seconds must be finite and >= 0"};
  }
  if (!std::isfinite(config.ui.chat.passive_fade_seconds) ||
      config.ui.chat.passive_fade_seconds < 0.0F) {
    return ConfigError{"ui.chat.passive_fade_seconds must be finite and >= 0"};
  }
  if (config.chunk_pipeline.max_generation_jobs_per_frame == 0U ||
      config.chunk_pipeline.max_generation_results_per_tick == 0U ||
      config.chunk_pipeline.max_lighting_results_per_tick == 0U ||
      config.chunk_pipeline.max_lighting_sections_per_job == 0U ||
      config.chunk_pipeline.max_client_chunk_applies_per_frame == 0U ||
      config.chunk_pipeline.max_client_chunk_unloads_per_frame == 0U ||
      config.chunk_pipeline.max_mesh_jobs_per_frame == 0U ||
      config.chunk_pipeline.max_mesh_sections_per_job == 0U ||
      config.chunk_pipeline.max_mesh_sections_prepared_per_frame == 0U) {
    return ConfigError{"chunk_pipeline job budgets must be non-zero"};
  }

  return std::nullopt;
}

} // namespace arv::io::detail
