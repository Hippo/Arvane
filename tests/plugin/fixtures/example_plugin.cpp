#include "arv/plugin/plugin_api.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

const ArvPluginAPI* g_api{};
std::atomic<int> g_sync_ticks{};
std::atomic<int> g_async_ticks{};
std::atomic<int> g_thread_violations{};
std::atomic<int> g_server_starts{};
std::atomic<int> g_server_stops{};
int g_service_value = 42;

void sync_job(void*) {
  g_sync_ticks.fetch_add(1, std::memory_order_relaxed);
}

void async_job(void*) {
  g_async_ticks.fetch_add(1, std::memory_order_relaxed);
  ArvPluginBlockPos pos{0, 0, 0};
  ArvPluginResult result =
      g_api->worlds->set_block(g_api->worlds->context, 1, pos, 0);
  if (result.code == ARV_PLUGIN_RESULT_THREAD_VIOLATION) {
    g_thread_violations.fetch_add(1, std::memory_order_relaxed);
  }
}

void event_handler(const ArvPluginEvent* event, void*) {
  if (event == nullptr || event->event_id == nullptr) {
    return;
  }
  const char* id = event->event_id;
  if (std::string_view{id} == "arvane:event/server_start") {
    g_server_starts.fetch_add(1, std::memory_order_relaxed);
  } else if (std::string_view{id} == "arvane:event/server_stop") {
    g_server_stops.fetch_add(1, std::memory_order_relaxed);
  }
}

ArvPluginResult command(
    ArvPluginCommandContext* ctx,
    void*) {
  char message[192];
  std::snprintf(
      message,
      sizeof(message),
      "sync=%d async=%d thread_violations=%d starts=%d stops=%d",
      g_sync_ticks.load(std::memory_order_relaxed),
      g_async_ticks.load(std::memory_order_relaxed),
      g_thread_violations.load(std::memory_order_relaxed),
      g_server_starts.load(std::memory_order_relaxed),
      g_server_stops.load(std::memory_order_relaxed));
  ctx->reply(ctx, ARV_PLUGIN_COMMAND_MESSAGE_SUCCESS, message);
  return {ARV_PLUGIN_RESULT_OK, ""};
}

bool result_ok(ArvPluginResult result) {
  return result.code == ARV_PLUGIN_RESULT_OK;
}

bool buffer_equals(ArvPluginByteBuffer buffer, std::string_view expected) {
  return buffer.size == expected.size() &&
         std::memcmp(buffer.data, expected.data(), expected.size()) == 0;
}

} // namespace

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  g_sync_ticks.store(0, std::memory_order_relaxed);
  g_async_ticks.store(0, std::memory_order_relaxed);
  g_thread_violations.store(0, std::memory_order_relaxed);
  g_server_starts.store(0, std::memory_order_relaxed);
  g_server_stops.store(0, std::memory_order_relaxed);
  g_api = api;
  api->logger->info(api->logger->context, "example plugin loaded");

  if (!api->context->owns_namespace(api->context->context, "example_plugin_pack")) {
    return false;
  }
  if (api->context->owns_namespace(api->context->context, "other_pack")) {
    return false;
  }

  constexpr std::string_view expected_resource = "probe=data\n";
  if (!api->resources->exists(
          api->resources->context,
          ARV_PLUGIN_RESOURCE_DATA,
          "example_plugin_pack:plugin_probe",
          ".txt")) {
    return false;
  }
  ArvPluginByteBuffer resource_buffer{};
  if (!result_ok(api->resources->read(
          api->resources->context,
          ARV_PLUGIN_RESOURCE_DATA,
          "example_plugin_pack:plugin_probe",
          ".txt",
          &resource_buffer))) {
    return false;
  }
  const bool resource_matches = buffer_equals(resource_buffer, expected_resource);
  api->resources->release_buffer(api->resources->context, resource_buffer);
  if (!resource_matches) {
    return false;
  }

  constexpr std::string_view expected_config = "enabled: true\n";
  if (!result_ok(api->config->save_plugin_config(
          api->config->context,
          "example_plugin_pack:settings",
          reinterpret_cast<const uint8_t*>(expected_config.data()),
          expected_config.size()))) {
    return false;
  }
  ArvPluginByteBuffer config_buffer{};
  if (!result_ok(api->config->load_plugin_config(
          api->config->context,
          "example_plugin_pack:settings",
          &config_buffer))) {
    return false;
  }
  const bool config_matches = buffer_equals(config_buffer, expected_config);
  api->resources->release_buffer(api->resources->context, config_buffer);
  if (!config_matches) {
    return false;
  }

  if (!result_ok(api->registries->register_block_behavior(
          api->registries->context,
          "example_plugin_pack:glowing_block_behavior"))) {
    return false;
  }
  if (!result_ok(api->registries->register_item_behavior(
          api->registries->context,
          "example_plugin_pack:wand_use"))) {
    return false;
  }
  if (!result_ok(api->registries->register_entity_goal(
          api->registries->context,
          "example_plugin_pack:idle_goal"))) {
    return false;
  }

  ArvPluginCommandDescriptor command_descriptor{};
  command_descriptor.executor_id = "example_plugin_pack:test_command";
  command_descriptor.command_name = "testplugin";
  command_descriptor.domain = ARV_PLUGIN_COMMAND_SERVER_BOUND;
  command_descriptor.execute = &command;
  if (!result_ok(api->commands->register_executor(
          api->commands->context,
          &command_descriptor))) {
    return false;
  }

  ArvScheduledJobHandle sync_handle{};
  if (!result_ok(api->scheduler->sync_repeating(
          api->scheduler->context,
          1,
          1,
          &sync_job,
          nullptr,
          &sync_handle))) {
    return false;
  }
  ArvScheduledJobHandle async_handle{};
  if (!result_ok(api->scheduler->async_delayed(
          api->scheduler->context,
          1,
          &async_job,
          nullptr,
          &async_handle))) {
    return false;
  }

  ArvPluginServiceDescriptor service{};
  service.service_id = "example_plugin_pack:test_service";
  service.api_version = 1;
  service.priority = 10;
  service.threading = ARV_PLUGIN_SERVICE_ANY_THREAD;
  service.display_name = "Test Service";
  service.api_table = &g_service_value;
  ArvServiceHandle service_handle{};
  if (!result_ok(api->services->publish(
          api->services->context,
          &service,
          &service_handle))) {
    return false;
  }
  ArvPluginServiceRef service_ref{};
  if (!result_ok(api->services->get(
          api->services->context,
          "example_plugin_pack:test_service",
          1,
          1,
          &service_ref))) {
    return false;
  }
  if (service_ref.api_table != &g_service_value) {
    return false;
  }

  ArvEventSubscriptionHandle start_subscription{};
  if (!result_ok(api->events->subscribe(
          api->events->context,
          "arvane:event/server_start",
          ARV_PLUGIN_EVENT_PRIORITY_NORMAL,
          &event_handler,
          nullptr,
          &start_subscription))) {
    return false;
  }
  ArvEventSubscriptionHandle stop_subscription{};
  if (!result_ok(api->events->subscribe(
          api->events->context,
          "arvane:event/server_stop",
          ARV_PLUGIN_EVENT_PRIORITY_NORMAL,
          &event_handler,
          nullptr,
          &stop_subscription))) {
    return false;
  }

  return true;
}
