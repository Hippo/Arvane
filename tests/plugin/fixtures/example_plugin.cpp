#include <arv/api.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

const ArvAPI* g_api{};
std::atomic<int> g_sync_ticks{};
std::atomic<int> g_async_ticks{};
std::atomic<int> g_thread_violations{};
std::atomic<int> g_server_starts{};
std::atomic<int> g_server_stops{};
std::atomic<int> g_api_checks{};
std::atomic<int> g_network_sends{};
int g_service_value = 42;

bool view_equals(ArvStringView view, std::string_view expected);
bool result_ok(ArvResult result);

void sync_job(void*) {
  g_sync_ticks.fetch_add(1, std::memory_order_relaxed);

  bool checks_ok = true;
  constexpr ArvPlayerID player_id = 7;

  checks_ok = checks_ok && g_api->gamemodes->exists(
      g_api->gamemodes_ctx,
      arv_cstr("survival"));
  ArvStringView current_mode{};
  checks_ok = checks_ok && result_ok(g_api->gamemodes->current(
      g_api->gamemodes_ctx,
      player_id,
      &current_mode)) &&
      view_equals(current_mode, "survival");

  checks_ok = checks_ok && result_ok(g_api->permissions->add_group(
      g_api->permissions_ctx,
      player_id,
      arv_cstr("example_plugin_pack:vip")));
  ArvPermissionValue custom_value = ARV_PERMISSION_UNSET;
  checks_ok = checks_ok && result_ok(g_api->permissions->check_custom(
      g_api->permissions_ctx,
      player_id,
      arv_cstr("example_plugin_pack.command.test"),
      &custom_value)) &&
      custom_value == ARV_PERMISSION_ALLOW;

  ArvCorePermissionSet region_core{};
  region_core.can_modify_world = ARV_PERMISSION_DENY;
  ArvRegionDefinition region{};
  region.id = arv_cstr("example_plugin_pack:runtime_spawn");
  region.world = 1;
  region.priority = 100;
  region.shape.kind = ARV_REGION_SHAPE_AABB;
  region.shape.aabb.min = {0.0F, 0.0F, -30.0F};
  region.shape.aabb.max = {20.0F, 40.0F, 0.0F};
  region.core = region_core;
  ArvRegionHandle region_handle{};
  checks_ok = checks_ok && result_ok(g_api->regions->create_runtime_region(
      g_api->regions_ctx,
      &region,
      &region_handle));
  ArvRegionQuery query{};
  query.world = 1;
  query.position = {8.0F, 20.0F, -20.0F};
  checks_ok = checks_ok && g_api->regions->find_regions_at(
      g_api->regions_ctx,
      &query,
      nullptr,
      0) >= 1;
  checks_ok = checks_ok && g_api->regions->active_regions(
      g_api->regions_ctx,
      player_id,
      nullptr,
      0) >= 1;
  checks_ok = checks_ok && g_api->regions->is_player_in_region(
      g_api->regions_ctx,
      player_id,
      arv_cstr("example_plugin_pack:runtime_spawn"));
  bool can_modify_world = true;
  checks_ok = checks_ok && result_ok(g_api->permissions->check_core(
      g_api->permissions_ctx,
      player_id,
      ARV_CORE_PERMISSION_CAN_MODIFY_WORLD,
      &can_modify_world)) &&
      !can_modify_world;
  checks_ok = checks_ok && result_ok(g_api->regions->remove_runtime_region(
      g_api->regions_ctx,
      region_handle));

  ArvTextHandle network_text{};
  ArvPacketHandle network_packet{};
  checks_ok = checks_ok && result_ok(g_api->text->literal(
      g_api->text_ctx,
      arv_cstr("network api probe"),
      &network_text));
  checks_ok = checks_ok && result_ok(g_api->network->create_clientbound_chat_message(
      g_api->network_ctx,
      network_text,
      arv_cstr("arvane:system"),
      ARV_CHAT_MESSAGE_SYSTEM,
      &network_packet));
  checks_ok = checks_ok && result_ok(g_api->network->send_to_player(
      g_api->network_ctx,
      player_id,
      network_packet));
  if (network_packet.value != 0U) {
    g_api->network->release_packet(g_api->network_ctx, network_packet);
  }
  if (network_text.value != 0U) {
    g_api->text->release(g_api->text_ctx, network_text);
  }
  if (checks_ok) {
    g_network_sends.fetch_add(1, std::memory_order_relaxed);
  }

  if (checks_ok) {
    g_api_checks.fetch_add(1, std::memory_order_relaxed);
  }
}

void async_job(void*) {
  g_async_ticks.fetch_add(1, std::memory_order_relaxed);
  ArvBlockPos pos{0, 0, 0};
  ArvResult result =
      g_api->worlds->set_block(g_api->worlds_ctx, 1, pos, 0);
  if (result.code == ARV_RESULT_THREAD_VIOLATION) {
    g_thread_violations.fetch_add(1, std::memory_order_relaxed);
  }
}

