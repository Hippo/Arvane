#include "arv/plugin/plugin_manager.hpp"

#include "dynamic_library.hpp"

#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace arv::plugin {
namespace {

enum class PluginThreadContext : u8 {
  None,
  Sync,
  Async,
};

thread_local PluginThreadContext tls_thread_context = PluginThreadContext::None;

class ThreadContextScope {
public:
  explicit ThreadContextScope(PluginThreadContext context) noexcept
      : previous_(tls_thread_context) {
    tls_thread_context = context;
  }

  ~ThreadContextScope() {
    tls_thread_context = previous_;
  }

private:
  PluginThreadContext previous_{PluginThreadContext::None};
};

[[nodiscard]] const char* safe_cstr(const char* text) noexcept {
  return text == nullptr ? "" : text;
}

[[nodiscard]] std::string_view abi_view(ArvStringView view) noexcept {
  return view.data == nullptr ? std::string_view{} : std::string_view{view.data, view.size};
}

[[nodiscard]] std::string abi_string(ArvStringView view) {
  return std::string{abi_view(view)};
}

[[nodiscard]] ArvStringView stable_view(const std::string& text) noexcept {
  return {text.data(), text.size()};
}

[[nodiscard]] ArvStringView literal_view(const char* text) noexcept {
  return {text, text == nullptr ? 0U : std::strlen(text)};
}

[[nodiscard]] bool ascii_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] bool command_name_valid(std::string_view name) noexcept {
  return !name.empty() &&
         std::none_of(name.begin(), name.end(), ascii_space);
}

[[nodiscard]] command::CommandDomain command_domain_from_abi(
    ArvPluginCommandDomain domain) noexcept {
  switch (domain) {
    case ARV_PLUGIN_COMMAND_CLIENT_BOUND:
      return command::CommandDomain::ClientBound;
    case ARV_PLUGIN_COMMAND_SERVER_BOUND:
      return command::CommandDomain::ServerBound;
  }
  return command::CommandDomain::ServerBound;
}

[[nodiscard]] command::CommandMessageKind command_message_kind_from_abi(
    ArvPluginCommandMessageKind kind) noexcept {
  switch (kind) {
    case ARV_PLUGIN_COMMAND_MESSAGE_INFO:
      return command::CommandMessageKind::Info;
    case ARV_PLUGIN_COMMAND_MESSAGE_SUCCESS:
      return command::CommandMessageKind::Success;
    case ARV_PLUGIN_COMMAND_MESSAGE_WARNING:
      return command::CommandMessageKind::Warning;
    case ARV_PLUGIN_COMMAND_MESSAGE_ERROR:
      return command::CommandMessageKind::Error;
  }
  return command::CommandMessageKind::Info;
}

[[nodiscard]] command::CommandStatus command_status_from_result(
    ArvPluginResultCode code) noexcept {
  switch (code) {
    case ARV_PLUGIN_RESULT_OK:
      return command::CommandStatus::Success;
    case ARV_PLUGIN_RESULT_PERMISSION_DENIED:
      return command::CommandStatus::PermissionDenied;
    case ARV_PLUGIN_RESULT_INVALID_ARGUMENT:
      return command::CommandStatus::UsageError;
    case ARV_PLUGIN_RESULT_NOT_FOUND:
      return command::CommandStatus::UnknownCommand;
    case ARV_PLUGIN_RESULT_INVALID_STATE:
    case ARV_PLUGIN_RESULT_DUPLICATE_ID:
    case ARV_PLUGIN_RESULT_UNSUPPORTED_VERSION:
    case ARV_PLUGIN_RESULT_THREAD_VIOLATION:
    case ARV_PLUGIN_RESULT_UNSUPPORTED:
    case ARV_PLUGIN_RESULT_INTERNAL_ERROR:
      return command::CommandStatus::Failure;
  }
  return command::CommandStatus::Failure;
}

