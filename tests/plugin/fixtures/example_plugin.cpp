#include <arv/api.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

const ArvAPI* g_api{};
std::atomic<int> g_sync_ticks{};
std::atomic<int> g_async_ticks{};
std::atomic<int> g_sync_ms_ticks{};
std::atomic<int> g_async_ms_ticks{};
std::atomic<int> g_thread_violations{};
std::atomic<int> g_server_starts{};
std::atomic<int> g_server_stops{};
std::atomic<int> g_api_checks{};
std::atomic<int> g_network_sends{};
std::atomic<int> g_chat_sends{};
int g_service_value = 42;

bool view_equals(ArvStringView view, std::string_view expected);
bool result_ok(ArvResult result);
bool buffer_equals(ArvByteBuffer buffer, std::string_view expected);
bool document_string_equals(ArvDataDocumentHandle document, std::string_view path, std::string_view expected);

ArvDataDocumentHandle make_probe_document(std::string_view value) {
  ArvDataDocumentHandle document =
      g_api->data->create_document(g_api->data_ctx, 1);
  if (document == 0) {
    return 0;
  }
  if (!result_ok(g_api->data->set_string(
          g_api->data_ctx,
          document,
          arv_cstr("value"),
          arv_string_view(value.data(), value.size())))) {
    g_api->data->release_document(g_api->data_ctx, document);
    return 0;
  }
  return document;
}

bool save_probe_document(
    const ArvPersistentScope& scope,
    std::string_view key,
    std::string_view value) {
  ArvDataDocumentHandle document = make_probe_document(value);
  if (document == 0) {
    return false;
  }
  const bool ok = result_ok(g_api->persistent_data->save(
      g_api->persistent_data_ctx,
      &scope,
      arv_string_view(key.data(), key.size()),
      document));
  g_api->data->release_document(g_api->data_ctx, document);
  return ok;
}

bool load_probe_document_equals(
    const ArvPersistentScope& scope,
    std::string_view key,
    std::string_view expected) {
  ArvDataDocumentHandle document{};
  const bool ok = result_ok(g_api->persistent_data->load(
      g_api->persistent_data_ctx,
      &scope,
      arv_string_view(key.data(), key.size()),
      &document)) &&
      document_string_equals(document, "value", expected);
  if (document != 0) {
    g_api->data->release_document(g_api->data_ctx, document);
  }
  return ok;
}

bool item_stack_data_checks(ArvPlayerID player_id) {
  if (g_api->items == nullptr) {
    return false;
  }

  ArvItemStackLocation selected{
      player_id,
      ARV_ITEM_STACK_SELECTED_HOTBAR,
      0U};
  ArvItemStackInfo info{};
  bool checks_ok = result_ok(g_api->items->stack_info(
      g_api->items_ctx,
      selected,
      &info)) &&
      info.present &&
      info.count == 3 &&
      view_equals(info.item_name, "arvane:stone");

  ArvDataDocumentHandle document = make_probe_document("owned item data");
  checks_ok = checks_ok && document != 0 &&
      result_ok(g_api->items->save_stack_namespace_data(
          g_api->items_ctx,
          selected,
          arv_cstr("example_plugin_pack"),
          document));
  checks_ok = checks_ok &&
      g_api->items->save_stack_namespace_data(
          g_api->items_ctx,
          selected,
          arv_cstr("other_pack"),
          document).code == ARV_RESULT_PERMISSION_DENIED;
  if (document != 0) {
    g_api->data->release_document(g_api->data_ctx, document);
  }

  ArvDataDocumentHandle namespace_document{};
  checks_ok = checks_ok && result_ok(g_api->items->load_stack_namespace_data(
      g_api->items_ctx,
      selected,
      arv_cstr("example_plugin_pack"),
      &namespace_document)) &&
      document_string_equals(namespace_document, "value", "owned item data");
  if (namespace_document != 0) {
    g_api->data->release_document(g_api->data_ctx, namespace_document);
  }

  ArvDataDocumentHandle full_document{};
  checks_ok = checks_ok && result_ok(g_api->items->load_stack_data(
      g_api->items_ctx,
      selected,
      &full_document)) &&
      document_string_equals(
          full_document,
          "example_plugin_pack.value",
          "owned item data");
  if (full_document != 0) {
    g_api->data->release_document(g_api->data_ctx, full_document);
  }

  return checks_ok;
}

