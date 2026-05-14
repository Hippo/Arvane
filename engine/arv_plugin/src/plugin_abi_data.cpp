#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

void api_data_release_document(void* context, ArvDataDocumentHandle document) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || document == 0U) {
    return;
  }
  ctx->data_documents.erase(document);
}

uint32_t api_data_schema_version(void* context, ArvDataDocumentHandle document) {
  const PluginCallContext* ctx = call_context(context);
  const data::DataDocument* stored = document_from_handle(ctx, document);
  return stored == nullptr ? 0U : stored->schema_version;
}

ArvDataKind api_data_kind(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  const PluginCallContext* ctx = call_context(context);
  const data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return ARV_DATA_INVALID;
  }
  const data::DataValue* value = data::find_path(*stored, abi_view(path));
  return value == nullptr ? ARV_DATA_INVALID : data_kind_to_abi(value->kind());
}

ArvResult set_data_value(
    PluginCallContext* ctx,
    ArvDataDocumentHandle document,
    ArvStringView path,
    data::DataValue value) {
  data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data document handle is invalid");
  }
  auto result = data::set_path(*stored, abi_view(path), std::move(value));
  if (!result) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, result.error().describe());
  }
  return ok(ctx);
}

ArvResult api_data_set_null(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  return set_data_value(call_context(context), document, path, nullptr);
}

ArvResult api_data_set_bool(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    bool value) {
  return set_data_value(call_context(context), document, path, value);
}

ArvResult api_data_set_i64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    int64_t value) {
  return set_data_value(call_context(context), document, path, static_cast<i64>(value));
}

ArvResult api_data_set_u64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    uint64_t value) {
  return set_data_value(call_context(context), document, path, static_cast<u64>(value));
}

ArvResult api_data_set_f64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    double value) {
  return set_data_value(call_context(context), document, path, static_cast<f64>(value));
}

ArvResult api_data_set_string(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvStringView value) {
  return set_data_value(call_context(context), document, path, abi_string(value));
}

ArvResult api_data_set_bytes(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvBytesView value) {
  PluginCallContext* ctx = call_context(context);
  if (value.data == nullptr && value.size != 0U) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "bytes view is null");
  }
  const std::span<const u8> bytes{value.data, value.size};
  return set_data_value(ctx, document, path, data::DataValue::bytes(bytes));
}

ArvResult api_data_set_array(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  return set_data_value(call_context(context), document, path, data::DataValue::array());
}

ArvResult api_data_set_object(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  return set_data_value(call_context(context), document, path, data::DataValue::object());
}

ArvResult push_data_array_value(
    PluginCallContext* ctx,
    ArvDataDocumentHandle document,
    ArvStringView path,
    data::DataValue value) {
  data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data document handle is invalid");
  }
  data::DataValue* array = data::find_path(*stored, abi_view(path));
  if (array == nullptr) {
    auto created = data::set_path(*stored, abi_view(path), data::DataValue::array());
    if (!created) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, created.error().describe());
    }
    array = data::find_path(*stored, abi_view(path));
  }
  if (array == nullptr || array->kind() != data::DataKind::Array) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data path is not an array");
  }
  array->push(std::move(value));
  return ok(ctx);
}

ArvResult api_data_array_push_null(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  return push_data_array_value(call_context(context), document, path, nullptr);
}

ArvResult api_data_array_push_bool(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    bool value) {
  return push_data_array_value(call_context(context), document, path, value);
}

ArvResult api_data_array_push_i64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    int64_t value) {
  return push_data_array_value(call_context(context), document, path, static_cast<i64>(value));
}

ArvResult api_data_array_push_u64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    uint64_t value) {
  return push_data_array_value(call_context(context), document, path, static_cast<u64>(value));
}

ArvResult api_data_array_push_f64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    double value) {
  return push_data_array_value(call_context(context), document, path, static_cast<f64>(value));
}