[[nodiscard]] std::string result_code_name(ArvPluginResultCode code) {
  switch (code) {
    case ARV_PLUGIN_RESULT_OK:
      return "Ok";
    case ARV_PLUGIN_RESULT_INVALID_ARGUMENT:
      return "InvalidArgument";
    case ARV_PLUGIN_RESULT_INVALID_STATE:
      return "InvalidState";
    case ARV_PLUGIN_RESULT_PERMISSION_DENIED:
      return "PermissionDenied";
    case ARV_PLUGIN_RESULT_NOT_FOUND:
      return "NotFound";
    case ARV_PLUGIN_RESULT_DUPLICATE_ID:
      return "DuplicateID";
    case ARV_PLUGIN_RESULT_UNSUPPORTED_VERSION:
      return "UnsupportedVersion";
    case ARV_PLUGIN_RESULT_THREAD_VIOLATION:
      return "ThreadViolation";
    case ARV_PLUGIN_RESULT_UNSUPPORTED:
      return "Unsupported";
    case ARV_PLUGIN_RESULT_INTERNAL_ERROR:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] bool owns_namespace(
    const PluginInfo& info,
    std::string_view name_space) noexcept {
  return std::find(info.namespaces.begin(), info.namespaces.end(), name_space) !=
         info.namespaces.end();
}

[[nodiscard]] std::optional<std::string> validate_owned_resource_id(
    const PluginInfo& info,
    std::string_view id) {
  auto parsed = asset::ResourceID::parse(id);
  if (!parsed) {
    return parsed.error();
  }
  if (!owns_namespace(info, parsed.value().name_space)) {
    return "resource namespace is not owned by plugin pack: " +
           parsed.value().name_space;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> validate_resource_id(std::string_view id) {
  auto parsed = asset::ResourceID::parse(id);
  if (!parsed) {
    return parsed.error();
  }
  return std::nullopt;
}

[[nodiscard]] std::filesystem::path config_relative_path_for(
    const asset::ResourceID& id) {
  std::filesystem::path path{id.name_space};
  path /= std::filesystem::path{id.path};
  if (path.extension().empty()) {
    path += ".yaml";
  }
  return path;
}

[[nodiscard]] bool path_is_under_lexically(
    const std::filesystem::path& root,
    const std::filesystem::path& path) {
  const std::filesystem::path normalized_root =
      std::filesystem::absolute(root).lexically_normal();
  const std::filesystem::path normalized_path =
      std::filesystem::absolute(path).lexically_normal();
  auto root_it = normalized_root.begin();
  auto path_it = normalized_path.begin();
  for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
    if (path_it == normalized_path.end() || *path_it != *root_it) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_native_library_path(
    const asset::PackSource& pack,
    std::string_view platform_key,
    std::string& error) {
  if (!pack.manifest.native.has_value()) {
    error = "pack does not declare native metadata";
    return std::nullopt;
  }
  const auto found = pack.manifest.native->libraries.find(std::string{platform_key});
  if (found == pack.manifest.native->libraries.end()) {
    error = "native.libraries is missing current platform key: " +
            std::string{platform_key};
    return std::nullopt;
  }
  const std::filesystem::path path = (pack.root / found->second).lexically_normal();
  if (!path_is_under_lexically(pack.root, path)) {
    error = "native library path escapes pack root: " + found->second.generic_string();
    return std::nullopt;
  }
  if (!native_library_extension_matches_platform(path)) {
    error = "native library extension does not match platform: " +
            found->second.generic_string();
    return std::nullopt;
  }
  return path;
}

[[nodiscard]] ArvPluginResult null_context_result(
    ArvPluginResultCode code,
    const char* message) noexcept {
  return {code, message};
}

struct PluginCommandRegistration {
  usize owner_index{};
  std::string executor_id{};
  std::string command_name{};
  command::CommandDomain domain{command::CommandDomain::ServerBound};
  std::string permission{};
  std::string description{};
  std::string usage{};
  bool public_to_players{};
  bool console_only{};
  ArvPluginCommandCallback callback{};
  void* user_data{};
};

enum class ScheduledJobKind : u8 {
  Sync,
  Async,
};

struct ScheduledJob {
  u64 id{};
  usize owner_index{};
  ScheduledJobKind kind{ScheduledJobKind::Sync};
  ArvPluginJobCallback callback{};
  void* user_data{};
  TickIndex next_run{};
  TickIndex interval{};
  bool repeating{};
  bool canceled{};
  bool running{};
};

struct EventSubscription {
  u64 id{};
  usize owner_index{};
  std::string event_id{};
  ArvPluginEventPriority priority{ARV_PLUGIN_EVENT_PRIORITY_NORMAL};
  ArvPluginEventCallback callback{};
  void* user_data{};
  bool active{true};
};

struct ServiceRegistration {
  u64 id{};
  usize owner_index{};
  std::string service_id{};
  u32 api_version{};
  i32 priority{};
  ArvPluginServiceThreading threading{ARV_PLUGIN_SERVICE_SYNC_ONLY};
  std::string display_name{};
  const void* api_table{};
  void* user_data{};
  bool active{true};
};

struct PluginCallContext;

struct PluginAPIStorage {
  explicit PluginAPIStorage(PluginCallContext* context);

  ArvPluginContextAPI context_api{};
  ArvPluginLoggerAPI logger_api{};
  ArvPluginRegistriesAPI registries_api{};
  ArvPluginCommandsAPI commands_api{};
  ArvPluginEventsAPI events_api{};
  ArvPluginSchedulerAPI scheduler_api{};
  ArvPluginServicesAPI services_api{};
  ArvPluginResourcesAPI resources_api{};
  ArvPluginWorldsAPI worlds_api{};
  ArvPluginEntitiesAPI entities_api{};
  ArvPluginConfigAPI config_api{};
  ArvPluginPersistentDataAPI persistent_data_api{};
  ArvPluginWorldGenAPI worldgen_api{};
  ArvPluginNetworkAPI network_api{};
  ArvPluginTextAPI text_api{};
  ArvPluginMetricsAPI metrics_api{};
  ArvPluginPermissionsAPI permissions_api{};
  ArvPluginAPI api{};
};

struct PluginRecord {
  PluginInfo info{};
  usize pack_order{};
  const asset::PackSource* pack{};
  std::unique_ptr<PluginCallContext> call_context{};
  std::unique_ptr<PluginAPIStorage> api_storage{};
  detail::DynamicLibrary library{};
};

} // namespace

struct PluginManagerImpl {
  PluginLoadOptions options{};
  PluginRuntimeContext runtime{};
  bool registration_open{};
  bool shutdown_started{};
  bool runtime_active{};
  bool shutdown_event_dispatched{};
  std::vector<PluginRecord> plugins{};
  std::vector<PluginError> diagnostics{};
  std::vector<PluginCommandRegistration> commands{};
  std::vector<ScheduledJob> jobs{};
  std::vector<EventSubscription> subscriptions{};
  std::vector<ServiceRegistration> services{};
  std::atomic<u64> next_job_id{1};
  u64 next_subscription_id{1};
  u64 next_service_id{1};
  mutable std::mutex jobs_mutex;
};

namespace {

struct PluginCallContext {
  PluginManagerImpl* impl{};
  usize plugin_index{};
  std::string result_message{};

  [[nodiscard]] PluginInfo& info() noexcept {
    return impl->plugins[plugin_index].info;
  }

  [[nodiscard]] const PluginInfo& info() const noexcept {
    return impl->plugins[plugin_index].info;
  }
};

[[nodiscard]] ArvPluginResult make_result(
    PluginCallContext* context,
    ArvPluginResultCode code,
    std::string message = {}) {
  if (context == nullptr) {
    return null_context_result(code, "plugin API context is null");
  }
  context->result_message = std::move(message);
  return {code, context->result_message.c_str()};
}

[[nodiscard]] ArvPluginResult ok(PluginCallContext* context) {
  return make_result(context, ARV_PLUGIN_RESULT_OK);
}

[[nodiscard]] PluginCallContext* call_context(void* context) noexcept {
  return static_cast<PluginCallContext*>(context);
}

void append_diagnostic(
    PluginManagerImpl& impl,
    std::string pack_id,
    std::string phase,
    std::filesystem::path file,
    std::string message) {
  PluginError diagnostic{
      std::move(pack_id),
      std::move(phase),
      std::move(file),
      std::move(message)};
  core::warn("plugin", diagnostic.describe());
  impl.diagnostics.push_back(std::move(diagnostic));
}

[[nodiscard]] ArvPluginResult require_registration_phase(
    PluginCallContext* context) {
  if (context == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (!context->impl->registration_open) {
    return make_result(
        context,
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "API may only be used during plugin registration");
  }
  return ok(context);
}

[[nodiscard]] ArvPluginResult require_sync_context(
    PluginCallContext* context,
    std::string_view api_name) {
  if (context == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (tls_thread_context != PluginThreadContext::Sync) {
    return make_result(
        context,
        ARV_PLUGIN_RESULT_THREAD_VIOLATION,
        std::string{api_name} + " requires sync plugin context");
  }
  return ok(context);
}

[[nodiscard]] world::WorldRuntime* world_from_handle(
    PluginCallContext* context,
    ArvWorldHandle handle) {
  if (context == nullptr || context->impl->runtime.default_world == nullptr) {
    return nullptr;
  }
  world::WorldRuntime* world = context->impl->runtime.default_world;
  return world->id().value == handle ? world : nullptr;
}

[[nodiscard]] ArvPluginByteBuffer byte_buffer_from_string(std::string text) {
  ArvPluginByteBuffer buffer{};
  buffer.size = text.size();
  if (buffer.size == 0U) {
    return buffer;
  }
  buffer.data = new uint8_t[buffer.size];
  std::memcpy(buffer.data, text.data(), buffer.size);
  return buffer;
}

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

ArvPluginResult api_register_block_behavior(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult phase = require_registration_phase(ctx);
      phase.code != ARV_PLUGIN_RESULT_OK) {
    return phase;
  }
  const std::string_view behavior_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), behavior_id);
      error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.block_behaviors == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_UNSUPPORTED,
        "block behavior registry is unavailable");
  }
  auto result = ctx->impl->options.block_behaviors->register_handler(std::string{behavior_id});
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_PLUGIN_RESULT_DUPLICATE_ID
            : ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

ArvPluginResult api_register_item_behavior(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult phase = require_registration_phase(ctx);
      phase.code != ARV_PLUGIN_RESULT_OK) {
    return phase;
  }
  const std::string_view behavior_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), behavior_id);
      error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.item_behaviors == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_UNSUPPORTED,
        "item behavior registry is unavailable");
  }
  auto result = ctx->impl->options.item_behaviors->register_handler(std::string{behavior_id});
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_PLUGIN_RESULT_DUPLICATE_ID
            : ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

