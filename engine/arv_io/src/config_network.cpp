#include "config_internal.hpp"

namespace arv::io::detail {

void read_movement_validation_config(
    const DataValue& movement,
    MovementValidationConfig& config) {
  read_scalar(movement, "enabled", config.enabled);
  read_scalar(movement, "mode", config.mode);
  read_scalar(movement, "send_rate_hz", config.send_rate_hz);
  read_scalar(
      movement,
      "max_ground_speed_blocks_per_second",
      config.max_ground_speed_blocks_per_second);
  read_scalar(
      movement,
      "max_air_speed_blocks_per_second",
      config.max_air_speed_blocks_per_second);
  read_scalar(
      movement,
      "max_swim_speed_blocks_per_second",
      config.max_swim_speed_blocks_per_second);
  read_scalar(
      movement,
      "max_fly_speed_blocks_per_second",
      config.max_fly_speed_blocks_per_second);
  read_scalar(
      movement,
      "max_vertical_speed_blocks_per_second",
      config.max_vertical_speed_blocks_per_second);
  read_scalar(
      movement,
      "base_position_epsilon_blocks",
      config.base_position_epsilon_blocks);
  read_scalar(movement, "lag_grace_blocks", config.lag_grace_blocks);
  read_scalar(
      movement,
      "max_single_packet_distance_blocks",
      config.max_single_packet_distance_blocks);
  read_scalar(
      movement,
      "max_validation_window_seconds",
      config.max_validation_window_seconds);
  read_scalar(
      movement,
      "reject_unloaded_chunk_movement",
      config.reject_unloaded_chunk_movement);
  read_scalar(movement, "solid_collision_check", config.solid_collision_check);
  read_scalar(movement, "correction_cooldown_ms", config.correction_cooldown_ms);
}

void read_keep_alive_config(
    const DataValue& keep_alive,
    KeepAliveConfig& config) {
  read_scalar(keep_alive, "interval_seconds", config.interval_seconds);
  read_scalar(keep_alive, "client_timeout_seconds", config.client_timeout_seconds);
}

void read_web_network_config(
    const DataValue& web,
    WebNetworkConfig& config) {
  read_scalar(web, "enabled", config.enabled);
  read_scalar(web, "bind_address", config.bind_address);
  read_scalar(web, "port", config.port);
  read_scalar(web, "path", config.path);
  read_scalar(web, "max_frame_bytes", config.max_frame_bytes);
  read_scalar(web, "max_pending_bytes", config.max_pending_bytes);
}

void read_multiplayer_config(
    const DataValue& multiplayer,
    MultiplayerConfig& config) {
  const DataValue* keep_alive = multiplayer.get("keep_alive");
  if (keep_alive != nullptr) {
    read_scalar(
        *keep_alive,
        "server_timeout_seconds",
        config.keep_alive.server_timeout_seconds);
  }
  read_scalar(
      multiplayer,
      "connection_timeout_seconds",
      config.connection_timeout_seconds);
}

void read_skins_config(const DataValue& skins, SkinConfig& config) {
  read_scalar(skins, "enabled", config.enabled);
  read_scalar(skins, "allow_uploads", config.allow_uploads);

  const DataValue* resolutions = skins.get("resolutions");
  if (resolutions != nullptr) {
    config.resolutions.clear();
    if (const DataValue::Array* array = resolutions->array_if(); array != nullptr) {
      config.resolutions.reserve(array->size());
      for (const DataValue& node : *array) {
        if (node.kind() != DataKind::Object) {
          continue;
        }
        SkinResolutionConfig resolution;
        read_scalar(node, "size", resolution.size);
        resolution.enabled = true;
        read_scalar(node, "enabled", resolution.enabled);
        read_scalar(node, "permission", resolution.permission);
        resolution.max_upload_bytes = default_skin_max_upload_bytes(resolution.size);
        resolution.max_decoded_bytes = default_skin_max_decoded_bytes(resolution.size);
        read_scalar(node, "max_upload_bytes", resolution.max_upload_bytes);
        read_scalar(node, "max_decoded_bytes", resolution.max_decoded_bytes);
        config.resolutions.push_back(std::move(resolution));
      }
    }
  }

  const DataValue* uploads = skins.get("uploads");
  if (uploads != nullptr) {
    read_scalar(*uploads, "max_chunk_bytes", config.uploads.max_chunk_bytes);
    read_scalar(*uploads, "chunk_threshold_bytes", config.uploads.chunk_threshold_bytes);
    read_scalar(*uploads, "max_active_per_connection", config.uploads.max_active_per_connection);
    read_scalar(*uploads, "max_active_global", config.uploads.max_active_global);
    read_scalar(*uploads, "timeout_seconds", config.uploads.timeout_seconds);
    read_scalar(*uploads, "idle_timeout_seconds", config.uploads.idle_timeout_seconds);
    read_scalar(
        *uploads,
        "max_begin_attempts_per_minute",
        config.uploads.max_begin_attempts_per_minute);
    read_scalar(
        *uploads,
        "max_completed_uploads_per_minute",
        config.uploads.max_completed_uploads_per_minute);
    read_scalar(
        *uploads,
        "max_completed_uploads_per_hour",
        config.uploads.max_completed_uploads_per_hour);
    read_scalar(
        *uploads,
        "max_upload_bytes_per_hour",
        config.uploads.max_upload_bytes_per_hour);
    read_scalar(
        *uploads,
        "max_failed_decodes_per_minute",
        config.uploads.max_failed_decodes_per_minute);
  }

  const DataValue* downloads = skins.get("downloads");
  if (downloads != nullptr) {
    read_scalar(
        *downloads,
        "max_requests_per_second",
        config.downloads.max_requests_per_second);
    read_scalar(
        *downloads,
        "max_requests_per_minute",
        config.downloads.max_requests_per_minute);
    read_scalar(
        *downloads,
        "max_bytes_per_minute",
        config.downloads.max_bytes_per_minute);
    read_scalar(
        *downloads,
        "max_in_flight_per_connection",
        config.downloads.max_in_flight_per_connection);
    read_scalar(
        *downloads,
        "require_advertised_hash",
        config.downloads.require_advertised_hash);
    read_scalar(*downloads, "max_batch_size", config.downloads.max_batch_size);
  }

  const DataValue* storage = skins.get("storage");
  if (storage != nullptr) {
    read_scalar(*storage, "max_total_skin_bytes", config.storage.max_total_skin_bytes);
    read_scalar(*storage, "max_skins_per_profile", config.storage.max_skins_per_profile);
  }
}

void put_network_movement_config(
    DataValue& root,
    std::string_view prefix,
    const MovementValidationConfig& movement) {
  put_under(root, prefix, "enabled", movement.enabled);
  put_under(root, prefix, "mode", movement.mode);
  put_under(root, prefix, "send_rate_hz", movement.send_rate_hz);
  put_under(root, prefix, "max_ground_speed_blocks_per_second", movement.max_ground_speed_blocks_per_second);
  put_under(root, prefix, "max_air_speed_blocks_per_second", movement.max_air_speed_blocks_per_second);
  put_under(root, prefix, "max_swim_speed_blocks_per_second", movement.max_swim_speed_blocks_per_second);
  put_under(root, prefix, "max_fly_speed_blocks_per_second", movement.max_fly_speed_blocks_per_second);
  put_under(root, prefix, "max_vertical_speed_blocks_per_second", movement.max_vertical_speed_blocks_per_second);
  put_under(root, prefix, "base_position_epsilon_blocks", movement.base_position_epsilon_blocks);
  put_under(root, prefix, "lag_grace_blocks", movement.lag_grace_blocks);
  put_under(root, prefix, "max_single_packet_distance_blocks", movement.max_single_packet_distance_blocks);
  put_under(root, prefix, "max_validation_window_seconds", movement.max_validation_window_seconds);
  put_under(root, prefix, "reject_unloaded_chunk_movement", movement.reject_unloaded_chunk_movement);
  put_under(root, prefix, "solid_collision_check", movement.solid_collision_check);
  put_under(root, prefix, "correction_cooldown_ms", movement.correction_cooldown_ms);
}

void put_network_keep_alive_config(
    DataValue& root,
    std::string_view prefix,
    const KeepAliveConfig& keep_alive) {
  put_under(root, prefix, "interval_seconds", keep_alive.interval_seconds);
  put_under(root, prefix, "client_timeout_seconds", keep_alive.client_timeout_seconds);
}

void put_web_network_config(
    DataValue& root,
    std::string_view prefix,
    const WebNetworkConfig& web) {
  put_under(root, prefix, "enabled", web.enabled);
  put_under(root, prefix, "bind_address", web.bind_address);
  put_under(root, prefix, "port", web.port);
  put_under(root, prefix, "path", web.path);
  put_under(root, prefix, "max_frame_bytes", web.max_frame_bytes);
  put_under(root, prefix, "max_pending_bytes", web.max_pending_bytes);
}

void put_network_config(DataValue& root, const NetworkConfig& network) {
  put(root, "network.bind_address", network.bind_address);
  put(root, "network.port", network.port);
  put_network_movement_config(root, "network.movement", network.movement);
  put_network_keep_alive_config(root, "network.keep_alive", network.keep_alive);
  put_web_network_config(root, "network.web", network.web);
}

void put_multiplayer_config(DataValue& root, const MultiplayerConfig& multiplayer) {
  put(
      root,
      "multiplayer.keep_alive.server_timeout_seconds",
      multiplayer.keep_alive.server_timeout_seconds);
  put(root, "multiplayer.connection_timeout_seconds", multiplayer.connection_timeout_seconds);
}

void put_skins_config(DataValue& root, const SkinConfig& skins) {
  put(root, "skins.enabled", skins.enabled);
  put(root, "skins.allow_uploads", skins.allow_uploads);
  DataValue resolutions = DataValue::array();
  for (const SkinResolutionConfig& resolution : skins.resolutions) {
    DataValue entry = DataValue::object();
    entry.set("size", config_value(resolution.size));
    entry.set("enabled", config_value(resolution.enabled));
    if (!resolution.permission.empty()) {
      entry.set("permission", config_value(resolution.permission));
    }
    if (resolution.max_upload_bytes != default_skin_max_upload_bytes(resolution.size) ||
        resolution.size == 64U) {
      entry.set("max_upload_bytes", config_value(resolution.max_upload_bytes));
    }
    if (resolution.max_decoded_bytes != default_skin_max_decoded_bytes(resolution.size) ||
        resolution.size == 64U) {
      entry.set("max_decoded_bytes", config_value(resolution.max_decoded_bytes));
    }
    resolutions.push(std::move(entry));
  }
  put_path(root, "skins.resolutions", std::move(resolutions));
  put(root, "skins.uploads.max_chunk_bytes", skins.uploads.max_chunk_bytes);
  put(root, "skins.uploads.chunk_threshold_bytes", skins.uploads.chunk_threshold_bytes);
  put(root, "skins.uploads.max_active_per_connection", skins.uploads.max_active_per_connection);
  put(root, "skins.uploads.max_active_global", skins.uploads.max_active_global);
  put(root, "skins.uploads.timeout_seconds", skins.uploads.timeout_seconds);
  put(root, "skins.uploads.idle_timeout_seconds", skins.uploads.idle_timeout_seconds);
  put(root, "skins.uploads.max_begin_attempts_per_minute", skins.uploads.max_begin_attempts_per_minute);
  put(root, "skins.uploads.max_completed_uploads_per_minute", skins.uploads.max_completed_uploads_per_minute);
  put(root, "skins.uploads.max_completed_uploads_per_hour", skins.uploads.max_completed_uploads_per_hour);
  put(root, "skins.uploads.max_upload_bytes_per_hour", skins.uploads.max_upload_bytes_per_hour);
  put(root, "skins.uploads.max_failed_decodes_per_minute", skins.uploads.max_failed_decodes_per_minute);
  put(root, "skins.downloads.max_requests_per_second", skins.downloads.max_requests_per_second);
  put(root, "skins.downloads.max_requests_per_minute", skins.downloads.max_requests_per_minute);
  put(root, "skins.downloads.max_bytes_per_minute", skins.downloads.max_bytes_per_minute);
  put(root, "skins.downloads.max_in_flight_per_connection", skins.downloads.max_in_flight_per_connection);
  put(root, "skins.downloads.require_advertised_hash", skins.downloads.require_advertised_hash);
  put(root, "skins.downloads.max_batch_size", skins.downloads.max_batch_size);
  put(root, "skins.storage.max_total_skin_bytes", skins.storage.max_total_skin_bytes);
  put(root, "skins.storage.max_skins_per_profile", skins.storage.max_skins_per_profile);
}

void read_network_sections(const DataValue& root, EngineConfig& config) {
  const DataValue* network = root.get("network");
  if (network != nullptr) {
    read_scalar(*network, "bind_address", config.network.bind_address);
    read_scalar(*network, "port", config.network.port);
    const DataValue* movement = network->get("movement");
    if (movement != nullptr) {
      read_movement_validation_config(*movement, config.network.movement);
    }
    const DataValue* keep_alive = network->get("keep_alive");
    if (keep_alive != nullptr) {
      read_keep_alive_config(*keep_alive, config.network.keep_alive);
    }
    const DataValue* web = network->get("web");
    if (web != nullptr) {
      read_web_network_config(*web, config.network.web);
    }
  }

  const DataValue* multiplayer = root.get("multiplayer");
  if (multiplayer != nullptr) {
    read_multiplayer_config(*multiplayer, config.multiplayer);
  }

  const DataValue* skins = root.get("skins");
  if (skins != nullptr) {
    read_skins_config(*skins, config.skins);
  }
}

void put_network_full(DataValue& root, const EngineConfig& config) {
  put_network_config(root, config.network);
  put_multiplayer_config(root, config.multiplayer);
}

void put_network_client(DataValue& root, const EngineConfig& config) {
  put_multiplayer_config(root, config.multiplayer);
}

void put_network_server(DataValue& root, const EngineConfig& config) {
  put_network_config(root, config.network);
  put_skins_config(root, config.skins);
}

} // namespace arv::io::detail