ArvResult api_data_array_push_string(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvStringView value) {
  return push_data_array_value(call_context(context), document, path, abi_string(value));
}

size_t api_data_size(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path) {
  const PluginCallContext* ctx = call_context(context);
  const data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return 0U;
  }
  const data::DataValue* value = data::find_path(*stored, abi_view(path));
  return value == nullptr ? 0U : value->size();
}

[[nodiscard]] const data::DataValue* require_data_value(
    PluginCallContext* ctx,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvResult& out_error) {
  const data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    out_error = make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data document handle is invalid");
    return nullptr;
  }
  data::DataDiagnostic diagnostic;
  const data::DataValue* value = data::find_path(*stored, abi_view(path), &diagnostic);
  if (value == nullptr) {
    out_error = make_result(ctx, ARV_RESULT_NOT_FOUND, diagnostic.describe());
  }
  return value;
}

ArvResult api_data_get_bool(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    bool* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = false;
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  if (value->kind() != data::DataKind::Bool) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not a bool");
  }
  *out = value->as_bool_or(false);
  return ok(ctx);
}

ArvResult api_data_get_i64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    int64_t* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = 0;
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  if (value->kind() != data::DataKind::I64 && value->kind() != data::DataKind::U64) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not an integer");
  }
  *out = value->as_i64_or(0);
  return ok(ctx);
}

ArvResult api_data_get_u64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    uint64_t* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = 0;
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  if (value->kind() != data::DataKind::I64 && value->kind() != data::DataKind::U64) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not an integer");
  }
  *out = value->as_u64_or(0);
  return ok(ctx);
}

ArvResult api_data_get_f64(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    double* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = 0.0;
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  if (value->kind() != data::DataKind::I64 && value->kind() != data::DataKind::U64 &&
      value->kind() != data::DataKind::F64) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not numeric");
  }
  *out = value->as_f64_or(0.0);
  return ok(ctx);
}

ArvResult api_data_get_string(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvStringView* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = {};
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  if (value->kind() != data::DataKind::String) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not a string");
  }
  const std::string_view text = value->as_string_or({});
  *out = {text.data(), text.size()};
  return ok(ctx);
}

ArvResult api_data_get_bytes(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvBytesView* out) {
  PluginCallContext* ctx = call_context(context);
  if (out == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out is null");
  }
  *out = {};
  ArvResult error{};
  const data::DataValue* value = require_data_value(ctx, document, path, error);
  if (value == nullptr) {
    return error;
  }
  const auto* bytes = value->bytes_if();
  if (bytes == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data value is not bytes");
  }
  *out = {bytes->data(), bytes->size()};
  return ok(ctx);
}

[[nodiscard]] data::ConfigStorageService config_storage_service(PluginCallContext* ctx) {
  return data::ConfigStorageService{
      ctx == nullptr ? std::filesystem::path{} : ctx->impl->options.config_root,
      ctx == nullptr ? std::filesystem::path{} : ctx->impl->runtime.server_save_root};
}

[[nodiscard]] ArvResult validate_config_scope(
    PluginCallContext* ctx,
    const ArvConfigScope* scope,
    ArvConfigScope& out_scope) {
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (scope == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "config scope is null");
  }
  out_scope = *scope;
  switch (scope->kind) {
    case ARV_CONFIG_SCOPE_GLOBAL:
      return ok(ctx);
    case ARV_CONFIG_SCOPE_WORLD:
      if (ctx->impl->runtime.server_save_root.empty()) {
        return make_result(ctx, ARV_RESULT_INVALID_STATE, "world config root is unavailable");
      }
      if (world_from_handle(ctx, scope->world) == nullptr) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
      }
      return ok(ctx);
  }
  return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "unknown config scope kind");
}