bool view_equals(ArvStringView view, std::string_view expected) {
  return view.size == expected.size() &&
         (view.size == 0U ||
          std::memcmp(view.data, expected.data(), expected.size()) == 0);
}

void event_handler(const ArvEvent* event, void*) {
  if (event == nullptr) {
    return;
  }
  if (view_equals(event->event_id, "arvane:event/server_start")) {
    g_server_starts.fetch_add(1, std::memory_order_relaxed);
  } else if (view_equals(event->event_id, "arvane:event/server_stop")) {
    g_server_stops.fetch_add(1, std::memory_order_relaxed);
  }
}

ArvResult command(
    ArvCommandContext* ctx,
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
  std::snprintf(
      message + std::strlen(message),
      sizeof(message) - std::strlen(message),
      " api_checks=%d",
      g_api_checks.load(std::memory_order_relaxed));
  std::snprintf(
      message + std::strlen(message),
      sizeof(message) - std::strlen(message),
      " network_sends=%d",
      g_network_sends.load(std::memory_order_relaxed));
  ctx->reply(ctx, ARV_COMMAND_MESSAGE_SUCCESS, arv_cstr(message));
  return {ARV_RESULT_OK, ""};
}

bool result_ok(ArvResult result) {
  return result.code == ARV_RESULT_OK;
}

bool buffer_equals(ArvByteBuffer buffer, std::string_view expected) {
  return buffer.size == expected.size() &&
         std::memcmp(buffer.data, expected.data(), expected.size()) == 0;
}

} // namespace

