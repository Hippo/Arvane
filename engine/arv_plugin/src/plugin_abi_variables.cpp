#include "plugin_manager_internal.hpp"

#include <iomanip>
#include <sstream>

namespace arv::plugin {
namespace {

[[nodiscard]] std::string variable_double_to_string(double value) {
  std::ostringstream out;
  out << std::setprecision(15) << value;
  return out.str();
}

[[nodiscard]] std::optional<text::TextVariableBinding> variable_binding_from_abi(
    ArvVariableBinding binding) noexcept {
  switch (binding) {
    case ARV_VARIABLE_BINDING_RESOLVED:
      return text::TextVariableBinding::Resolved;
    case ARV_VARIABLE_BINDING_LIVE:
      return text::TextVariableBinding::Live;
  }
  return std::nullopt;
}

[[nodiscard]] ArvResult validate_variable_name(
    PluginCallContext* ctx,
    ArvStringView name_view,
    std::string& out_name) {
  out_name = abi_string(name_view);
  if (out_name.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "variable name is empty");
  }
  if (out_name.size() > text::MaxTextVariableNameBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "variable name is too long");
  }
  if (!text::is_valid_utf8(out_name)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "variable name is not valid UTF-8");
  }
  if (!text::valid_text_variable_name(out_name)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "variable name is invalid");
  }
  if (text::is_reserved_builtin_text_variable(out_name)) {
    return make_result(
        ctx,
        ARV_RESULT_PERMISSION_DENIED,
        "built-in variable names are reserved");
  }
  if (out_name.find(':') != std::string::npos) {
    if (std::optional<std::string> error =
            validate_owned_resource_id(ctx->info(), out_name);
        error.has_value()) {
      return make_result(ctx, ARV_RESULT_PERMISSION_DENIED, *error);
    }
  }
  return ok(ctx);
}

[[nodiscard]] ArvResult validate_variable_value(
    PluginCallContext* ctx,
    std::string_view value) {
  if (value.size() > text::MaxTextVariableValueBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "variable value is too long");
  }
  if (!text::is_valid_utf8(value)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "variable value is not valid UTF-8");
  }
  return ok(ctx);
}

[[nodiscard]] ArvResult require_variable_store(
    PluginCallContext* ctx,
    text::TextVariableStore*& out_store) {
  ArvResult sync = require_sync_context(ctx, "VariablesAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.text_variables == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "text variable store is unavailable");
  }
  out_store = ctx->impl->runtime.text_variables;
  return ok(ctx);
}

[[nodiscard]] ArvResult api_variables_set_global_value(
    void* context,
    ArvStringView name_view,
    std::string value,
    ArvVariableBinding binding_view = ARV_VARIABLE_BINDING_RESOLVED) {
  PluginCallContext* ctx = call_context(context);
  std::optional<text::TextVariableBinding> binding =
      variable_binding_from_abi(binding_view);
  if (!binding.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid variable binding");
  }
  text::TextVariableStore* store{};
  ArvResult ready = require_variable_store(ctx, store);
  if (ready.code != ARV_RESULT_OK) {
    return ready;
  }
  std::string name;
  ArvResult name_result = validate_variable_name(ctx, name_view, name);
  if (name_result.code != ARV_RESULT_OK) {
    return name_result;
  }
  ArvResult value_result = validate_variable_value(ctx, value);
  if (value_result.code != ARV_RESULT_OK) {
    return value_result;
  }
  store->set_global(std::move(name), std::move(value), binding.value());
  return ok(ctx);
}

[[nodiscard]] ArvResult api_variables_set_player_value(
    void* context,
    ArvPlayerID player,
    ArvStringView name_view,
    std::string value,
    ArvVariableBinding binding_view = ARV_VARIABLE_BINDING_RESOLVED) {
  PluginCallContext* ctx = call_context(context);
  std::optional<text::TextVariableBinding> binding =
      variable_binding_from_abi(binding_view);
  if (!binding.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid variable binding");
  }
  text::TextVariableStore* store{};
  ArvResult ready = require_variable_store(ctx, store);
  if (ready.code != ARV_RESULT_OK) {
    return ready;
  }
  std::string name;
  ArvResult name_result = validate_variable_name(ctx, name_view, name);
  if (name_result.code != ARV_RESULT_OK) {
    return name_result;
  }
  ArvResult value_result = validate_variable_value(ctx, value);
  if (value_result.code != ARV_RESULT_OK) {
    return value_result;
  }
  store->set_player(PlayerID{player}, std::move(name), std::move(value), binding.value());
  return ok(ctx);
}

ArvResult api_variables_set_global_string(
    void* context,
    ArvStringView name,
    ArvStringView value) {
  return api_variables_set_global_value(context, name, abi_string(value));
}

ArvResult api_variables_set_global_bool(
    void* context,
    ArvStringView name,
    bool value) {
  return api_variables_set_global_value(context, name, value ? "true" : "false");
}

ArvResult api_variables_set_global_i64(
    void* context,
    ArvStringView name,
    int64_t value) {
  return api_variables_set_global_value(context, name, std::to_string(value));
}