ArvResult api_config_load(
    void* context,
    const ArvConfigScope* scope,
    ArvResourceID key,
    ArvDataDocumentHandle* out_document) {
  PluginCallContext* ctx = call_context(context);
  if (out_document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_document is null");
  }
  *out_document = 0U;
  ArvConfigScope validated_scope{};
  if (ArvResult scope_result = validate_config_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "config", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  data::ConfigStorageService storage = config_storage_service(ctx);
  data::StorageLoadResult loaded =
      validated_scope.kind == ARV_CONFIG_SCOPE_GLOBAL
          ? storage.load_global_config(ctx->info().pack_id, parsed.value())
          : storage.load_world_config(
                WorldID{static_cast<u32>(validated_scope.world)},
                ctx->info().pack_id,
                parsed.value());
  if (!loaded) {
    return make_result(ctx, storage_result_code(loaded.error().code), loaded.error().describe());
  }
  *out_document = store_document(ctx, std::move(loaded.value()));
  return ok(ctx);
}

ArvResult api_config_save(
    void* context,
    const ArvConfigScope* scope,
    ArvResourceID key,
    ArvDataDocumentHandle document) {
  PluginCallContext* ctx = call_context(context);
  ArvConfigScope validated_scope{};
  if (ArvResult scope_result = validate_config_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  const data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data document handle is invalid");
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "config", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  data::ConfigStorageService storage = config_storage_service(ctx);
  data::StorageSaveResult saved =
      validated_scope.kind == ARV_CONFIG_SCOPE_GLOBAL
          ? storage.save_global_config(ctx->info().pack_id, parsed.value(), *stored)
          : storage.save_world_config(
                WorldID{static_cast<u32>(validated_scope.world)},
                ctx->info().pack_id,
                parsed.value(),
                *stored);
  if (!saved) {
    return make_result(ctx, storage_result_code(saved.error().code), saved.error().describe());
  }
  return ok(ctx);
}

bool api_config_exists(
    void* context,
    const ArvConfigScope* scope,
    ArvResourceID key) {
  PluginCallContext* ctx = call_context(context);
  ArvConfigScope validated_scope{};
  if (validate_config_scope(ctx, scope, validated_scope).code != ARV_RESULT_OK) {
    return false;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "config", key_code, key_error);
  if (!parsed.has_value()) {
    return false;
  }
  data::ConfigStorageService storage = config_storage_service(ctx);
  return validated_scope.kind == ARV_CONFIG_SCOPE_GLOBAL
             ? storage.exists_global_config(ctx->info().pack_id, parsed.value())
             : storage.exists_world_config(
                   WorldID{static_cast<u32>(validated_scope.world)},
                   ctx->info().pack_id,
                   parsed.value());
}

ArvResult api_config_remove(
    void* context,
    const ArvConfigScope* scope,
    ArvResourceID key) {
  PluginCallContext* ctx = call_context(context);
  ArvConfigScope validated_scope{};
  if (ArvResult scope_result = validate_config_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "config", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  data::ConfigStorageService storage = config_storage_service(ctx);
  data::StorageRemoveResult removed =
      validated_scope.kind == ARV_CONFIG_SCOPE_GLOBAL
          ? storage.remove_global_config(ctx->info().pack_id, parsed.value())
          : storage.remove_world_config(
                WorldID{static_cast<u32>(validated_scope.world)},
                ctx->info().pack_id,
                parsed.value());
  if (!removed) {
    return make_result(ctx, storage_result_code(removed.error().code), removed.error().describe());
  }
  return ok(ctx);
}

ArvResult require_persistent_entity(
    PluginCallContext* ctx,
    ArvEntityHandle entity,
    std::optional<std::string>& out_uuid) {
  if (ctx == nullptr || ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  if (!ctx->impl->runtime.default_world->entity_exists(EntityID{entity})) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "entity not found");
  }
  out_uuid =
      ctx->impl->runtime.default_world->persistent_entity_uuid(EntityID{entity});
  if (!out_uuid.has_value()) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "entity has no persistent UUID");
  }
  return ok(ctx);
}