ArvPluginResult api_register_entity_goal(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult phase = require_registration_phase(ctx);
      phase.code != ARV_PLUGIN_RESULT_OK) {
    return phase;
  }
  const std::string_view goal_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), goal_id);
      error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.entity_goals == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_UNSUPPORTED,
        "entity goal registry is unavailable");
  }
  world::EntityGoalHandler handler;
  handler.can_start = [](world::EntityGoalContext&) {
    return false;
  };
  handler.can_continue = [](world::EntityGoalContext&) {
    return false;
  };
  auto result = ctx->impl->options.entity_goals->register_handler(
      std::string{goal_id},
      std::move(handler));
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_PLUGIN_RESULT_DUPLICATE_ID
            : ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

ArvPluginResult unsupported_registration(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  (void)id;
  return make_result(
      ctx,
      ARV_PLUGIN_RESULT_UNSUPPORTED,
      "this registry extension point is not implemented by the engine");
}

ArvPluginResult api_register_executor(
    void* context,
    const ArvPluginCommandDescriptor* descriptor) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult phase = require_registration_phase(ctx);
      phase.code != ARV_PLUGIN_RESULT_OK) {
    return phase;
  }
  if (descriptor == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "command descriptor is null");
  }
  if (descriptor->execute == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "command descriptor execute callback is null");
  }
  const std::string executor_id{abi_view(descriptor->executor_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), executor_id);
      error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  const std::string command_name{abi_view(descriptor->command_name)};
  if (!command_name_valid(command_name)) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "command name must be non-empty and contain no whitespace");
  }
  const auto duplicate = std::find_if(
      ctx->impl->commands.begin(),
      ctx->impl->commands.end(),
      [&](const PluginCommandRegistration& command) {
        return command.executor_id == executor_id ||
               command.command_name == command_name;
      });
  if (duplicate != ctx->impl->commands.end()) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_DUPLICATE_ID,
        "duplicate command executor or command name: " + executor_id);
  }

  ctx->impl->commands.push_back({
      ctx->plugin_index,
      executor_id,
      command_name,
      command_domain_from_abi(descriptor->domain),
      abi_string(descriptor->permission),
      abi_string(descriptor->description),
      abi_string(descriptor->usage),
      descriptor->public_to_players,
      descriptor->console_only,
      descriptor->execute,
      descriptor->user_data,
  });
  return ok(ctx);
}

ArvPluginResult schedule_job(
    PluginCallContext* ctx,
    ScheduledJobKind kind,
    TickIndex delay,
    TickIndex interval,
    bool repeating,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "scheduled job callback is null");
  }
  if (repeating && interval == 0U) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "repeating scheduled job interval must be non-zero");
  }
  if (ctx->impl->shutdown_started) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin manager is shutting down");
  }

  const u64 id = ctx->impl->next_job_id.fetch_add(1U, std::memory_order_relaxed);
  const TickIndex next_run =
      ctx->impl->runtime.tick_index + (delay == 0U ? 1U : delay);
  {
    std::lock_guard lock{ctx->impl->jobs_mutex};
    ctx->impl->jobs.push_back({
        id,
        ctx->plugin_index,
        kind,
        callback,
        user_data,
        next_run,
        interval,
        repeating,
        false,
        false,
    });
  }
  if (out_handle != nullptr) {
    out_handle->value = id;
  }
  return ok(ctx);
}