void sync_job(void*) {
  const int sync_tick =
      g_sync_ticks.fetch_add(1, std::memory_order_relaxed) + 1;

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
  ArvPlayerSelection selection{};
  checks_ok = checks_ok &&
      g_api->players != nullptr &&
      result_ok(g_api->players->current_selection(
          g_api->players_ctx,
          player_id,
          &selection)) &&
      (selection.flags & ARV_PLAYER_SELECTION_HAS_WORLD) != 0U &&
      (selection.flags & ARV_PLAYER_SELECTION_HAS_A) != 0U &&
      (selection.flags & ARV_PLAYER_SELECTION_HAS_B) != 0U &&
      selection.world == 1 &&
      selection.a.x == 1 &&
      selection.a.y == 2 &&
      selection.a.z == 3 &&
      selection.b.x == 4 &&
      selection.b.y == 5 &&
      selection.b.z == 6;
  checks_ok = checks_ok && item_stack_data_checks(player_id);

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
  checks_ok = checks_ok && result_ok(g_api->regions->set_region_flag(
      g_api->regions_ctx,
      region_handle,
      arv_cstr("example_plugin_pack:pvp"),
      ARV_REGION_RULE_DENY));
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
  ArvRegionRuleValue pvp_flag = ARV_REGION_RULE_UNSET;
  checks_ok = checks_ok && result_ok(g_api->regions->region_flag(
      g_api->regions_ctx,
      arv_cstr("example_plugin_pack:runtime_spawn"),
      arv_cstr("example_plugin_pack:pvp"),
      &pvp_flag)) &&
      pvp_flag == ARV_REGION_RULE_DENY;
  pvp_flag = ARV_REGION_RULE_UNSET;
  checks_ok = checks_ok && result_ok(g_api->regions->query_flag(
      g_api->regions_ctx,
      &query,
      arv_cstr("example_plugin_pack:pvp"),
      &pvp_flag)) &&
      pvp_flag == ARV_REGION_RULE_DENY;
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
  if (sync_tick == 1) {
    ArvRegionDefinition persistent_region = region;
    persistent_region.id = arv_cstr("example_plugin_pack:persistent_runtime_spawn");
    persistent_region.persistent = true;
    ArvRegionHandle persistent_handle{};
    checks_ok = checks_ok && result_ok(g_api->regions->create_runtime_region(
        g_api->regions_ctx,
        &persistent_region,
        &persistent_handle));
  }

  ArvPersistentScope server_scope{};
  server_scope.kind = ARV_PERSISTENT_SCOPE_SERVER;
  checks_ok = checks_ok && save_probe_document(
      server_scope,
      "example_plugin_pack:server/probe",
      "server document");
  checks_ok = checks_ok && load_probe_document_equals(
      server_scope,
      "example_plugin_pack:server/probe",
      "server document");

  ArvPersistentScope world_scope{};
  world_scope.kind = ARV_PERSISTENT_SCOPE_WORLD;
  world_scope.world = 1;
  checks_ok = checks_ok && save_probe_document(
      world_scope,
      "example_plugin_pack:world/probe",
      "world document v1");
  checks_ok = checks_ok && save_probe_document(
      world_scope,
      "example_plugin_pack:world/probe",
      "world document v2");
  checks_ok = checks_ok && load_probe_document_equals(
      world_scope,
      "example_plugin_pack:world/probe",
      "world document v2");
  checks_ok = checks_ok && g_api->persistent_data->exists(
      g_api->persistent_data_ctx,
      &world_scope,
      arv_cstr("example_plugin_pack:world/probe"));
  checks_ok = checks_ok && result_ok(g_api->persistent_data->remove(
      g_api->persistent_data_ctx,
      &world_scope,
      arv_cstr("example_plugin_pack:world/probe")));
  ArvDataDocumentHandle missing_document{};
  checks_ok = checks_ok && g_api->persistent_data->load(
      g_api->persistent_data_ctx,
      &world_scope,
      arv_cstr("example_plugin_pack:world/probe"),
      &missing_document).code == ARV_RESULT_NOT_FOUND;
  ArvDataDocumentHandle wrong_namespace = make_probe_document("wrong namespace");
  checks_ok = checks_ok && wrong_namespace != 0 &&
      g_api->persistent_data->save(
          g_api->persistent_data_ctx,
          &world_scope,
          arv_cstr("other_pack:world/probe"),
          wrong_namespace).code == ARV_RESULT_PERMISSION_DENIED;
  if (wrong_namespace != 0) {
    g_api->data->release_document(g_api->data_ctx, wrong_namespace);
  }

  ArvPersistentScope player_scope{};
  player_scope.kind = ARV_PERSISTENT_SCOPE_PLAYER;
  player_scope.player = player_id;
  checks_ok = checks_ok && save_probe_document(
      player_scope,
      "example_plugin_pack:player/probe",
      "player document");
  checks_ok = checks_ok && load_probe_document_equals(
      player_scope,
      "example_plugin_pack:player/probe",
      "player document");

  ArvBlockPos block_pos{1, 2, 3};
  ArvPersistentScope block_scope{};
  block_scope.kind = ARV_PERSISTENT_SCOPE_BLOCK;
  block_scope.world = 1;
  block_scope.block_pos = block_pos;
  checks_ok = checks_ok && save_probe_document(
      block_scope,
      "example_plugin_pack:block/probe",
      "block document");
  checks_ok = checks_ok && load_probe_document_equals(
      block_scope,
      "example_plugin_pack:block/probe",
      "block document");
  checks_ok = checks_ok && result_ok(g_api->persistent_data->remove(
      g_api->persistent_data_ctx,
      &block_scope,
      arv_cstr("example_plugin_pack:block/probe")));

  ArvPersistentScope chunk_scope{};
  chunk_scope.kind = ARV_PERSISTENT_SCOPE_CHUNK;
  chunk_scope.world = 1;
  chunk_scope.chunk_pos = {2, -3};
  checks_ok = checks_ok && save_probe_document(
      chunk_scope,
      "example_plugin_pack:chunk/probe",
      "chunk document");
  checks_ok = checks_ok && load_probe_document_equals(
      chunk_scope,
      "example_plugin_pack:chunk/probe",
      "chunk document");

  ArvEntityHandle entity{};
  checks_ok = checks_ok && result_ok(g_api->entities->spawn(
      g_api->entities_ctx,
      1,
      arv_cstr("example_plugin_pack:persistent_probe"),
      {8.0F, 20.0F, -20.0F},
      &entity));
  checks_ok = checks_ok && result_ok(g_api->entities->add_tag(
      g_api->entities_ctx,
      entity,
      arv_cstr("example_plugin_pack:tagged")));
  bool has_tag = false;
  checks_ok = checks_ok && result_ok(g_api->entities->has_tag(
      g_api->entities_ctx,
      entity,
      arv_cstr("example_plugin_pack:tagged"),
      &has_tag)) &&
      has_tag;
  checks_ok = checks_ok && result_ok(g_api->entities->remove_tag(
      g_api->entities_ctx,
      entity,
      arv_cstr("example_plugin_pack:tagged")));
  checks_ok = checks_ok && result_ok(g_api->entities->has_tag(
      g_api->entities_ctx,
      entity,
      arv_cstr("example_plugin_pack:tagged"),
      &has_tag)) &&
      !has_tag;
  ArvPersistentScope entity_scope{};
  entity_scope.kind = ARV_PERSISTENT_SCOPE_ENTITY;
  entity_scope.entity = entity;
  checks_ok = checks_ok && save_probe_document(
      entity_scope,
      "example_plugin_pack:entity/probe",
      "entity document");
  checks_ok = checks_ok && load_probe_document_equals(
      entity_scope,
      "example_plugin_pack:entity/probe",
      "entity document");
  checks_ok = checks_ok && result_ok(g_api->persistent_data->remove(
      g_api->persistent_data_ctx,
      &entity_scope,
      arv_cstr("example_plugin_pack:entity/probe")));

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

  checks_ok = checks_ok &&
      g_api->variables != nullptr &&
      result_ok(g_api->variables->set_global_string(
          g_api->variables_ctx,
          arv_cstr("example_plugin_pack:greeting"),
          arv_cstr("Hello"))) &&
      result_ok(g_api->variables->set_player_string(
          g_api->variables_ctx,
          player_id,
          arv_cstr("example_plugin_pack:target"),
          arv_cstr("Builder"))) &&
      result_ok(g_api->variables->set_player_string_bound(
          g_api->variables_ctx,
          player_id,
          arv_cstr("example_plugin_pack:live_target"),
          arv_cstr("Builder"),
          ARV_VARIABLE_BINDING_LIVE));
  ArvDataDocumentHandle chat_document =
      g_api->data->create_document(g_api->data_ctx, 1);
  checks_ok = checks_ok && chat_document != 0 &&
      result_ok(g_api->data->set_string(
          g_api->data_ctx,
          chat_document,
          arv_cstr("message"),
          arv_cstr("<green>{example_plugin_pack:greeting} {example_plugin_pack:target}")));
  ArvChatMessageHandle chat_message{};
  ArvByteBuffer chat_plain{};
  ArvTextHandle chat_text{};
  checks_ok = checks_ok &&
      g_api->chat_message != nullptr &&
      result_ok(g_api->chat_message->from_data(
          g_api->chat_message_ctx,
          chat_document,
          arv_cstr("message"),
          &chat_message)) &&
      result_ok(g_api->chat_message->to_string_for(
          g_api->chat_message_ctx,
          chat_message,
          player_id,
          &chat_plain)) &&
      buffer_equals(chat_plain, "Hello Builder") &&
      result_ok(g_api->chat_message->as_text_for(
          g_api->chat_message_ctx,
          chat_message,
          player_id,
          &chat_text)) &&
      result_ok(g_api->chat_message->send_to(
          g_api->chat_message_ctx,
          chat_message,
          player_id,
          arv_cstr("example_plugin_pack:alerts"),
          ARV_CHAT_MESSAGE_PRIVATE)) &&
      result_ok(g_api->variables->clear_player(
          g_api->variables_ctx,
          player_id,
          arv_cstr("example_plugin_pack:target")));
  if (chat_plain.data != nullptr) {
    g_api->resources->release_buffer(g_api->resources_ctx, chat_plain);
  }
  if (chat_text.value != 0U) {
    g_api->text->release(g_api->text_ctx, chat_text);
  }
  if (chat_message.value != 0U) {
    g_api->chat_message->release(g_api->chat_message_ctx, chat_message);
  }
  if (chat_document != 0) {
    g_api->data->release_document(g_api->data_ctx, chat_document);
  }
  if (checks_ok) {
    g_chat_sends.fetch_add(1, std::memory_order_relaxed);
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

void sync_millisecond_job(void*) {
  g_sync_ms_ticks.fetch_add(1, std::memory_order_relaxed);
}

void async_millisecond_job(void*) {
  g_async_ms_ticks.fetch_add(1, std::memory_order_relaxed);
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
  std::snprintf(
      message + std::strlen(message),
      sizeof(message) - std::strlen(message),
      " chat_sends=%d",
      g_chat_sends.load(std::memory_order_relaxed));
  std::snprintf(
      message + std::strlen(message),
      sizeof(message) - std::strlen(message),
      " sync_ms=%d async_ms=%d",
      g_sync_ms_ticks.load(std::memory_order_relaxed),
      g_async_ms_ticks.load(std::memory_order_relaxed));
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

bool document_string_equals(
    ArvDataDocumentHandle document,
    std::string_view path,
    std::string_view expected) {
  ArvStringView value{};
  return result_ok(g_api->data->get_string(
             g_api->data_ctx,
             document,
             arv_string_view(path.data(), path.size()),
             &value)) &&
         view_equals(value, expected);
}

} // namespace

extern "C" ARV_EXPORT bool arvane_plugin_main(const ArvAPI* api) {
  if (api == nullptr || api->api_version != ARV_API_VERSION) {
    return false;
  }
  g_sync_ticks.store(0, std::memory_order_relaxed);
  g_async_ticks.store(0, std::memory_order_relaxed);
  g_sync_ms_ticks.store(0, std::memory_order_relaxed);
  g_async_ms_ticks.store(0, std::memory_order_relaxed);
  g_thread_violations.store(0, std::memory_order_relaxed);
  g_server_starts.store(0, std::memory_order_relaxed);
  g_server_stops.store(0, std::memory_order_relaxed);
  g_api_checks.store(0, std::memory_order_relaxed);
  g_network_sends.store(0, std::memory_order_relaxed);
  g_chat_sends.store(0, std::memory_order_relaxed);
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

  ArvDataDocumentHandle config_document =
      api->data->create_document(api->data_ctx, 1);
  if (config_document == 0) {
    return false;
  }
  if (!result_ok(api->data->set_bool(
          api->data_ctx,
          config_document,
          arv_cstr("enabled"),
          true))) {
    api->data->release_document(api->data_ctx, config_document);
    return false;
  }
  ArvConfigScope global_config{ARV_CONFIG_SCOPE_GLOBAL, 0};
  if (!result_ok(api->config->save(
          api->config_ctx,
          &global_config,
          arv_cstr("example_plugin_pack:settings"),
          config_document))) {
    api->data->release_document(api->data_ctx, config_document);
    return false;
  }
  api->data->release_document(api->data_ctx, config_document);
  ArvDataDocumentHandle loaded_config{};
  bool enabled = false;
  if (!result_ok(api->config->load(
          api->config_ctx,
          &global_config,
          arv_cstr("example_plugin_pack:settings"),
          &loaded_config)) ||
      !result_ok(api->data->get_bool(
          api->data_ctx,
          loaded_config,
          arv_cstr("enabled"),
          &enabled)) ||
      !enabled) {
    if (loaded_config != 0) {
      api->data->release_document(api->data_ctx, loaded_config);
    }
    return false;
  }
  api->data->release_document(api->data_ctx, loaded_config);

  ArvTextHandle hello{};
  ArvTextHandle world{};
  ArvTextHandle red_hello{};
  ArvTextHandle combined{};
  ArvTextHandle clickable{};
  ArvTextHandle translated{};
  if (!result_ok(api->text->literal(api->text_ctx, arv_cstr("Hello"), &hello))) {
    return false;
  }
  if (!result_ok(api->text->translatable(
          api->text_ctx,
          arv_cstr("example_plugin_pack.text.hello"),
          &translated))) {
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
  ArvByteBuffer translated_plain{};
  if (!result_ok(api->text->plain_text(api->text_ctx, translated, &translated_plain))) {
    return false;
  }
  const bool translated_matches =
      buffer_equals(translated_plain, "example_plugin_pack.text.hello");
  api->resources->release_buffer(api->resources_ctx, translated_plain);
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
  api->text->release(api->text_ctx, translated);
  api->text->release(api->text_ctx, hello);
  if (!plain_matches || !translated_matches || !serialized_nonempty) {
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
  ArvGameModeDefinition mode{};
  mode.id = arv_cstr("example_plugin_pack:builder");
  mode.base_id = arv_cstr("survival");
  mode.custom_permissions = &group_permission;
  mode.custom_permission_count = 1;
  if (!result_ok(api->gamemodes->register_mode(api->gamemodes_ctx, &mode))) {
    return false;
  }

  ArvRegionFlagDefinition flag{};
  flag.id = arv_cstr("example_plugin_pack:pvp");
  flag.description = arv_cstr("Player combat enabled");
  flag.default_value = ARV_REGION_RULE_ALLOW;
  if (!result_ok(api->regions->register_region_flag(api->regions_ctx, &flag))) {
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
  if (!result_ok(api->registries->register_entity_behavior(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:entity_behavior")))) {
    return false;
  }
  if (!result_ok(api->registries->register_component_codec(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:component_codec")))) {
    return false;
  }
  if (!result_ok(api->registries->register_loot_function(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:loot_function")))) {
    return false;
  }
  if (!result_ok(api->registries->register_recipe_type(
          api->registries_ctx,
          arv_cstr("example_plugin_pack:recipe_type")))) {
    return false;
  }
  if (!result_ok(api->worldgen->register_feature_type(
          api->worldgen_ctx,
          arv_cstr("example_plugin_pack:feature")))) {
    return false;
  }
  if (!result_ok(api->worldgen->register_noise_type(
          api->worldgen_ctx,
          arv_cstr("example_plugin_pack:noise")))) {
    return false;
  }
  if (!result_ok(api->worldgen->register_density_function_type(
          api->worldgen_ctx,
          arv_cstr("example_plugin_pack:density")))) {
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
  if (!result_ok(api->scheduler->sync_repeating_tick(
          api->scheduler_ctx,
          1,
          1,
          &sync_job,
          nullptr,
          &sync_handle))) {
    return false;
  }
  ArvScheduledJobHandle async_handle{};
  if (!result_ok(api->scheduler->async_delayed_tick(
          api->scheduler_ctx,
          1,
          &async_job,
          nullptr,
          &async_handle))) {
    return false;
  }
  ArvScheduledJobHandle sync_ms_handle{};
  if (!result_ok(api->scheduler->sync_delayed(
          api->scheduler_ctx,
          0,
          &sync_millisecond_job,
          nullptr,
          &sync_ms_handle))) {
    return false;
  }
  ArvScheduledJobHandle async_ms_handle{};
  if (!result_ok(api->scheduler->async_delayed(
          api->scheduler_ctx,
          0,
          &async_millisecond_job,
          nullptr,
          &async_ms_handle))) {
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