[[nodiscard]] data::PersistentDataStorageService persistent_storage_service(
    PluginCallContext* ctx) {
  return data::PersistentDataStorageService{
      ctx == nullptr ? std::filesystem::path{} : ctx->impl->runtime.server_save_root};
}

[[nodiscard]] BlockPos block_pos_from_abi(ArvBlockPos pos) noexcept {
  return {{pos.x, pos.y, pos.z}};
}

[[nodiscard]] ChunkCoord chunk_pos_from_abi(ArvChunkPos pos) noexcept {
  return {pos.x, pos.z};
}

[[nodiscard]] ArvResult validate_persistent_scope(
    PluginCallContext* ctx,
    const ArvPersistentScope* scope,
    data::PersistentScope& out_scope) {
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (scope == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "persistent scope is null");
  }
  if (ctx->impl->runtime.server_save_root.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_STATE, "persistent data root is unavailable");
  }

  switch (scope->kind) {
    case ARV_PERSISTENT_SCOPE_SERVER:
      out_scope = {.kind = data::PersistentScopeKind::Server};
      return ok(ctx);
    case ARV_PERSISTENT_SCOPE_WORLD:
      if (world_from_handle(ctx, scope->world) == nullptr) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
      }
      out_scope = {
          .kind = data::PersistentScopeKind::World,
          .world = WorldID{static_cast<u32>(scope->world)}};
      return ok(ctx);
    case ARV_PERSISTENT_SCOPE_PLAYER:
      if (player_from_id(ctx, scope->player) == nullptr) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
      }
      if (!ctx->impl->runtime.player_stable_id) {
        return make_result(
            ctx,
            ARV_RESULT_UNSUPPORTED,
            "player stable identity resolver is unavailable");
      }
      if (std::optional<std::string> stable_id =
              ctx->impl->runtime.player_stable_id(PlayerID{scope->player});
          stable_id.has_value() && !stable_id->empty()) {
        out_scope = {
            .kind = data::PersistentScopeKind::Player,
            .player = PlayerID{scope->player},
            .player_stable_id = stable_id.value()};
        return ok(ctx);
      }
      out_scope = {
          .kind = data::PersistentScopeKind::Player,
          .player = PlayerID{scope->player}};
      return make_result(ctx, ARV_RESULT_INVALID_STATE, "player has no stable identity");
    case ARV_PERSISTENT_SCOPE_ENTITY: {
      std::optional<std::string> uuid;
      if (ArvResult persistent = require_persistent_entity(ctx, scope->entity, uuid);
          persistent.code != ARV_RESULT_OK) {
        return persistent;
      }
      out_scope = {
          .kind = data::PersistentScopeKind::Entity,
          .entity_uuid = uuid.value()};
      return ok(ctx);
    }
    case ARV_PERSISTENT_SCOPE_BLOCK:
      if (world_from_handle(ctx, scope->world) == nullptr) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
      }
      out_scope = {
          .kind = data::PersistentScopeKind::Block,
          .world = WorldID{static_cast<u32>(scope->world)},
          .block_pos = block_pos_from_abi(scope->block_pos)};
      return ok(ctx);
    case ARV_PERSISTENT_SCOPE_CHUNK:
      if (world_from_handle(ctx, scope->world) == nullptr) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
      }
      out_scope = {
          .kind = data::PersistentScopeKind::Chunk,
          .world = WorldID{static_cast<u32>(scope->world)},
          .chunk_pos = chunk_pos_from_abi(scope->chunk_pos)};
      return ok(ctx);
  }
  return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "unknown persistent scope kind");
}

