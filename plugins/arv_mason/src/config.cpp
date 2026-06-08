#include "arv_mason/config.hpp"

#include "arv_mason/ids.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

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

MasonConfig default_config() {
  return {};
}

MasonMessages default_messages() {
  MasonMessages messages;
  messages.entries = {
      {"example-message", "{arv_mason_prefix} My message"},
      {"help", "{arv_mason_prefix} /mason marker, set, replace, copy, paste, undo, redo, confirm."},
      {"aliases", "{arv_mason_prefix} Aliases: /mm /mmarker /mset /mreplace /mcopy /mpaste /mundo /mredo."},
      {"status", "{arv_mason_prefix} Mason 0.1.0 ready. Limits: selection {selection_limit}, clipboard {clipboard_limit}."},
      {"unknown-subcommand", "{arv_mason_prefix} Unknown Mason subcommand. Try /mason help."},
      {"usage-marker", "{arv_mason_prefix} Usage: /mason marker."},
      {"usage-set", "{arv_mason_prefix} Usage: /mason set <block_state>."},
      {"usage-replace", "{arv_mason_prefix} Usage: /mason replace <from> <to>."},
      {"usage-copy", "{arv_mason_prefix} Usage: /mason copy."},
      {"usage-paste", "{arv_mason_prefix} Usage: /mason paste [--air|--no-air]."},
      {"usage-undo", "{arv_mason_prefix} Usage: /mason undo."},
      {"usage-redo", "{arv_mason_prefix} Usage: /mason redo."},
      {"usage-confirm", "{arv_mason_prefix} Usage: /mason confirm."},
      {"player-required", "{arv_mason_prefix} This Mason command requires a player."},
      {"block-state-empty", "{arv_mason_prefix} Block state must not be empty."},
      {"unknown-block-state", "{arv_mason_prefix} Unknown block state: {block_state}."},
      {"selection-read-failed", "{arv_mason_prefix} Failed to read selection: {error}."},
      {"paste-destination-read-failed", "{arv_mason_prefix} Failed to read paste destination: {error}."},
      {"current-selection-read-failed", "{arv_mason_prefix} Unable to read current selection: {error}"},
      {"selection-invalid", "{arv_mason_prefix} {message}"},
      {"permission-denied", "{arv_mason_prefix} Permission denied: {permission}."},
      {"world-edit-player-required", "{arv_mason_prefix} World edits require a player source."},
      {"permission-denied-modify", "{arv_mason_prefix} Permission denied: can_modify_world is required."},
      {"marker-create-failed", "{arv_mason_prefix} Failed to create marker item data: {error}."},
      {"marker-give-failed", "{arv_mason_prefix} Failed to give marker item: {error}."},
      {"marker-given", "{arv_mason_prefix} Marker added to your inventory."},
      {"operation-limit-exceeded", "{arv_mason_prefix} Operation exceeds limit: {count} blocks. Limit: {limit}."},
      {"confirmation-required", "{arv_mason_prefix} Operation affects {count} blocks. Run /mason confirm to proceed."},
      {"set-complete", "Set complete"},
      {"replace-complete", "Replace complete"},
      {"paste-complete", "Paste complete"},
      {"operation-complete-zero", "{arv_mason_prefix} {summary}: changed 0 blocks."},
      {"operation-started", "{arv_mason_prefix} Operation started: {count} blocks in batches of {batch}."},
      {"operation-failed", "{arv_mason_prefix} Operation failed: {error}."},
      {"operation-complete", "{arv_mason_prefix} {summary}: changed {count} blocks."},
      {"no-pending-confirm", "{arv_mason_prefix} No Mason operation is awaiting confirmation."},
      {"confirm", "{arv_mason_prefix} Confirmed Mason {operation} operation."},
      {"nothing-undo", "{arv_mason_prefix} Nothing to undo."},
      {"nothing-redo", "{arv_mason_prefix} Nothing to redo."},
      {"undo-failed", "{arv_mason_prefix} Undo failed: {error}."},
      {"redo-failed", "{arv_mason_prefix} Redo failed: {error}."},
      {"undo-complete", "{arv_mason_prefix} Undo complete: changed {count} blocks."},
      {"redo-complete", "{arv_mason_prefix} Redo complete: changed {count} blocks."},
      {"copy-selection-failed", "{arv_mason_prefix} Failed to copy selection: {error}."},
      {"clipboard-limit-exceeded", "{arv_mason_prefix} Clipboard exceeds limit: {count} blocks. Limit: {limit}."},
      {"copy-requires-position", "{arv_mason_prefix} Copy requires a player position."},
      {"copy-complete", "{arv_mason_prefix} Copied {count} blocks."},
      {"clipboard-empty", "{arv_mason_prefix} Clipboard is empty. Use /mason copy first."},
      {"paste-requires-position", "{arv_mason_prefix} Paste requires a player world and position."},
  };
  return messages;
}

