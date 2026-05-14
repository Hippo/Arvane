#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvStringView api_pack_id(void* context) {
  PluginCallContext* ctx = call_context(context);
  return ctx == nullptr ? literal_view("") : stable_view(ctx->info().pack_id);
}

ArvStringView api_pack_version(void* context) {
  PluginCallContext* ctx = call_context(context);
  return ctx == nullptr ? literal_view("") : stable_view(ctx->info().pack_version);
}

ArvStringView api_side(void* context) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return literal_view("both");
  }
  switch (ctx->info().side) {
    case asset::PackSide::Client:
      return literal_view("client");
    case asset::PackSide::Server:
      return literal_view("server");
    case asset::PackSide::Both:
      return literal_view("both");
  }
  return literal_view("both");
}

uint32_t api_namespace_count(void* context) {
  PluginCallContext* ctx = call_context(context);
  return ctx == nullptr ? 0U : static_cast<uint32_t>(ctx->info().namespaces.size());
}

ArvStringView api_namespace_at(void* context, uint32_t index) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || index >= ctx->info().namespaces.size()) {
    return literal_view("");
  }
  return stable_view(ctx->info().namespaces[index]);
}

bool api_owns_namespace(void* context, ArvStringView name_space) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr && owns_namespace(ctx->info(), abi_view(name_space));
}

void log_for_plugin(
    void* context,
    core::LogLevel level,
    ArvStringView message) {
  PluginCallContext* ctx = call_context(context);
  const std::string pack = ctx == nullptr ? std::string{"<unknown>"}
                                          : ctx->info().pack_id;
  core::log(
      level,
      "plugin",
      "[" + pack + "] " + abi_string(message));
}

void api_log_trace(void* context, ArvStringView message) {
  log_for_plugin(context, core::LogLevel::Trace, message);
}

void api_log_debug(void* context, ArvStringView message) {
  log_for_plugin(context, core::LogLevel::Debug, message);
}

void api_log_info(void* context, ArvStringView message) {
  log_for_plugin(context, core::LogLevel::Info, message);
}

void api_log_warn(void* context, ArvStringView message) {
  log_for_plugin(context, core::LogLevel::Warning, message);
}

void api_log_error(void* context, ArvStringView message) {
  log_for_plugin(context, core::LogLevel::Error, message);
}


void api_metrics_counter_add(void* context, ArvResourceID metric_id, int64_t amount) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return;
  }
  core::debug(
      "plugin",
      "[{}] metric counter {} += {}",
      ctx->info().pack_id,
      abi_string(metric_id),
      amount);
}

} // namespace

void fill_context_api(PluginAPIStorage& storage) {
  storage.context_api = {
      &api_pack_id,
      &api_pack_version,
      &api_side,
      &api_namespace_count,
      &api_namespace_at,
      &api_owns_namespace,
  };
  storage.logger_api = {
      &api_log_trace,
      &api_log_debug,
      &api_log_info,
      &api_log_warn,
      &api_log_error,
  };
  storage.metrics_api = {&api_metrics_counter_add};
}

} // namespace arv::plugin
