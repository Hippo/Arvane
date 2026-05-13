#include <arv/buffer.hpp>
#include <arv/commands.hpp>
#include <arv/config.hpp>
#include <arv/context.hpp>
#include <arv/data.hpp>
#include <arv/codec.hpp>
#include <arv/entities.hpp>
#include <arv/events.hpp>
#include <arv/gamemode.hpp>
#include <arv/logger.hpp>
#include <arv/metrics.hpp>
#include <arv/network.hpp>
#include <arv/permissions.hpp>
#include <arv/persistent_data.hpp>
#include <arv/plugin.hpp>
#include <arv/registries.hpp>
#include <arv/regions.hpp>
#include <arv/resources.hpp>
#include <arv/result.hpp>
#include <arv/scheduler.hpp>
#include <arv/services.hpp>
#include <arv/text.hpp>
#include <arv/types.hpp>
#include <arv/world.hpp>
#include <arv/worldgen.hpp>

#include <chrono>

struct SampleConfig {
  int max_items = 1000;
  bool enabled = true;
};

template<>
struct arv::Codec<SampleConfig> {
  static constexpr std::uint32_t schema_version = 1;

  static arv::Result encode(const arv::DataDocument& document, const SampleConfig& value) {
    arv::Result result = document.set_i64("max_items", value.max_items);
    if (!result) {
      return result;
    }
    return document.set_bool("enabled", value.enabled);
  }

  static arv::DecodeResult<SampleConfig> decode(const arv::DataDocument& document) {
    SampleConfig config;
    if (auto max_items = document.i64_value("max_items")) {
      config.max_items = static_cast<int>(max_items.value);
    }
    if (auto enabled = document.bool_value("enabled")) {
      config.enabled = enabled.value;
    }
    return {arv::Result::ok(), config};
  }
};

int arv_plugin_sdk_cpp_compile_test() {
  arv::Plugin plugin{nullptr};
  arv::Buffer buffer;
  arv::CommandContext command_context{nullptr};
  arv::Config config{nullptr};
  arv::Context context{nullptr};
  arv::Data data{nullptr};
  arv::Entities entities{nullptr};
  arv::Event event{nullptr};
  arv::Events events{nullptr};
  arv::GameModes gamemodes{nullptr};
  arv::Logger logger{nullptr};
  arv::Metrics metrics{nullptr};
  arv::Network network{nullptr};
  arv::Packet packet;
  arv::Permissions permissions{nullptr};
  arv::PersistentData persistent_data{nullptr};
  arv::PersistentScopeRef persistent_scope = persistent_data.world(1);
  arv::Registries registries{nullptr};
  arv::Regions regions{nullptr};
  arv::Resources resources{nullptr};
  arv::Job job{[] {}};
  arv::Scheduler scheduler{nullptr};
  arv::Services services{nullptr};
  arv::Text text{nullptr};
  arv::TextComponent text_component;
  arv::World world{nullptr};
  arv::WorldGen worldgen{nullptr};

  auto command_handler = [](arv::CommandContext& ctx) {
    ctx.info("ok");
    return arv::Result::ok();
  };
  auto event_handler = [](arv::Event) {};
  auto job_handler = [] {};
  logger.info("ok");
  metrics.counter_add("test:metric", 1);
  job();

  const arv::Result command_result = plugin.commands().register_executor(
      {
          .executor_id = "test:command",
          .name = "test",
      },
      command_handler);
  const arv::Result event_result = plugin.events().subscribe(
      "test:event",
      event_handler);
  const arv::Result job_result = plugin.scheduler().sync(job_handler);
  const arv::Result job_ms_result = plugin.scheduler().sync(
      std::chrono::milliseconds{10},
      job_handler);
  const arv::Result job_tick_result = plugin.scheduler().sync_tick(
      1,
      job_handler);
  const arv::PacketResult packet_result = network.create_chat_message(text_component);
  const ArvRegionFlagDefinition flag_definition{
      arv::view("test:pvp"),
      arv::view("pvp"),
      ARV_REGION_RULE_ALLOW};
  const ArvZoneDefinition zone_definition{
      arv::view("test:zone"),
      0,
      {},
      nullptr,
      0};
  const ArvRegionHandle region_handle{};
  const ArvBlockPos block_pos{};

  arv::Result result;
  auto created_document = data.create_document();
  SampleConfig sample{};
  return arv::ApiVersion == 1u && result && buffer.empty() &&
                 command_context.arg_count() == 0 && !command_result &&
                 !event_result && !job_result && !job_ms_result &&
                 !job_tick_result && !event.id().data() &&
                 !events.subscribe("test:event", event_handler) &&
                 !config.global().load("test:config") &&
                 !config.global().save("test:config", sample) &&
                 config.global().load_or_default<SampleConfig>("test:config").enabled &&
                 !context.owns_namespace("test") &&
                 !created_document &&
                 !entities.exists({}) && !network.unsupported() &&
                 !packet_result && !network.broadcast(packet) &&
                 !gamemodes.exists("survival") && !permissions.unsupported() &&
                 !persistent_scope.load("test:data") &&
                 !persistent_scope.save("test:data", sample) &&
                 persistent_scope.load_or_default<SampleConfig>("test:data").enabled &&
                 !persistent_data.server().remove("test:data") &&
                 !persistent_data.entity(1).load("test:data") &&
                 !persistent_data.block(1, block_pos).load("test:data") &&
                 !persistent_data.chunk(1, {0, 0}).load("test:data") &&
                 !registries.register_item_behavior("test:item") &&
                 !registries.register_entity_behavior("test:entity_behavior") &&
                 !registries.register_component_codec("test:component") &&
                 !registries.register_loot_function("test:loot") &&
                 !registries.register_recipe_type("test:recipe") &&
                 !regions.register_region_flag(flag_definition) &&
                 !regions.register_runtime_zone(zone_definition) &&
                 !regions.region_flag("test:region", "test:pvp") &&
                 !regions.query_flag({}, "test:pvp") &&
                 !regions.set_region_flag(region_handle, "test:pvp", arv::RegionRuleValue::Allow) &&
                 !regions.set_zone_flag(region_handle, "test:pvp", arv::RegionRuleValue::Deny) &&
                 regions.find_regions_at({}).empty() &&
                 !resources.exists(arv::ResourceKind::Data, "test:data") &&
                 !scheduler.sync(job_handler) &&
                 !scheduler.async(std::chrono::milliseconds{10}, job_handler) &&
                 !scheduler.async_tick(1, job_handler) &&
                 services.list("test:svc", 0, 0).empty() &&
                 !text.unsupported() && !world.current_tick({}) &&
                 !worldgen.register_feature_type("test:feature") &&
                 !worldgen.register_noise_type("test:noise") &&
                 !worldgen.register_density_function_type("test:density") &&
                 plugin.context().namespace_count() == 0
             ? 0
             : 1;
}