ArvPluginResult api_sync_now(
    void* context,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      0U,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_sync_delayed(
    void* context,
    uint64_t delay_ticks,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      delay_ticks,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_sync_repeating(
    void* context,
    uint64_t start_delay_ticks,
    uint64_t interval_ticks,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      start_delay_ticks,
      interval_ticks,
      true,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_async_now(
    void* context,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      0U,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_async_delayed(
    void* context,
    uint64_t delay_ticks,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      delay_ticks,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_async_repeating(
    void* context,
    uint64_t start_delay_ticks,
    uint64_t interval_ticks,
    ArvPluginJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      start_delay_ticks,
      interval_ticks,
      true,
      callback,
      user_data,
      out_handle);
}

ArvPluginResult api_cancel_job(void* context, ArvScheduledJobHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  std::lock_guard lock{ctx->impl->jobs_mutex};
  for (ScheduledJob& job : ctx->impl->jobs) {
    if (job.id == handle.value && job.owner_index == ctx->plugin_index) {
      job.canceled = true;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "scheduled job not found");
}

ArvPluginResult api_subscribe_event(
    void* context,
    ArvResourceID event_id,
    ArvPluginEventPriority priority,
    ArvPluginEventCallback callback,
    void* user_data,
    ArvEventSubscriptionHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "event callback is null");
  }
  const std::string id{abi_view(event_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  const u64 handle = ctx->impl->next_subscription_id++;
  ctx->impl->subscriptions.push_back({
      handle,
      ctx->plugin_index,
      id,
      priority,
      callback,
      user_data,
      true,
  });
  if (out_handle != nullptr) {
    out_handle->value = handle;
  }
  return ok(ctx);
}

ArvPluginResult api_unsubscribe_event(
    void* context,
    ArvEventSubscriptionHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  for (EventSubscription& subscription : ctx->impl->subscriptions) {
    if (subscription.id == handle.value &&
        subscription.owner_index == ctx->plugin_index &&
        subscription.active) {
      subscription.active = false;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "event subscription not found");
}

ArvPluginResult api_publish_service(
    void* context,
    const ArvPluginServiceDescriptor* descriptor,
    ArvServiceHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (descriptor == nullptr || descriptor->api_table == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "service descriptor or API table is null");
  }
  if (descriptor->api_version == 0U) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "service API version must be non-zero");
  }
  const std::string id{abi_view(descriptor->service_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id);
      error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  const u64 handle = ctx->impl->next_service_id++;
  ctx->impl->services.push_back({
      handle,
      ctx->plugin_index,
      id,
      descriptor->api_version,
      descriptor->priority,
      descriptor->threading,
      abi_string(descriptor->display_name),
      descriptor->api_table,
      descriptor->user_data,
      true,
  });
  if (out_handle != nullptr) {
    out_handle->value = handle;
  }
  return ok(ctx);
}

ArvPluginResult api_unpublish_service(void* context, ArvServiceHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  for (ServiceRegistration& service : ctx->impl->services) {
    if (service.id == handle.value &&
        service.owner_index == ctx->plugin_index &&
        service.active) {
      service.active = false;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "service not found");
}

[[nodiscard]] bool service_version_matches(
    const ServiceRegistration& service,
    u32 min_version,
    u32 max_version) noexcept {
  const bool above_min = min_version == 0U || service.api_version >= min_version;
  const bool below_max = max_version == 0U || service.api_version <= max_version;
  return above_min && below_max;
}

[[nodiscard]] std::vector<const ServiceRegistration*> matching_services(
    PluginManagerImpl& impl,
    std::string_view service_id,
    u32 min_api_version,
    u32 max_api_version) {
  std::vector<const ServiceRegistration*> matches;
  for (const ServiceRegistration& service : impl.services) {
    if (!service.active || service.service_id != service_id ||
        !service_version_matches(service, min_api_version, max_api_version)) {
      continue;
    }
    matches.push_back(&service);
  }
  std::sort(matches.begin(), matches.end(), [&](const ServiceRegistration* lhs, const ServiceRegistration* rhs) {
    if (lhs->priority != rhs->priority) {
      return lhs->priority > rhs->priority;
    }
    const usize lhs_order = impl.plugins[lhs->owner_index].pack_order;
    const usize rhs_order = impl.plugins[rhs->owner_index].pack_order;
    if (lhs_order != rhs_order) {
      return lhs_order < rhs_order;
    }
    return lhs->service_id < rhs->service_id;
  });
  return matches;
}

[[nodiscard]] ArvPluginServiceRef service_ref_for(
    const PluginManagerImpl& impl,
    const ServiceRegistration& service) {
  return {
      ArvServiceHandle{service.id},
      stable_view(service.service_id),
      service.api_version,
      stable_view(impl.plugins[service.owner_index].info.pack_id),
      service.api_table,
      service.user_data,
      service.active,
  };
}

ArvPluginResult api_get_service(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvPluginServiceRef* out_service) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_service == nullptr) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INVALID_ARGUMENT,
        "out_service is null");
  }
  const std::string id{abi_view(service_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::vector<const ServiceRegistration*> matches =
      matching_services(*ctx->impl, id, min_api_version, max_api_version);
  if (matches.empty()) {
    *out_service = {};
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "service not found");
  }
  *out_service = service_ref_for(*ctx->impl, *matches.front());
  return ok(ctx);
}

uint64_t api_list_services(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvPluginServiceRef* out_services,
    uint64_t max_services) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  const std::string id{abi_view(service_id)};
  if (validate_resource_id(id).has_value()) {
    return 0U;
  }
  std::vector<const ServiceRegistration*> matches =
      matching_services(*ctx->impl, id, min_api_version, max_api_version);
  if (out_services != nullptr && max_services > 0U) {
    const uint64_t count = std::min<uint64_t>(max_services, matches.size());
    for (uint64_t i = 0; i < count; ++i) {
      out_services[i] = service_ref_for(*ctx->impl, *matches[i]);
    }
  }
  return matches.size();
}

void api_release_service(void* context, ArvPluginServiceRef service) {
  (void)context;
  (void)service;
}

bool api_resource_exists(
    void* context,
    ArvPluginResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || ctx->impl->options.packs == nullptr) {
    return false;
  }
  if (kind == ARV_PLUGIN_RESOURCE_DATA) {
    return ctx->impl->options.packs
        ->resolve_data_path(abi_view(resource_id), abi_view(extension))
        .has_value();
  }
  return false;
}

ArvPluginResult api_resource_read(
    void* context,
    ArvPluginResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension,
    ArvPluginByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  if (ctx->impl->options.packs == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_UNSUPPORTED, "pack manager is unavailable");
  }
  if (kind != ARV_PLUGIN_RESOURCE_DATA) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_UNSUPPORTED,
        "only data resource reads are implemented");
  }
  auto text = ctx->impl->options.packs->load_data_text(
      abi_view(resource_id),
      abi_view(extension));
  if (!text) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, text.error().describe());
  }
  *out_buffer = byte_buffer_from_string(std::move(text.value()));
  return ok(ctx);
}

ArvPluginResult api_resource_debug_path(
    void* context,
    ArvPluginResourceKind kind,
    ArvResourceID resource_id,
    ArvStringView extension,
    ArvPluginByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  if (ctx->impl->options.packs == nullptr || kind != ARV_PLUGIN_RESOURCE_DATA) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_UNSUPPORTED,
        "only data resource debug paths are implemented");
  }
  auto resolved = ctx->impl->options.packs->resolve_data_path(
      abi_view(resource_id),
      abi_view(extension));
  if (!resolved) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, resolved.error().describe());
  }
  *out_buffer = byte_buffer_from_string(resolved.value().path.generic_string());
  return ok(ctx);
}

void api_release_buffer(void* context, ArvPluginByteBuffer buffer) {
  (void)context;
  delete[] buffer.data;
}

ArvPluginResult api_world_get_block(
    void* context,
    ArvWorldHandle world_handle,
    ArvPluginBlockPos pos,
    uint32_t* out_block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "world.get_block");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  if (out_block_variant == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_block_variant is null");
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  *out_block_variant = world->get_block({pos.x, pos.y, pos.z}).value;
  return ok(ctx);
}

ArvPluginResult api_world_set_block(
    void* context,
    ArvWorldHandle world_handle,
    ArvPluginBlockPos pos,
    uint32_t block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "world.set_block");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  if (ctx->impl->runtime.block_registry != nullptr &&
      ctx->impl->runtime.block_registry->variant(voxel::BlockVariantID{block_variant}) == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "block variant not found");
  }
  if (!world->set_block({pos.x, pos.y, pos.z}, voxel::BlockVariantID{block_variant})) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_STATE, "block write failed");
  }
  return ok(ctx);
}

ArvPluginResult api_world_current_tick(
    void* context,
    ArvWorldHandle world_handle,
    uint64_t* out_tick) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_tick == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_tick is null");
  }
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  *out_tick = world->tick_index();
  return ok(ctx);
}

ArvPluginResult api_world_seed(
    void* context,
    ArvWorldHandle world_handle,
    uint64_t* out_seed) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_seed == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_seed is null");
  }
  if (world_from_handle(ctx, world_handle) == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  *out_seed = ctx->impl->runtime.world_seed;
  return ok(ctx);
}