std::string_view MasonMessages::get(std::string_view key) const noexcept {
  const auto found = entries.find(std::string{key});
  if (found == entries.end()) {
    return {};
  }
  return found->second;
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
  auto loaded = config.global().load_file<MasonConfig>(ids::config_file);
  if (!loaded) {
    logger.warn("Mason config was not loaded; using safe defaults.");
    validate_config(value);
    (void)config.global().save_file(ids::config_file, value);
    return value;
  }
  value = std::move(loaded.value);
  validate_config(value);
  logger.info("Mason config loaded.");
  return value;
}

MasonMessages load_messages(const arv::Config& config, const arv::Logger& logger) {
  MasonMessages value = default_messages();
  auto loaded = config.global().load_file<MasonMessages>(ids::messages_file);
  if (!loaded) {
    logger.warn("Mason messages config was not loaded; using built-in messages.");
    (void)config.global().save_file(ids::messages_file, value);
    return value;
  }
  value = std::move(loaded.value);
  logger.info("Mason messages config loaded.");
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
  config.limits.max_selection_blocks = document.u64_value(
      "limits.max_selection_blocks",
      config.limits.max_selection_blocks);
  config.limits.max_clipboard_blocks = document.u64_value(
      "limits.max_clipboard_blocks",
      config.limits.max_clipboard_blocks);
  config.limits.max_blueprint_blocks = document.u64_value(
      "limits.max_blueprint_blocks",
      config.limits.max_blueprint_blocks);
  config.limits.max_history_entries = document.u64_value(
      "limits.max_history_entries",
      config.limits.max_history_entries);
  config.limits.max_operation_blocks_per_tick = document.u64_value(
      "limits.max_operation_blocks_per_tick",
      config.limits.max_operation_blocks_per_tick);
  config.behavior.default_paste_air = document.bool_value(
      "behavior.default_paste_air",
      config.behavior.default_paste_air);
  config.behavior.require_confirmation_above_blocks = document.u64_value(
      "behavior.require_confirmation_above_blocks",
      config.behavior.require_confirmation_above_blocks);
  config.behavior.save_selection_per_player = document.bool_value(
      "behavior.save_selection_per_player",
      config.behavior.save_selection_per_player);
  config.behavior.save_clipboard_per_player = document.bool_value(
      "behavior.save_clipboard_per_player",
      config.behavior.save_clipboard_per_player);
  config.behavior.show_progress_messages = document.bool_value(
      "behavior.show_progress_messages",
      config.behavior.show_progress_messages);
  config.blueprints.default_scope = std::string{document.string_value(
      "blueprints.default_scope",
      config.blueprints.default_scope)};
  arv_mason::validate_config(config);
  return {Result::ok(), config};
}

Result Codec<arv_mason::MasonMessages>::encode(
    const DataDocument& document,
    const arv_mason::MasonMessages& value) {
  if (Result result = document.set_string("prefix", value.prefix); !result) {
    return result;
  }
  for (const auto& [key, message] : value.entries) {
    if (Result result = document.set_string(key, message); !result) {
      return result;
    }
  }
  return Result::ok();
}

DecodeResult<arv_mason::MasonMessages> Codec<arv_mason::MasonMessages>::decode(
    const DataDocument& document) {
  arv_mason::MasonMessages messages = arv_mason::default_messages();
  messages.prefix = std::string{document.string_value("prefix", messages.prefix)};
  for (auto& [key, message] : messages.entries) {
    message = std::string{document.string_value(key, message)};
  }
  return {Result::ok(), messages};
}

} // namespace arv