ArvResult api_variables_set_global_u64(
    void* context,
    ArvStringView name,
    uint64_t value) {
  return api_variables_set_global_value(context, name, std::to_string(value));
}

ArvResult api_variables_set_global_f64(
    void* context,
    ArvStringView name,
    double value) {
  return api_variables_set_global_value(context, name, variable_double_to_string(value));
}

ArvResult api_variables_set_global_string_bound(
    void* context,
    ArvStringView name,
    ArvStringView value,
    ArvVariableBinding binding) {
  return api_variables_set_global_value(context, name, abi_string(value), binding);
}

ArvResult api_variables_set_global_bool_bound(
    void* context,
    ArvStringView name,
    bool value,
    ArvVariableBinding binding) {
  return api_variables_set_global_value(
      context,
      name,
      value ? "true" : "false",
      binding);
}

ArvResult api_variables_set_global_i64_bound(
    void* context,
    ArvStringView name,
    int64_t value,
    ArvVariableBinding binding) {
  return api_variables_set_global_value(context, name, std::to_string(value), binding);
}

ArvResult api_variables_set_global_u64_bound(
    void* context,
    ArvStringView name,
    uint64_t value,
    ArvVariableBinding binding) {
  return api_variables_set_global_value(context, name, std::to_string(value), binding);
}

ArvResult api_variables_set_global_f64_bound(
    void* context,
    ArvStringView name,
    double value,
    ArvVariableBinding binding) {
  return api_variables_set_global_value(
      context,
      name,
      variable_double_to_string(value),
      binding);
}

ArvResult api_variables_clear_global(void* context, ArvStringView name_view) {
  PluginCallContext* ctx = call_context(context);
  text::TextVariableStore* store{};
  ArvResult ready = require_variable_store(ctx, store);
  if (ready.code != ARV_RESULT_OK) {
    return ready;
  }
  std::string name;
  ArvResult name_result = validate_variable_name(ctx, name_view, name);
  if (name_result.code != ARV_RESULT_OK) {
    return name_result;
  }
  (void)store->clear_global(name);
  return ok(ctx);
}

ArvResult api_variables_set_player_string(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    ArvStringView value) {
  return api_variables_set_player_value(context, player, name, abi_string(value));
}

ArvResult api_variables_set_player_bool(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    bool value) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      value ? "true" : "false");
}

ArvResult api_variables_set_player_i64(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    int64_t value) {
  return api_variables_set_player_value(context, player, name, std::to_string(value));
}

ArvResult api_variables_set_player_u64(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    uint64_t value) {
  return api_variables_set_player_value(context, player, name, std::to_string(value));
}

ArvResult api_variables_set_player_f64(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    double value) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      variable_double_to_string(value));
}

ArvResult api_variables_set_player_string_bound(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    ArvStringView value,
    ArvVariableBinding binding) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      abi_string(value),
      binding);
}

ArvResult api_variables_set_player_bool_bound(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    bool value,
    ArvVariableBinding binding) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      value ? "true" : "false",
      binding);
}

ArvResult api_variables_set_player_i64_bound(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    int64_t value,
    ArvVariableBinding binding) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      std::to_string(value),
      binding);
}

ArvResult api_variables_set_player_u64_bound(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    uint64_t value,
    ArvVariableBinding binding) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      std::to_string(value),
      binding);
}

ArvResult api_variables_set_player_f64_bound(
    void* context,
    ArvPlayerID player,
    ArvStringView name,
    double value,
    ArvVariableBinding binding) {
  return api_variables_set_player_value(
      context,
      player,
      name,
      variable_double_to_string(value),
      binding);
}

ArvResult api_variables_clear_player(
    void* context,
    ArvPlayerID player,
    ArvStringView name_view) {
  PluginCallContext* ctx = call_context(context);
  text::TextVariableStore* store{};
  ArvResult ready = require_variable_store(ctx, store);
  if (ready.code != ARV_RESULT_OK) {
    return ready;
  }
  std::string name;
  ArvResult name_result = validate_variable_name(ctx, name_view, name);
  if (name_result.code != ARV_RESULT_OK) {
    return name_result;
  }
  (void)store->clear_player_value(PlayerID{player}, name);
  return ok(ctx);
}

} // namespace

void fill_variables_api(PluginAPIStorage& storage) {
  storage.variables_api = {
      &api_variables_set_global_string,
      &api_variables_set_global_bool,
      &api_variables_set_global_i64,
      &api_variables_set_global_u64,
      &api_variables_set_global_f64,
      &api_variables_clear_global,
      &api_variables_set_player_string,
      &api_variables_set_player_bool,
      &api_variables_set_player_i64,
      &api_variables_set_player_u64,
      &api_variables_set_player_f64,
      &api_variables_clear_player,
      &api_variables_set_global_string_bound,
      &api_variables_set_global_bool_bound,
      &api_variables_set_global_i64_bound,
      &api_variables_set_global_u64_bound,
      &api_variables_set_global_f64_bound,
      &api_variables_set_player_string_bound,
      &api_variables_set_player_bool_bound,
      &api_variables_set_player_i64_bound,
      &api_variables_set_player_u64_bound,
      &api_variables_set_player_f64_bound,
  };
}

} // namespace arv::plugin