ArvPluginResult api_entity_spawn(
    void* context,
    ArvWorldHandle world_handle,
    ArvResourceID entity_type,
    ArvPluginVec3 position,
    ArvEntityHandle* out_entity) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "entities.spawn");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  if (out_entity == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_entity is null");
  }
  *out_entity = 0U;
  world::WorldRuntime* world = world_from_handle(ctx, world_handle);
  if (world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  if (ctx->impl->runtime.entity_registry == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_UNSUPPORTED, "entity registry is unavailable");
  }
  const std::optional<EntityTypeID> type_id =
      ctx->impl->runtime.entity_registry->find_type(abi_view(entity_type));
  if (!type_id.has_value()) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "entity type not found");
  }
  world::EntityCreateInfo info;
  info.type_id = type_id.value();
  info.type_name = abi_string(entity_type);
  info.position = {position.x, position.y, position.z};
  const EntityID entity = world->create_entity(std::move(info));
  *out_entity = entity.value;
  return ok(ctx);
}

ArvPluginResult api_entity_destroy(void* context, ArvEntityHandle entity) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "entities.destroy");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->destroy_entity(EntityID{entity})) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

bool api_entity_exists(void* context, ArvEntityHandle entity) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr && ctx->impl->runtime.default_world != nullptr &&
         tls_thread_context == PluginThreadContext::Sync &&
         ctx->impl->runtime.default_world->entity_exists(EntityID{entity});
}

ArvPluginResult api_entity_world(
    void* context,
    ArvEntityHandle entity,
    ArvWorldHandle* out_world) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "entities.world");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  if (out_world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_world is null");
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr || !world->entity_exists(EntityID{entity})) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "entity not found");
  }
  *out_world = world->id().value;
  return ok(ctx);
}

ArvPluginResult api_entity_position(
    void* context,
    ArvEntityHandle entity,
    ArvPluginVec3* out_position) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "entities.position");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  if (out_position == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_position is null");
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "world not found");
  }
  const entity::EntityTransform* transform =
      world->find_entity_transform(EntityID{entity});
  if (transform == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "entity transform not found");
  }
  *out_position = {
      transform->position.x,
      transform->position.y,
      transform->position.z};
  return ok(ctx);
}

ArvPluginResult api_entity_set_position(
    void* context,
    ArvEntityHandle entity,
    ArvPluginVec3 position) {
  PluginCallContext* ctx = call_context(context);
  if (ArvPluginResult sync = require_sync_context(ctx, "entities.set_position");
      sync.code != ARV_PLUGIN_RESULT_OK) {
    return sync;
  }
  world::WorldRuntime* world = ctx->impl->runtime.default_world;
  if (world == nullptr ||
      !world->set_entity_position(EntityID{entity}, {position.x, position.y, position.z})) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "entity not found");
  }
  return ok(ctx);
}

ArvPluginResult unsupported_entity_tags(void* context, ArvEntityHandle entity, ArvResourceID tag) {
  PluginCallContext* ctx = call_context(context);
  (void)entity;
  (void)tag;
  return make_result(
      ctx,
      ARV_PLUGIN_RESULT_UNSUPPORTED,
      "entity tags are not implemented by the engine");
}

ArvPluginResult unsupported_entity_has_tag(
    void* context,
    ArvEntityHandle entity,
    ArvResourceID tag,
    bool* out_has_tag) {
  PluginCallContext* ctx = call_context(context);
  (void)entity;
  (void)tag;
  if (out_has_tag != nullptr) {
    *out_has_tag = false;
  }
  return make_result(
      ctx,
      ARV_PLUGIN_RESULT_UNSUPPORTED,
      "entity tags are not implemented by the engine");
}

ArvPluginResult api_load_plugin_config(
    void* context,
    ArvResourceID config_id,
    ArvPluginByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  const std::string id_text{abi_view(config_id)};
  auto parsed = asset::ResourceID::parse(id_text);
  if (!parsed) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, parsed.error());
  }
  if (!owns_namespace(ctx->info(), parsed.value().name_space)) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_PERMISSION_DENIED,
        "config namespace is not owned by plugin pack");
  }
  const std::filesystem::path path =
      ctx->impl->options.config_root / "plugins" /
      ctx->info().pack_id / config_relative_path_for(parsed.value());
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return make_result(ctx, ARV_PLUGIN_RESULT_NOT_FOUND, "plugin config not found");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  *out_buffer = byte_buffer_from_string(buffer.str());
  return ok(ctx);
}

ArvPluginResult api_save_plugin_config(
    void* context,
    ArvResourceID config_id,
    ArvBytesView bytes) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_PLUGIN_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (bytes.data == nullptr && bytes.size != 0U) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "config bytes are null");
  }
  constexpr uint64_t MaxPluginConfigBytes = 1024ULL * 1024ULL;
  if (bytes.size > MaxPluginConfigBytes) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, "plugin config is too large");
  }
  const std::string id_text{abi_view(config_id)};
  auto parsed = asset::ResourceID::parse(id_text);
  if (!parsed) {
    return make_result(ctx, ARV_PLUGIN_RESULT_INVALID_ARGUMENT, parsed.error());
  }
  if (!owns_namespace(ctx->info(), parsed.value().name_space)) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_PERMISSION_DENIED,
        "config namespace is not owned by plugin pack");
  }
  const std::filesystem::path path =
      ctx->impl->options.config_root / "plugins" /
      ctx->info().pack_id / config_relative_path_for(parsed.value());
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INTERNAL_ERROR,
        "failed to create plugin config directory: " + error.message());
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return make_result(
        ctx,
        ARV_PLUGIN_RESULT_INTERNAL_ERROR,
        "failed to open plugin config for writing");
  }
  output.write(
      reinterpret_cast<const char*>(bytes.data),
      static_cast<std::streamsize>(bytes.size));
  return ok(ctx);
}

ArvPluginResult unsupported_no_arg(void* context) {
  PluginCallContext* ctx = call_context(context);
  return make_result(
      ctx,
      ARV_PLUGIN_RESULT_UNSUPPORTED,
      "this plugin API is not implemented by the engine");
}

