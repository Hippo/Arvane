#pragma once

#include <arv/codec.hpp>
#include <arv/config.hpp>
#include <arv/logger.hpp>

#include <cstdint>
#include <string>

namespace arv_mason {

struct MasonLimits {
  std::uint64_t max_selection_blocks{250000};
  std::uint64_t max_clipboard_blocks{250000};
  std::uint64_t max_blueprint_blocks{1000000};
  std::uint64_t max_history_entries{15};
  std::uint64_t max_operation_blocks_per_tick{8192};
};

struct MasonBehavior {
  bool default_paste_air{false};
  std::uint64_t require_confirmation_above_blocks{100000};
  bool save_selection_per_player{true};
  bool save_clipboard_per_player{false};
  bool show_progress_messages{true};
};

struct MasonBlueprintConfig {
  std::string default_scope{"world"};
};

struct MasonConfig {
  MasonLimits limits{};
  MasonBehavior behavior{};
  MasonBlueprintConfig blueprints{};
};

[[nodiscard]] MasonConfig default_config();
void validate_config(MasonConfig& config);
[[nodiscard]] MasonConfig load_config(const arv::Config& config, const arv::Logger& logger);

} // namespace arv_mason

namespace arv {

template<>
struct Codec<arv_mason::MasonConfig> {
  static constexpr std::uint32_t schema_version = 1;

  [[nodiscard]] static Result encode(const DataDocument& document, const arv_mason::MasonConfig& value);
  [[nodiscard]] static DecodeResult<arv_mason::MasonConfig> decode(const DataDocument& document);
};

} // namespace arv