ArvResult api_persistent_load(
    void* context,
    const ArvPersistentScope* scope,
    ArvResourceID key,
    ArvDataDocumentHandle* out_document) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "persistent_data.load");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_document is null");
  }
  *out_document = 0U;
  data::PersistentScope validated_scope{};
  if (ArvResult scope_result = validate_persistent_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "persistent data", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  auto loaded = persistent_storage_service(ctx).load(
      validated_scope,
      ctx->info().pack_id,
      parsed.value());
  if (!loaded) {
    return make_result(ctx, storage_result_code(loaded.error().code), loaded.error().describe());
  }
  *out_document = store_document(ctx, std::move(loaded.value()));
  return ok(ctx);
}

ArvResult api_persistent_save(
    void* context,
    const ArvPersistentScope* scope,
    ArvResourceID key,
    ArvDataDocumentHandle document) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "persistent_data.save");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const data::DataDocument* stored = document_from_handle(ctx, document);
  if (stored == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "data document handle is invalid");
  }
  data::PersistentScope validated_scope{};
  if (ArvResult scope_result = validate_persistent_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "persistent data", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  auto saved = persistent_storage_service(ctx).save(
      validated_scope,
      ctx->info().pack_id,
      parsed.value(),
      *stored);
  if (!saved) {
    return make_result(ctx, storage_result_code(saved.error().code), saved.error().describe());
  }
  return ok(ctx);
}

bool api_persistent_exists(
    void* context,
    const ArvPersistentScope* scope,
    ArvResourceID key) {
  PluginCallContext* ctx = call_context(context);
  if (require_sync_context(ctx, "persistent_data.exists").code != ARV_RESULT_OK) {
    return false;
  }
  data::PersistentScope validated_scope{};
  if (validate_persistent_scope(ctx, scope, validated_scope).code != ARV_RESULT_OK) {
    return false;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "persistent data", key_code, key_error);
  if (!parsed.has_value()) {
    return false;
  }
  return persistent_storage_service(ctx).exists(
      validated_scope,
      ctx->info().pack_id,
      parsed.value());
}

ArvResult api_persistent_remove(
    void* context,
    const ArvPersistentScope* scope,
    ArvResourceID key) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "persistent_data.remove");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  data::PersistentScope validated_scope{};
  if (ArvResult scope_result = validate_persistent_scope(ctx, scope, validated_scope);
      scope_result.code != ARV_RESULT_OK) {
    return scope_result;
  }
  ArvResultCode key_code{};
  std::string key_error;
  auto parsed = parse_owned_plugin_key(ctx, key, "persistent data", key_code, key_error);
  if (!parsed.has_value()) {
    return make_result(ctx, key_code, key_error);
  }
  auto removed = persistent_storage_service(ctx).remove(
      validated_scope,
      ctx->info().pack_id,
      parsed.value());
  if (!removed) {
    return make_result(ctx, storage_result_code(removed.error().code), removed.error().describe());
  }
  return ok(ctx);
}

} // namespace

void fill_data_api(PluginAPIStorage& storage) {
  storage.data_api = {
      &api_data_create_document,
      &api_data_release_document,
      &api_data_schema_version,
      &api_data_kind,
      &api_data_set_null,
      &api_data_set_bool,
      &api_data_set_i64,
      &api_data_set_u64,
      &api_data_set_f64,
      &api_data_set_string,
      &api_data_set_bytes,
      &api_data_set_array,
      &api_data_set_object,
      &api_data_array_push_null,
      &api_data_array_push_bool,
      &api_data_array_push_i64,
      &api_data_array_push_u64,
      &api_data_array_push_f64,
      &api_data_array_push_string,
      &api_data_size,
      &api_data_get_bool,
      &api_data_get_i64,
      &api_data_get_u64,
      &api_data_get_f64,
      &api_data_get_string,
      &api_data_get_bytes,
  };
  storage.config_api = {
      &api_config_load,
      &api_config_save,
      &api_config_exists,
      &api_config_remove,
  };
  storage.persistent_data_api = {
      &api_persistent_load,
      &api_persistent_save,
      &api_persistent_exists,
      &api_persistent_remove,
  };
}

} // namespace arv::plugin
