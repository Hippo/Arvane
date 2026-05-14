#include <arv/plugin.hpp>

struct SampleConfig {
  int max_items = 1000;
  bool enabled = true;
};

template<>
struct arv::Codec<SampleConfig> {
  static constexpr std::uint32_t schema_version = 1;

  static arv::Result encode(const arv::DataDocument& document, const SampleConfig& config) {
    if (arv::Result result = document.set_i64("max_items", config.max_items); !result) {
      return result;
    }
    return document.set_bool("enabled", config.enabled);
  }

  static arv::DecodeResult<SampleConfig> decode(const arv::DataDocument& document) {
    SampleConfig config;
    if (auto max_items = document.i64_value("max_items")) {
      config.max_items = static_cast<int>(max_items.value());
    }
    if (auto enabled = document.bool_value("enabled")) {
      config.enabled = enabled.value();
    }
    return {arv::Result::ok(), config};
  }
};

ARV_MAIN(plugin) {
  SampleConfig config = plugin.config().global().load_or_default<SampleConfig>(
      "config_example:sample_config");
  config.max_items = config.max_items < 1 ? 1 : config.max_items;
  return plugin.config().global().save("config_example:sample_config", config).ok_value();
}
