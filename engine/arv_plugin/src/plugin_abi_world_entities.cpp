#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_world_get_block(
    void* context,
    ArvWorldHandle world_handle,
    ArvBlockPos pos,
    uint32_t* out_block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "world.get_block");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_block_variant == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_block_variant is null");
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  *out_block_variant = world->get_block({pos.x, pos.y, pos.z}).value;
  return ok(ctx);
}

ArvResult api_world_set_block(
    void* context,
    ArvWorldHandle world_handle,
    ArvBlockPos pos,
    uint32_t block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "world.set_block");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  if (ctx->impl->runtime.block_registry != nullptr &&
      ctx->impl->runtime.block_registry->variant(voxel::BlockVariantID{block_variant}) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "block variant not found");
  }
  if (!world->set_block({pos.x, pos.y, pos.z}, voxel::BlockVariantID{block_variant})) {
    return make_result(ctx, ARV_RESULT_INVALID_STATE, "block write failed");
  }
  return ok(ctx);
}

ArvResult api_world_current_tick(
    void* context,
    ArvWorldHandle world_handle,
    uint64_t* out_tick) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_tick == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_tick is null");
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  *out_tick = world->tick_index();
  return ok(ctx);
}

ArvResult api_world_seed(
    void* context,
    ArvWorldHandle world_handle,
    uint64_t* out_seed) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_seed == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_seed is null");
  }
  if (world_from_handle(ctx, world_handle) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  *out_seed = ctx->impl->runtime.world_seed;
  return ok(ctx);
}

ArvResult api_entity_spawn(
    void* context,
    ArvWorldHandle world_handle,
    ArvResourceID entity_type,
    ArvVec3 position,
    ArvEntityHandle* out_entity) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.spawn");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_entity == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_entity is null");
  }
  *out_entity = 0U;
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  if (ctx->impl->runtime.entity_registry == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "entity registry is unavailable");
  }
  const std::optional<EntityTypeID> type_id =
      ctx->impl->runtime.entity_registry->find_type(abi_view(entity_type));
  if (!type_id.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity type not found");
  }
  world::EntityCreateInfo info;
  info.type_id = type_id.value();
  info.type_name = abi_string(entity_type);
  info.position = {position.x, position.y, position.z};
  if (const entity::EntityTypeRecord* type =
          ctx->impl->runtime.entity_registry->type(type_id.value());
      type != nullptr) {
    info.persistent = type->persistent_by_default;
    info.replicated = type->replicated_by_default;
    info.authored_components = type->components;
  }
  const EntityID entity = world->create_entity(std::move(info));
  *out_entity = entity.value;
  return ok(ctx);
}

ArvResult api_entity_destroy(void* context, ArvEntityHandle entity) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.destroy");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->destroy_entity(EntityID{entity})) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

bool api_entity_exists(void* context, ArvEntityHandle entity) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr && ctx->impl->runtime.default_world != nullptr &&
         tls_thread_context == PluginThreadContext::Sync &&
         ctx->impl->runtime.default_world->entity_exists(EntityID{entity});
}

ArvResult api_entity_world(
    void* context,
    ArvEntityHandle entity,
    ArvWorldHandle* out_world) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.world");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_world == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_world is null");
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->entity_exists(EntityID{entity})) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  *out_world = world->id().value;
  return ok(ctx);
}

ArvResult api_entity_position(
    void* context,
    ArvEntityHandle entity,
    ArvVec3* out_position) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.position");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_position == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_position is null");
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  const entity::EntityTransform* transform =
      world->find_entity_transform(EntityID{entity});
  if (transform == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity transform not found");
  }
  *out_position = {
      transform->position.x,
      transform->position.y,
      transform->position.z};
  return ok(ctx);
}

ArvResult api_entity_set_position(
    void* context,
    ArvEntityHandle entity,
    ArvVec3 position) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.set_position");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr ||
      !world->set_entity_position(EntityID{entity}, {position.x, position.y, position.z})) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

ArvResult api_entity_add_tag(
    void* context,
    ArvEntityHandle entity,
    ArvResourceID tag) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.add_tag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string tag_id{abi_view(tag)};
  if (auto error = validate_owned_resource_id(ctx->info(), tag_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->add_entity_tag(EntityID{entity}, tag_id)) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

ArvResult api_entity_remove_tag(
    void* context,
    ArvEntityHandle entity,
    ArvResourceID tag) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.remove_tag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string tag_id{abi_view(tag)};
  if (auto error = validate_owned_resource_id(ctx->info(), tag_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->remove_entity_tag(EntityID{entity}, tag_id)) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

ArvResult api_entity_has_tag(
    void* context,
    ArvEntityHandle entity,
    ArvResourceID tag,
    bool* out_has_tag) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "entities.has_tag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_has_tag == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_has_tag is null");
  }
  *out_has_tag = false;
  const std::string tag_id{abi_view(tag)};
  if (auto error = validate_owned_resource_id(ctx->info(), tag_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->entity_exists(EntityID{entity})) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  *out_has_tag = world->entity_has_tag(EntityID{entity}, tag_id);
  return ok(ctx);
}

ArvDataDocumentHandle api_data_create_document(void* context, uint32_t schema_version) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  return store_document(ctx, data::DataDocument{schema_version, data::DataValue::object()});
}

} // namespace

void fill_world_entity_api(PluginAPIStorage& storage) {
  storage.worlds_api = {
      &api_world_get_block,
      &api_world_set_block,
      &api_world_current_tick,
      &api_world_seed,
  };
  storage.entities_api = {
      &api_entity_spawn,
      &api_entity_destroy,
      &api_entity_exists,
      &api_entity_world,
      &api_entity_position,
      &api_entity_set_position,
      &api_entity_add_tag,
      &api_entity_remove_tag,
      &api_entity_has_tag,
  };
}

} // namespace arv::plugin
