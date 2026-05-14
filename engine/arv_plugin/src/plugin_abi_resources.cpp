#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

bool api_resource_exists(
    void* context,
    ArvResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || ctx->impl->options.packs == nullptr) {
    return false;
  }
  if (kind == ARV_RESOURCE_DATA) {
    return ctx->impl->options.packs
        ->resolve_data_path(abi_view(resource_id), abi_view(extension))
        .has_value();
  }
  return false;
}

ArvResult api_resource_read(
    void* context,
    ArvResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  if (ctx->impl->options.packs == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "pack manager is unavailable");
  }
  if (kind != ARV_RESOURCE_DATA) {
    return make_result(
        ctx,
        ARV_RESULT_UNSUPPORTED,
        "only data resource reads are implemented");
  }
  auto text = ctx->impl->options.packs->load_data_text(
      abi_view(resource_id),
      abi_view(extension));
  if (!text) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, text.error().describe());
  }
  *out_buffer = byte_buffer_from_string(std::move(text.value()));
  return ok(ctx);
}

ArvResult api_resource_debug_path(
    void* context,
    ArvResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  if (ctx->impl->options.packs == nullptr || kind != ARV_RESOURCE_DATA) {
    return make_result(
        ctx,
        ARV_RESULT_UNSUPPORTED,
        "only data resource debug paths are implemented");
  }
  auto resolved = ctx->impl->options.packs->resolve_data_path(
      abi_view(resource_id),
      abi_view(extension));
  if (!resolved) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, resolved.error().describe());
  }
  *out_buffer = byte_buffer_from_string(resolved.value().path.generic_string());
  return ok(ctx);
}

void api_release_buffer(void* context, ArvByteBuffer buffer) {
  (void)context;
  delete[] buffer.data;
}

} // namespace

void fill_resource_api(PluginAPIStorage& storage) {
  storage.resources_api = {
      &api_resource_exists,
      &api_resource_read,
      &api_resource_debug_path,
      &api_release_buffer,
  };
}

} // namespace arv::plugin