extern "C" ARV_EXPORT bool arvane_plugin_main(const ArvAPI* api) {
  if (api == nullptr || api->api_version != ARV_API_VERSION) {
    return false;
  }
  g_sync_ticks.store(0, std::memory_order_relaxed);
  g_async_ticks.store(0, std::memory_order_relaxed);
  g_thread_violations.store(0, std::memory_order_relaxed);
  g_server_starts.store(0, std::memory_order_relaxed);
  g_server_stops.store(0, std::memory_order_relaxed);
  g_api_checks.store(0, std::memory_order_relaxed);
  g_network_sends.store(0, std::memory_order_relaxed);
  g_api = api;
  api->logger->info(api->logger_ctx, arv_cstr("example plugin loaded"));

  if (!api->context->owns_namespace(api->context_ctx, arv_cstr("example_plugin_pack"))) {
    return false;
  }
  if (api->context->owns_namespace(api->context_ctx, arv_cstr("other_pack"))) {
    return false;
  }

  constexpr std::string_view expected_resource = "probe=data\n";
  if (!api->resources->exists(
          api->resources_ctx,
          ARV_RESOURCE_DATA,
          arv_cstr("example_plugin_pack:plugin_probe"),
          arv_cstr(".txt"))) {
    return false;
  }
  ArvByteBuffer resource_buffer{};
  if (!result_ok(api->resources->read(
          api->resources_ctx,
          ARV_RESOURCE_DATA,
          arv_cstr("example_plugin_pack:plugin_probe"),
          arv_cstr(".txt"),
          &resource_buffer))) {
    return false;
  }
  const bool resource_matches = buffer_equals(resource_buffer, expected_resource);
  api->resources->release_buffer(api->resources_ctx, resource_buffer);
  if (!resource_matches) {
    return false;
  }

  constexpr std::string_view expected_config = "enabled: true\n";
  if (!result_ok(api->config->save_plugin_config(
          api->config_ctx,
          arv_cstr("example_plugin_pack:settings"),
          {reinterpret_cast<const uint8_t*>(expected_config.data()),
           expected_config.size()}))) {
    return false;
  }
  ArvByteBuffer config_buffer{};
  if (!result_ok(api->config->load_plugin_config(
          api->config_ctx,
          arv_cstr("example_plugin_pack:settings"),
          &config_buffer))) {
    return false;
  }
  const bool config_matches = buffer_equals(config_buffer, expected_config);
  api->resources->release_buffer(api->resources_ctx, config_buffer);
  if (!config_matches) {
    return false;
  }

  ArvTextHandle hello{};
  ArvTextHandle world{};
  ArvTextHandle red_hello{};
  ArvTextHandle combined{};
  ArvTextHandle clickable{};
  if (!result_ok(api->text->literal(api->text_ctx, arv_cstr("Hello"), &hello))) {
    return false;
  }
  if (!result_ok(api->text->literal(api->text_ctx, arv_cstr(" world"), &world))) {
    return false;
  }
  if (!result_ok(api->text->color(api->text_ctx, hello, {255, 64, 64}, &red_hello))) {
    return false;
  }
  if (!result_ok(api->text->append(api->text_ctx, red_hello, world, &combined))) {
    return false;
  }
  if (!result_ok(api->text->click(
          api->text_ctx,
          combined,
          ARV_TEXT_CLICK_SUGGEST_COMMAND,
          arv_cstr("/help"),
          &clickable))) {
    return false;
  }
  if (!result_ok(api->text->validate(api->text_ctx, clickable, nullptr))) {
    return false;
  }
  ArvByteBuffer plain_text{};
  if (!result_ok(api->text->plain_text(api->text_ctx, clickable, &plain_text))) {
    return false;
  }
  const bool plain_matches = buffer_equals(plain_text, "Hello world");
  api->resources->release_buffer(api->resources_ctx, plain_text);
  ArvByteBuffer rich_text{};
  if (!result_ok(api->text->serialize(api->text_ctx, clickable, &rich_text))) {
    return false;
  }
  const bool serialized_nonempty = rich_text.size > 0U;
  api->resources->release_buffer(api->resources_ctx, rich_text);
  api->text->release(api->text_ctx, clickable);
  api->text->release(api->text_ctx, combined);
  api->text->release(api->text_ctx, red_hello);
  api->text->release(api->text_ctx, world);
  api->text->release(api->text_ctx, hello);
  if (!plain_matches || !serialized_nonempty) {
    return false;
  }

  ArvPermissionNodeDefinition permission_node{};
  permission_node.node = arv_cstr("example_plugin_pack.command.test");
  permission_node.description = arv_cstr("Test permission");
  permission_node.default_value = ARV_PERMISSION_UNSET;
  if (!result_ok(api->permissions->register_permission_node(
          api->permissions_ctx,
          &permission_node))) {
    return false;
  }
  ArvCustomPermissionEntry group_permission{};
  group_permission.node = arv_cstr("example_plugin_pack.command.test");
  group_permission.value = ARV_PERMISSION_ALLOW;
  ArvPermissionGroupDefinition group{};
  group.id = arv_cstr("example_plugin_pack:vip");
  group.priority = 10;
  group.custom_permissions = &group_permission;
  group.custom_permission_count = 1;
  if (!result_ok(api->permissions->register_group(api->permissions_ctx, &group))) {
    return false;
  }

  if (!result_ok(api->registries->register_block_behavior(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:glowing_block_behavior")))) {
    return false;
  }
  if (!result_ok(api->registries->register_item_behavior(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:wand_use")))) {
    return false;
  }
  if (!result_ok(api->registries->register_entity_goal(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:idle_goal")))) {
    return false;
  }

  ArvCommandDescriptor command_descriptor{};
  command_descriptor.executor_id = arv_cstr("example_plugin_pack:test_command");
  command_descriptor.command_name = arv_cstr("testplugin");
  command_descriptor.domain = ARV_COMMAND_SERVER_BOUND;
  command_descriptor.execute = &command;
  if (!result_ok(api->commands->register_executor(
          api->commands_ctx,
          &command_descriptor))) {
    return false;
  }

  ArvScheduledJobHandle sync_handle{};
  if (!result_ok(api->scheduler->sync_repeating(
          api->scheduler_ctx,
          1,
          1,
          &sync_job,
          nullptr,
          &sync_handle))) {
    return false;
  }
  ArvScheduledJobHandle async_handle{};
  if (!result_ok(api->scheduler->async_delayed(
          api->scheduler_ctx,
          1,
          &async_job,
          nullptr,
          &async_handle))) {
    return false;
  }

  ArvServiceDescriptor service{};
  service.service_id = arv_cstr("example_plugin_pack:test_service");
  service.api_version = 1;
  service.priority = 10;
  service.threading = ARV_SERVICE_ANY_THREAD;
  service.display_name = arv_cstr("Test Service");
  service.api_table = &g_service_value;
  ArvServiceHandle service_handle{};
  if (!result_ok(api->services->publish(
          api->services_ctx,
          &service,
          &service_handle))) {
    return false;
  }
  ArvServiceRef service_ref{};
  if (!result_ok(api->services->get(
          api->services_ctx,
          arv_cstr("example_plugin_pack:test_service"),
          1,
          1,
          &service_ref))) {
    return false;
  }
  if (service_ref.api_table != &g_service_value || !service_ref.valid) {
    return false;
  }

  ArvEventSubscriptionHandle start_subscription{};
  if (!result_ok(api->events->subscribe(
          api->events_ctx,
          arv_cstr("arvane:event/server_start"),
          ARV_EVENT_PRIORITY_NORMAL,
          &event_handler,
          nullptr,
          &start_subscription))) {
    return false;
  }
  ArvEventSubscriptionHandle stop_subscription{};
  if (!result_ok(api->events->subscribe(
          api->events_ctx,
          arv_cstr("arvane:event/server_stop"),
          ARV_EVENT_PRIORITY_NORMAL,
          &event_handler,
          nullptr,
          &stop_subscription))) {
    return false;
  }

  return true;
}