ArvPluginResult unsupported_worldgen_registration(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  (void)id;
  return make_result(
      ctx,
      ARV_PLUGIN_RESULT_UNSUPPORTED,
      "native worldgen type registries are not implemented by the engine");
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

struct CommandExecutionContext {
  explicit CommandExecutionContext(command::CommandCtx& cpp_ctx)
      : cpp_context(cpp_ctx) {
    args.reserve(cpp_ctx.args.tokens.size());
    arg_views.reserve(cpp_ctx.args.tokens.size());
    for (std::string_view token : cpp_ctx.args.tokens) {
      args.emplace_back(token);
    }
    for (const std::string& arg : args) {
      arg_views.push_back(stable_view(arg));
    }
    abi_context.arg_count = static_cast<uint32_t>(arg_views.size());
    abi_context.args = arg_views.data();
    abi_context.engine_context = this;
    abi_context.reply = &reply;
  }

  static void reply(
      ArvPluginCommandContext* command_context,
      ArvPluginCommandMessageKind kind,
      ArvStringView message) {
    if (command_context == nullptr || command_context->engine_context == nullptr) {
      return;
    }
    auto* self = static_cast<CommandExecutionContext*>(command_context->engine_context);
    self->messages.push_back({
        command_message_kind_from_abi(kind),
        abi_string(message),
    });
  }

  command::CommandCtx& cpp_context;
  std::vector<std::string> args{};
  std::vector<ArvStringView> arg_views{};
  std::vector<command::CommandMessage> messages{};
  ArvPluginCommandContext abi_context{};
};

[[nodiscard]] command::CommandResult invoke_plugin_command(
    PluginManagerImpl& impl,
    const PluginCommandRegistration& registration,
    command::CommandCtx& ctx) {
  if (registration.owner_index >= impl.plugins.size() ||
      registration.callback == nullptr) {
    return command::CommandResult::internal_error("Plugin command owner is unavailable.");
  }

  CommandExecutionContext execution{ctx};
  ThreadContextScope sync_scope{PluginThreadContext::Sync};
  ArvPluginResult result =
      registration.callback(&execution.abi_context, registration.user_data);

  command::CommandResult command_result;
  command_result.status = command_status_from_result(result.code);
  command_result.messages = std::move(execution.messages);
  if (result.code != ARV_PLUGIN_RESULT_OK && command_result.messages.empty()) {
    const std::string message =
        result.message != nullptr && result.message[0] != '\0'
            ? std::string{result.message}
            : "Plugin command failed: " + result_code_name(result.code);
    command_result.add_message(command::CommandMessageKind::Error, message);
  }
  return command_result;
}

PluginAPIStorage::PluginAPIStorage(PluginCallContext* context) {
  context_api = {
      &api_pack_id,
      &api_pack_version,
      &api_side,
      &api_namespace_count,
      &api_namespace_at,
      &api_owns_namespace,
  };
  logger_api = {
      &api_log_trace,
      &api_log_debug,
      &api_log_info,
      &api_log_warn,
      &api_log_error,
  };
  registries_api = {
      &api_register_block_behavior,
      &api_register_item_behavior,
      &api_register_entity_goal,
      &unsupported_registration,
      &unsupported_registration,
      &unsupported_registration,
      &unsupported_registration,
  };
  commands_api = {&api_register_executor};
  events_api = {&api_subscribe_event, &api_unsubscribe_event};
  scheduler_api = {
      &api_sync_now,
      &api_sync_delayed,
      &api_sync_repeating,
      &api_async_now,
      &api_async_delayed,
      &api_async_repeating,
      &api_cancel_job,
  };
  services_api = {
      &api_publish_service,
      &api_unpublish_service,
      &api_get_service,
      &api_list_services,
      &api_release_service,
  };
  resources_api = {
      &api_resource_exists,
      &api_resource_read,
      &api_resource_debug_path,
      &api_release_buffer,
  };
  worlds_api = {
      &api_world_get_block,
      &api_world_set_block,
      &api_world_current_tick,
      &api_world_seed,
  };
  entities_api = {
      &api_entity_spawn,
      &api_entity_destroy,
      &api_entity_exists,
      &api_entity_world,
      &api_entity_position,
      &api_entity_set_position,
      &unsupported_entity_tags,
      &unsupported_entity_tags,
      &unsupported_entity_has_tag,
  };
  config_api = {
      &api_load_plugin_config,
      &api_save_plugin_config,
  };
  persistent_data_api = {&unsupported_no_arg};
  worldgen_api = {
      &unsupported_worldgen_registration,
      &unsupported_worldgen_registration,
      &unsupported_worldgen_registration,
  };
  network_api = {&unsupported_no_arg};
  text_api = {&unsupported_no_arg};
  metrics_api = {&api_metrics_counter_add};
  permissions_api = {&unsupported_no_arg};
  api = {
      PluginApiVersion,
      context,
      &context_api,
      context,
      &registries_api,
      context,
      &resources_api,
      context,
      &worlds_api,
      context,
      &entities_api,
      context,
      &events_api,
      context,
      &scheduler_api,
      context,
      &commands_api,
      context,
      &permissions_api,
      context,
      &services_api,
      context,
      &config_api,
      context,
      &persistent_data_api,
      context,
      &worldgen_api,
      context,
      &network_api,
      context,
      &text_api,
      context,
      &metrics_api,
      context,
      &logger_api,
  };
}

} // namespace

std::string PluginError::describe() const {
  std::string text;
  if (!phase.empty()) {
    text += phase;
    text += ": ";
  }
  text += message;
  if (!pack_id.empty()) {
    text += " (pack ";
    text += pack_id;
    text += ")";
  }
  if (!file.empty()) {
    text += " [file ";
    text += file.generic_string();
    text += "]";
  }
  return text;
}

bool NativePluginPolicy::allows(std::string_view pack_id) const noexcept {
  return std::find(allowed_packs.begin(), allowed_packs.end(), pack_id) !=
         allowed_packs.end();
}

PluginManager::PluginManager() : impl_(std::make_unique<PluginManagerImpl>()) {
}

PluginManager::~PluginManager() {
  shutdown();
}

PluginManager::PluginManager(PluginManager&&) noexcept = default;

PluginManager& PluginManager::operator=(PluginManager&&) noexcept = default;

