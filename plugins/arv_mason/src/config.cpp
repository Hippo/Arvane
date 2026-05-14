#include "arv_mason/config.hpp"

#include "arv_mason/ids.hpp"

#include <algorithm>
#include <string>

namespace arv_mason {
namespace {

[[nodiscard]] std::uint64_t clamp_limit(
    std::uint64_t value,
    std::uint64_t fallback,
    std::uint64_t min_value,
    std::uint64_t max_value) {
  if (value == 0U) {
    value = fallback;
  }
  return std::clamp(value, min_value, max_value);
}

} // namespace

[[nodiscard]] std::uint64_t read_u64(
    const arv::DataDocument& document,
    std::string_view path,
    std::uint64_t fallback) {
  auto value = document.u64_value(path);
  return value ? value.value : fallback;
}

[[nodiscard]] bool read_bool(
    const arv::DataDocument& document,
    std::string_view path,
    bool fallback) {
  auto value = document.bool_value(path);
  return value ? value.value : fallback;
}

[[nodiscard]] std::string read_string(
    const arv::DataDocument& document,
    std::string_view path,
    std::string fallback) {
  auto value = document.string_value(path);
  return value ? std::string{value.value} : std::move(fallback);
}

MasonConfig default_config() {
  return {};
}

void validate_config(MasonConfig& config) {
  const MasonConfig fallback = default_config();
  config.limits.max_selection_blocks = clamp_limit(
      config.limits.max_selection_blocks,
      fallback.limits.max_selection_blocks,
      1,
      10000000);
  config.limits.max_clipboard_blocks = clamp_limit(
      config.limits.max_clipboard_blocks,
      fallback.limits.max_clipboard_blocks,
      1,
      10000000);
  config.limits.max_blueprint_blocks = clamp_limit(
      config.limits.max_blueprint_blocks,
      fallback.limits.max_blueprint_blocks,
      1,
      50000000);
  config.limits.max_history_entries = clamp_limit(
      config.limits.max_history_entries,
      fallback.limits.max_history_entries,
      1,
      100);
  config.limits.max_operation_blocks_per_tick = clamp_limit(
      config.limits.max_operation_blocks_per_tick,
      fallback.limits.max_operation_blocks_per_tick,
      1,
      100000);
  config.behavior.require_confirmation_above_blocks = clamp_limit(
      config.behavior.require_confirmation_above_blocks,
      fallback.behavior.require_confirmation_above_blocks,
      1,
      50000000);
  if (config.blueprints.default_scope != "world" &&
      config.blueprints.default_scope != "server") {
    config.blueprints.default_scope = fallback.blueprints.default_scope;
  }
}

MasonConfig load_config(const arv::Config& config, const arv::Logger& logger) {
  MasonConfig value = default_config();
  auto loaded = config.global().load<MasonConfig>(ids::config_main);
  if (!loaded) {
    logger.warn("Mason config was not loaded; using safe defaults.");
    validate_config(value);
    (void)config.global().save(ids::config_main, value);
    return value;
  }
  value = std::move(loaded.value);
  validate_config(value);
  logger.info("Mason config loaded.");
  return value;
}

} // namespace arv_mason

namespace arv {

Result Codec<arv_mason::MasonConfig>::encode(
    const DataDocument& document,
    const arv_mason::MasonConfig& value) {
  if (Result result = document.set_u64(
          "limits.max_selection_blocks",
          value.limits.max_selection_blocks);
      !result) {
    return result;
  }
  if (Result result = document.set_u64(
          "limits.max_clipboard_blocks",
          value.limits.max_clipboard_blocks);
      !result) {
    return result;
  }
  if (Result result = document.set_u64(
          "limits.max_blueprint_blocks",
          value.limits.max_blueprint_blocks);
      !result) {
    return result;
  }
  if (Result result = document.set_u64(
          "limits.max_history_entries",
          value.limits.max_history_entries);
      !result) {
    return result;
  }
  if (Result result = document.set_u64(
          "limits.max_operation_blocks_per_tick",
          value.limits.max_operation_blocks_per_tick);
      !result) {
    return result;
  }
  if (Result result = document.set_bool(
          "behavior.default_paste_air",
          value.behavior.default_paste_air);
      !result) {
    return result;
  }
  if (Result result = document.set_u64(
          "behavior.require_confirmation_above_blocks",
          value.behavior.require_confirmation_above_blocks);
      !result) {
    return result;
  }
  if (Result result = document.set_bool(
          "behavior.save_selection_per_player",
          value.behavior.save_selection_per_player);
      !result) {
    return result;
  }
  if (Result result = document.set_bool(
          "behavior.save_clipboard_per_player",
          value.behavior.save_clipboard_per_player);
      !result) {
    return result;
  }
  if (Result result = document.set_bool(
          "behavior.show_progress_messages",
          value.behavior.show_progress_messages);
      !result) {
    return result;
  }
  return document.set_string(
      "blueprints.default_scope",
      value.blueprints.default_scope);
}

DecodeResult<arv_mason::MasonConfig> Codec<arv_mason::MasonConfig>::decode(
    const DataDocument& document) {
  arv_mason::MasonConfig config = arv_mason::default_config();
  config.limits.max_selection_blocks = arv_mason::read_u64(
      document,
      "limits.max_selection_blocks",
      config.limits.max_selection_blocks);
  config.limits.max_clipboard_blocks = arv_mason::read_u64(
      document,
      "limits.max_clipboard_blocks",
      config.limits.max_clipboard_blocks);
  config.limits.max_blueprint_blocks = arv_mason::read_u64(
      document,
      "limits.max_blueprint_blocks",
      config.limits.max_blueprint_blocks);
  config.limits.max_history_entries = arv_mason::read_u64(
      document,
      "limits.max_history_entries",
      config.limits.max_history_entries);
  config.limits.max_operation_blocks_per_tick = arv_mason::read_u64(
      document,
      "limits.max_operation_blocks_per_tick",
      config.limits.max_operation_blocks_per_tick);
  config.behavior.default_paste_air = arv_mason::read_bool(
      document,
      "behavior.default_paste_air",
      config.behavior.default_paste_air);
  config.behavior.require_confirmation_above_blocks = arv_mason::read_u64(
      document,
      "behavior.require_confirmation_above_blocks",
      config.behavior.require_confirmation_above_blocks);
  config.behavior.save_selection_per_player = arv_mason::read_bool(
      document,
      "behavior.save_selection_per_player",
      config.behavior.save_selection_per_player);
  config.behavior.save_clipboard_per_player = arv_mason::read_bool(
      document,
      "behavior.save_clipboard_per_player",
      config.behavior.save_clipboard_per_player);
  config.behavior.show_progress_messages = arv_mason::read_bool(
      document,
      "behavior.show_progress_messages",
      config.behavior.show_progress_messages);
  config.blueprints.default_scope = arv_mason::read_string(
      document,
      "blueprints.default_scope",
      config.blueprints.default_scope);
  arv_mason::validate_config(config);
  return {Result::ok(), config};
}

} // namespace arv
