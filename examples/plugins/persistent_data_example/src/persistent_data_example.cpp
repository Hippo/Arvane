#include <arv/plugin.hpp>

struct SampleWorldData {
  std::uint64_t launches = 0;
  bool initialized = true;
};

template<>
struct arv::Codec<SampleWorldData> {
  static constexpr std::uint32_t schema_version = 1;

  static arv::Result encode(const arv::DataDocument& document, const SampleWorldData& data) {
    if (arv::Result result = document.set_u64("launches", data.launches); !result) {
      return result;
    }
    return document.set_bool("initialized", data.initialized);
  }

  static arv::DecodeResult<SampleWorldData> decode(const arv::DataDocument& document) {
    SampleWorldData data;
    if (auto launches = document.u64_value("launches")) {
      data.launches = launches.value;
    }
    if (auto initialized = document.bool_value("initialized")) {
      data.initialized = initialized.value;
    }
    return {arv::Result::ok(), data};
  }
};

static arv::Result update_sample_world_data(arv::Plugin& plugin, ArvWorldHandle world) {
  auto scope = plugin.persistent_data().world(world);
  SampleWorldData data = scope.load_or_default<SampleWorldData>(
      "persistent_data_example:world/sample");
  ++data.launches;
  return scope.save("persistent_data_example:world/sample", data);
}

ARV_MAIN(plugin) {
  (void)&update_sample_world_data;
  plugin.logger().info("Persistent data example loaded.");
  return true;
}