PluginResult PluginManager::load_plugins(PluginLoadOptions options) {
  if (options.packs == nullptr) {
    return PluginResult::failure({"", "plugin loading", {}, "pack manager is null"});
  }

  shutdown();
  impl_ = std::make_unique<PluginManagerImpl>();
  impl_->options = std::move(options);
  impl_->registration_open = true;

  const std::string platform_key = current_native_platform_key();
  const std::span<const asset::PackSource> stack = impl_->options.packs->stack();
  impl_->plugins.reserve(stack.size());

  for (usize pack_index = 0; pack_index < stack.size(); ++pack_index) {
    const asset::PackSource& pack = stack[pack_index];
    if (!pack.manifest.native.has_value()) {
      continue;
    }

    PluginRecord record;
    record.pack = &pack;
    record.pack_order = pack_index;
    record.info.pack_id = pack.manifest.id;
    record.info.pack_version = pack.manifest.version;
    record.info.namespaces = pack.manifest.namespaces;
    record.info.side = pack.manifest.side;
    record.info.entrypoint = pack.manifest.native->entrypoint;
    record.info.state = PluginState::Discovered;

    if (!impl_->options.policy.enabled) {
      record.info.state = PluginState::SkippedByPolicy;
      record.info.diagnostics.push_back("native plugins are disabled by runtime policy");
      append_diagnostic(
          *impl_,
          record.info.pack_id,
          "native plugin policy",
          pack.root / "pack.yaml",
          "skipped native plugin because native_plugins.enabled is false");
      impl_->plugins.push_back(std::move(record));
      continue;
    }
    if (!impl_->options.policy.allows(pack.manifest.id)) {
      record.info.state = PluginState::SkippedByPolicy;
      record.info.diagnostics.push_back("pack is not allowlisted for native plugin loading");
      append_diagnostic(
          *impl_,
          record.info.pack_id,
          "native plugin policy",
          pack.root / "pack.yaml",
          "skipped native plugin because pack is not in native_plugins.allowed_packs");
      impl_->plugins.push_back(std::move(record));
      continue;
    }
    if (pack.manifest.native->api_version != PluginApiVersion) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back("unsupported native plugin API version");
      PluginError error{
          pack.manifest.id,
          "plugin ABI validation",
          pack.root / "pack.yaml",
          "unsupported native plugin API version " +
              std::to_string(pack.manifest.native->api_version) +
              "; engine supports " + std::to_string(PluginApiVersion)};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    std::string path_error;
    std::optional<std::filesystem::path> library_path =
        resolve_native_library_path(pack, platform_key, path_error);
    if (!library_path.has_value()) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(path_error);
      PluginError error{
          pack.manifest.id,
          "native library selection",
          pack.root / "pack.yaml",
          path_error};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }
    record.info.library_path = library_path.value();
    if (!std::filesystem::is_regular_file(record.info.library_path)) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back("native library file does not exist");
      PluginError error{
          pack.manifest.id,
          "native library load",
          record.info.library_path,
          "native library file does not exist"};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    auto library = detail::DynamicLibrary::load(
        record.info.library_path,
        record.info.pack_id);
    if (!library) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(library.error().message);
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(library.error());
      return PluginResult::failure(library.error());
    }
    record.library = std::move(library.value());
    record.info.state = PluginState::Loaded;

    const usize plugin_index = impl_->plugins.size();
    record.call_context = std::make_unique<PluginCallContext>();
    record.call_context->impl = impl_.get();
    record.call_context->plugin_index = plugin_index;
    record.api_storage = std::make_unique<PluginAPIStorage>(record.call_context.get());

    std::string symbol_error;
    void* raw_entrypoint = record.library.symbol(record.info.entrypoint, symbol_error);
    if (raw_entrypoint == nullptr) {
      record.info.state = PluginState::Failed;
      record.info.diagnostics.push_back(symbol_error);
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          record.info.library_path,
          "entrypoint '" + record.info.entrypoint + "' was not found: " + symbol_error};
      impl_->plugins.push_back(std::move(record));
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    impl_->plugins.push_back(std::move(record));
    auto entrypoint = reinterpret_cast<ArvPluginEntrypoint>(raw_entrypoint);
    ThreadContextScope sync_scope{PluginThreadContext::Sync};
    bool ok = false;
    try {
      ok = entrypoint(&impl_->plugins[plugin_index].api_storage->api);
    } catch (const std::exception& ex) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint threw an exception: " + std::string{ex.what()}};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    } catch (...) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint threw an unknown exception"};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }
    if (!ok) {
      PluginError error{
          pack.manifest.id,
          "native entrypoint",
          impl_->plugins[plugin_index].info.library_path,
          "entrypoint returned false"};
      impl_->plugins[plugin_index].info.state = PluginState::Failed;
      impl_->diagnostics.push_back(error);
      return PluginResult::failure(std::move(error));
    }

    impl_->plugins[plugin_index].info.state = PluginState::Initialized;
    core::info(
        "plugin",
        "loaded native plugin pack={} library={}",
        impl_->plugins[plugin_index].info.pack_id,
        impl_->plugins[plugin_index].info.library_path.generic_string());
  }

  impl_->registration_open = false;
  return PluginResult::success();
}

void PluginManager::register_commands(command::CommandRegistry& registry) {
  for (const PluginCommandRegistration& registration : impl_->commands) {
    command::Command command;
    command.name = registration.command_name;
    command.domain = registration.domain;
    command.permission = registration.permission;
    command.description = registration.description;
    command.usage = registration.usage;
    command.public_to_players = registration.public_to_players;
    command.console_only = registration.console_only;
    command.execute = [impl = impl_.get(), registration](command::CommandCtx& ctx) {
      return invoke_plugin_command(*impl, registration, ctx);
    };
    auto result = registry.register_command(std::move(command));
    if (!result) {
      append_diagnostic(
          *impl_,
          impl_->plugins[registration.owner_index].info.pack_id,
          "command registration",
          {},
          result.error().message);
    }
  }
}

void PluginManager::attach_runtime(PluginRuntimeContext context) {
  impl_->runtime = context;
  if (!impl_->runtime_active && context.default_world != nullptr) {
    impl_->runtime_active = true;
    impl_->shutdown_event_dispatched = false;
    dispatch_event("arvane:event/server_start");
  }
}

void PluginManager::stop_runtime() {
  if (!impl_->runtime_active) {
    impl_->runtime = {};
    return;
  }
  if (!impl_->shutdown_event_dispatched) {
    dispatch_event("arvane:event/server_stop");
    impl_->shutdown_event_dispatched = true;
  }
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      job.canceled = true;
    }
    impl_->jobs.erase(
        std::remove_if(
            impl_->jobs.begin(),
            impl_->jobs.end(),
            [](const ScheduledJob& job) {
              return job.canceled && !job.running;
            }),
        impl_->jobs.end());
  }
  impl_->runtime = {};
  impl_->runtime_active = false;
}

void PluginManager::tick(TickIndex tick_index) {
  impl_->runtime.tick_index = tick_index;
  if (impl_->shutdown_started) {
    return;
  }

  struct JobToRun {
    u64 id{};
    usize owner_index{};
    ScheduledJobKind kind{};
    ArvPluginJobCallback callback{};
    void* user_data{};
  };
  std::vector<JobToRun> due;
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      if (job.canceled || job.running || job.next_run > tick_index) {
        continue;
      }
      due.push_back({job.id, job.owner_index, job.kind, job.callback, job.user_data});
      if (job.kind == ScheduledJobKind::Async) {
        job.running = true;
      }
      if (job.repeating) {
        job.next_run = tick_index + job.interval;
      } else {
        job.canceled = true;
      }
    }
  }

  for (const JobToRun& job : due) {
    if (job.owner_index >= impl_->plugins.size() ||
        impl_->plugins[job.owner_index].info.state != PluginState::Initialized) {
      continue;
    }
    if (job.kind == ScheduledJobKind::Sync) {
      ThreadContextScope sync_scope{PluginThreadContext::Sync};
      job.callback(job.user_data);
      continue;
    }
    if (impl_->runtime.job_system == nullptr) {
      append_diagnostic(
          *impl_,
          impl_->plugins[job.owner_index].info.pack_id,
          "scheduler",
          {},
          "async plugin job could not run because no JobSystem is attached");
      std::lock_guard lock{impl_->jobs_mutex};
      for (ScheduledJob& stored : impl_->jobs) {
        if (stored.id == job.id) {
          stored.running = false;
          stored.canceled = true;
        }
      }
      continue;
    }
    sched::JobDesc desc;
    desc.name = "plugin/" + impl_->plugins[job.owner_index].info.pack_id;
    desc.lane = sched::JobLane::BackgroundCpu;
    desc.domain = sched::JobDomain::Generic;
    sched::JobHandle handle = impl_->runtime.job_system->submit(
        std::move(desc),
        [impl = impl_.get(), job](const sched::CancellationToken& token) {
          if (!token.is_canceled()) {
            ThreadContextScope async_scope{PluginThreadContext::Async};
            job.callback(job.user_data);
          }
          std::lock_guard lock{impl->jobs_mutex};
          for (ScheduledJob& stored : impl->jobs) {
            if (stored.id == job.id) {
              stored.running = false;
              break;
            }
          }
        });
    if (!handle.valid()) {
      append_diagnostic(
          *impl_,
          impl_->plugins[job.owner_index].info.pack_id,
          "scheduler",
          {},
          "async plugin job was not accepted by JobSystem");
      std::lock_guard lock{impl_->jobs_mutex};
      for (ScheduledJob& stored : impl_->jobs) {
        if (stored.id == job.id) {
          stored.running = false;
          stored.canceled = true;
        }
      }
    }
  }

  std::lock_guard lock{impl_->jobs_mutex};
  impl_->jobs.erase(
      std::remove_if(
          impl_->jobs.begin(),
          impl_->jobs.end(),
          [](const ScheduledJob& job) {
            return job.canceled && !job.running;
          }),
      impl_->jobs.end());
}

void PluginManager::dispatch_event(std::string_view event_id) {
  std::vector<const EventSubscription*> listeners;
  for (const EventSubscription& subscription : impl_->subscriptions) {
    if (!subscription.active || subscription.event_id != event_id) {
      continue;
    }
    listeners.push_back(&subscription);
  }
  std::sort(listeners.begin(), listeners.end(), [&](const EventSubscription* lhs, const EventSubscription* rhs) {
    if (lhs->priority != rhs->priority) {
      return static_cast<int>(lhs->priority) > static_cast<int>(rhs->priority);
    }
    const usize lhs_order = impl_->plugins[lhs->owner_index].pack_order;
    const usize rhs_order = impl_->plugins[rhs->owner_index].pack_order;
    if (lhs_order != rhs_order) {
      return lhs_order < rhs_order;
    }
    return lhs->event_id < rhs->event_id;
  });

  ArvPluginEvent event;
  event.tick = impl_->runtime.tick_index;
  event.world = impl_->runtime.default_world == nullptr
                    ? 0U
                    : impl_->runtime.default_world->id().value;
  event.payload = {};
  ThreadContextScope sync_scope{PluginThreadContext::Sync};
  const std::string stable_event_id{event_id};
  event.event_id = stable_view(stable_event_id);
  for (const EventSubscription* subscription : listeners) {
    if (subscription->owner_index >= impl_->plugins.size() ||
        impl_->plugins[subscription->owner_index].info.state != PluginState::Initialized) {
      continue;
    }
    subscription->callback(&event, subscription->user_data);
  }
}

void PluginManager::shutdown() {
  if (!impl_ || impl_->shutdown_started) {
    return;
  }
  impl_->shutdown_started = true;
  stop_runtime();
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      job.canceled = true;
    }
  }
  for (ServiceRegistration& service : impl_->services) {
    service.active = false;
  }
  for (EventSubscription& subscription : impl_->subscriptions) {
    subscription.active = false;
  }
  for (PluginRecord& plugin : impl_->plugins) {
    if (plugin.info.state == PluginState::Initialized ||
        plugin.info.state == PluginState::Loaded) {
      plugin.info.state = PluginState::Shutdown;
    }
  }
}

std::vector<PluginInfo> PluginManager::plugins() const {
  std::vector<PluginInfo> infos;
  infos.reserve(impl_->plugins.size());
  for (const PluginRecord& plugin : impl_->plugins) {
    infos.push_back(plugin.info);
  }
  return infos;
}

std::span<const PluginError> PluginManager::diagnostics() const noexcept {
  return impl_->diagnostics;
}

usize PluginManager::loaded_plugin_count() const noexcept {
  usize count = 0;
  for (const PluginRecord& plugin : impl_->plugins) {
    if (plugin.info.state == PluginState::Initialized) {
      ++count;
    }
  }
  return count;
}

usize PluginManager::command_executor_count() const noexcept {
  return impl_->commands.size();
}

usize PluginManager::service_count() const noexcept {
  return static_cast<usize>(std::count_if(
      impl_->services.begin(),
      impl_->services.end(),
      [](const ServiceRegistration& service) {
        return service.active;
      }));
}

bool PluginManager::has_command_executor(std::string_view id) const noexcept {
  return std::any_of(
      impl_->commands.begin(),
      impl_->commands.end(),
      [&](const PluginCommandRegistration& command) {
        return command.executor_id == id;
      });
}

bool PluginManager::has_service(std::string_view id) const noexcept {
  return std::any_of(
      impl_->services.begin(),
      impl_->services.end(),
      [&](const ServiceRegistration& service) {
        return service.active && service.service_id == id;
      });
}

const char* plugin_state_name(PluginState state) noexcept {
  switch (state) {
    case PluginState::Discovered:
      return "Discovered";
    case PluginState::SkippedByPolicy:
      return "SkippedByPolicy";
    case PluginState::Loaded:
      return "Loaded";
    case PluginState::Initialized:
      return "Initialized";
    case PluginState::Failed:
      return "Failed";
    case PluginState::ShuttingDown:
      return "ShuttingDown";
    case PluginState::Shutdown:
      return "Shutdown";
  }
  return "Unknown";
}

std::string current_native_platform_key() {
#if defined(_WIN32)
  constexpr std::string_view os = "windows";
#elif defined(__APPLE__)
  constexpr std::string_view os = "macos";
#elif defined(__linux__)
  constexpr std::string_view os = "linux";
#else
  constexpr std::string_view os = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
  constexpr std::string_view arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  constexpr std::string_view arch = "arm64";
#else
  constexpr std::string_view arch = "unknown";
#endif
  return std::string{os} + "-" + std::string{arch};
}

bool native_library_extension_matches_platform(const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.extension() == ".dll";
#elif defined(__APPLE__)
  return path.extension() == ".dylib";
#else
  return path.extension() == ".so";
#endif
}

} // namespace arv::plugin
