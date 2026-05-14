#include "arv/plugin/plugin_manager.hpp"

#include "dynamic_library.hpp"

#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"
#include "arv/data/document.hpp"
#include "arv/data/storage.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/session_control.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
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
    ArvCommandDomain domain) noexcept {
  switch (domain) {
    case ARV_COMMAND_CLIENT_BOUND:
      return command::CommandDomain::ClientBound;
    case ARV_COMMAND_SERVER_BOUND:
      return command::CommandDomain::ServerBound;
  }
  return command::CommandDomain::ServerBound;
}

[[nodiscard]] command::CommandMessageKind command_message_kind_from_abi(
    ArvCommandMessageKind kind) noexcept {
  switch (kind) {
    case ARV_COMMAND_MESSAGE_INFO:
      return command::CommandMessageKind::Info;
    case ARV_COMMAND_MESSAGE_SUCCESS:
      return command::CommandMessageKind::Success;
    case ARV_COMMAND_MESSAGE_WARNING:
      return command::CommandMessageKind::Warning;
    case ARV_COMMAND_MESSAGE_ERROR:
      return command::CommandMessageKind::Error;
  }
  return command::CommandMessageKind::Info;
}

[[nodiscard]] player::TriState permission_value_from_abi(
    ArvPermissionValue value) noexcept {
  switch (value) {
    case ARV_PERMISSION_ALLOW:
      return player::TriState::Allow;
    case ARV_PERMISSION_DENY:
      return player::TriState::Deny;
    case ARV_PERMISSION_UNSET:
      return player::TriState::Unset;
  }
  return player::TriState::Unset;
}

[[nodiscard]] ArvPermissionValue permission_value_to_abi(
    player::TriState value) noexcept {
  switch (value) {
    case player::TriState::Allow:
      return ARV_PERMISSION_ALLOW;
    case player::TriState::Deny:
      return ARV_PERMISSION_DENY;
    case player::TriState::Unset:
      return ARV_PERMISSION_UNSET;
  }
  return ARV_PERMISSION_UNSET;
}

[[nodiscard]] world::RegionRuleValue region_rule_value_from_abi(
    ArvRegionRuleValue value) noexcept {
  switch (value) {
    case ARV_REGION_RULE_ALLOW:
      return world::RegionRuleValue::Allow;
    case ARV_REGION_RULE_DENY:
      return world::RegionRuleValue::Deny;
    case ARV_REGION_RULE_UNSET:
      return world::RegionRuleValue::Unset;
  }
  return world::RegionRuleValue::Unset;
}

[[nodiscard]] ArvRegionRuleValue region_rule_value_to_abi(
    world::RegionRuleValue value) noexcept {
  switch (value) {
    case world::RegionRuleValue::Allow:
      return ARV_REGION_RULE_ALLOW;
    case world::RegionRuleValue::Deny:
      return ARV_REGION_RULE_DENY;
    case world::RegionRuleValue::Unset:
      return ARV_REGION_RULE_UNSET;
  }
  return ARV_REGION_RULE_UNSET;
}

[[nodiscard]] int tristate_to_int(player::TriState value) noexcept {
  return static_cast<int>(value);
}

[[nodiscard]] player::TriState tristate_from_int(int value) noexcept {
  switch (value) {
    case static_cast<int>(player::TriState::Allow):
      return player::TriState::Allow;
    case static_cast<int>(player::TriState::Deny):
      return player::TriState::Deny;
    default:
      return player::TriState::Unset;
  }
}

[[nodiscard]] int region_rule_to_int(world::RegionRuleValue value) noexcept {
  return static_cast<int>(value);
}

[[nodiscard]] world::RegionRuleValue region_rule_from_int(int value) noexcept {
  switch (value) {
    case static_cast<int>(world::RegionRuleValue::Allow):
      return world::RegionRuleValue::Allow;
    case static_cast<int>(world::RegionRuleValue::Deny):
      return world::RegionRuleValue::Deny;
    default:
      return world::RegionRuleValue::Unset;
  }
}

[[nodiscard]] player::CorePermissionSet core_permissions_from_abi(
    const ArvCorePermissionSet& permissions) noexcept {
  return {
      permission_value_from_abi(permissions.can_take_damage),
      permission_value_from_abi(permissions.can_modify_world),
      permission_value_from_abi(permissions.can_interact),
      permission_value_from_abi(permissions.can_open_inventory),
      permission_value_from_abi(permissions.has_creative_inventory),
      permission_value_from_abi(permissions.can_fly),
      permission_value_from_abi(permissions.has_collision),
      permission_value_from_abi(permissions.is_visible_to_others),
  };
}

[[nodiscard]] ArvResolvedCorePermissions resolved_core_to_abi(
    const player::ResolvedCorePermissions& permissions) noexcept {
  return {
      permissions.can_take_damage,
      permissions.can_modify_world,
      permissions.can_interact,
      permissions.can_open_inventory,
      permissions.has_creative_inventory,
      permissions.can_fly,
      permissions.has_collision,
      permissions.is_visible_to_others,
  };
}

[[nodiscard]] bool resolved_core_permission_value(
    const player::ResolvedCorePermissions& permissions,
    ArvCorePermission permission) noexcept {
  switch (permission) {
    case ARV_CORE_PERMISSION_CAN_TAKE_DAMAGE:
      return permissions.can_take_damage;
    case ARV_CORE_PERMISSION_CAN_MODIFY_WORLD:
      return permissions.can_modify_world;
    case ARV_CORE_PERMISSION_CAN_INTERACT:
      return permissions.can_interact;
    case ARV_CORE_PERMISSION_CAN_OPEN_INVENTORY:
      return permissions.can_open_inventory;
    case ARV_CORE_PERMISSION_HAS_CREATIVE_INVENTORY:
      return permissions.has_creative_inventory;
    case ARV_CORE_PERMISSION_CAN_FLY:
      return permissions.can_fly;
    case ARV_CORE_PERMISSION_HAS_COLLISION:
      return permissions.has_collision;
    case ARV_CORE_PERMISSION_IS_VISIBLE_TO_OTHERS:
      return permissions.is_visible_to_others;
  }
  return false;
}

[[nodiscard]] command::CommandStatus command_status_from_result(
    ArvResultCode code) noexcept {
  switch (code) {
    case ARV_RESULT_OK:
      return command::CommandStatus::Success;
    case ARV_RESULT_PERMISSION_DENIED:
      return command::CommandStatus::PermissionDenied;
    case ARV_RESULT_INVALID_ARGUMENT:
      return command::CommandStatus::UsageError;
    case ARV_RESULT_NOT_FOUND:
      return command::CommandStatus::UnknownCommand;
    case ARV_RESULT_INVALID_STATE:
    case ARV_RESULT_DUPLICATE_ID:
    case ARV_RESULT_UNSUPPORTED_VERSION:
    case ARV_RESULT_THREAD_VIOLATION:
    case ARV_RESULT_UNSUPPORTED:
    case ARV_RESULT_INTERNAL_ERROR:
      return command::CommandStatus::Failure;
  }
  return command::CommandStatus::Failure;
}

[[nodiscard]] std::string result_code_name(ArvResultCode code) {
  switch (code) {
    case ARV_RESULT_OK:
      return "Ok";
    case ARV_RESULT_INVALID_ARGUMENT:
      return "InvalidArgument";
    case ARV_RESULT_INVALID_STATE:
      return "InvalidState";
    case ARV_RESULT_PERMISSION_DENIED:
      return "PermissionDenied";
    case ARV_RESULT_NOT_FOUND:
      return "NotFound";
    case ARV_RESULT_DUPLICATE_ID:
      return "DuplicateID";
    case ARV_RESULT_UNSUPPORTED_VERSION:
      return "UnsupportedVersion";
    case ARV_RESULT_THREAD_VIOLATION:
      return "ThreadViolation";
    case ARV_RESULT_UNSUPPORTED:
      return "Unsupported";
    case ARV_RESULT_INTERNAL_ERROR:
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

[[nodiscard]] std::filesystem::path data_relative_path_for(
    const asset::ResourceID& id,
    std::string_view extension) {
  std::filesystem::path path{id.name_space};
  path /= std::filesystem::path{id.path};
  if (!extension.empty()) {
    path += std::string{extension};
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

[[nodiscard]] ArvResult null_context_result(
    ArvResultCode code,
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
  ArvCommandCallback callback{};
  void* user_data{};
};

enum class ScheduledJobKind : u8 {
  Sync,
  Async,
};

enum class ScheduledJobTiming : u8 {
  Ticks,
  Milliseconds,
};

struct ScheduledJob {
  u64 id{};
  usize owner_index{};
  ScheduledJobKind kind{ScheduledJobKind::Sync};
  ScheduledJobTiming timing{ScheduledJobTiming::Ticks};
  ArvJobCallback callback{};
  void* user_data{};
  TickIndex next_run{};
  TickIndex interval{};
  std::chrono::steady_clock::time_point next_time{};
  std::chrono::milliseconds interval_time{};
  bool repeating{};
  bool canceled{};
  bool running{};
};

struct EventSubscription {
  u64 id{};
  usize owner_index{};
  std::string event_id{};
  ArvEventPriority priority{ARV_EVENT_PRIORITY_NORMAL};
  ArvEventCallback callback{};
  void* user_data{};
  bool active{true};
};

struct ServiceRegistration {
  u64 id{};
  usize owner_index{};
  std::string service_id{};
  u32 api_version{};
  i32 priority{};
  ArvServiceThreading threading{ARV_SERVICE_SYNC_ONLY};
  std::string display_name{};
  const void* api_table{};
  void* user_data{};
  bool active{true};
};

struct PermissionNodeRegistration {
  usize owner_index{};
  std::string node{};
  std::string description{};
  player::TriState default_value{player::TriState::Unset};
};

struct PermissionGroupRegistration {
  usize owner_index{};
  player::PermissionGroup group{};
  bool installed{};
};

struct PluginExtensionRegistration {
  usize owner_index{};
  PluginExtensionRegistryKind kind{PluginExtensionRegistryKind::EntityBehavior};
  std::string id{};
};

struct RegionFlagRegistration {
  usize owner_index{};
  std::string id{};
  std::string description{};
  world::RegionRuleValue default_value{world::RegionRuleValue::Unset};
};

struct RuntimeRegionRegistration {
  usize owner_index{};
  world::RegionID id{};
  bool active{true};
  bool persistent{};
  std::filesystem::path storage_path{};
};

struct RuntimeZoneRegistration {
  usize owner_index{};
  world::ZoneID id{};
  bool active{true};
};

struct PluginCallContext;

struct PluginAPIStorage {
  explicit PluginAPIStorage(PluginCallContext* context);

  ArvContextAPI context_api{};
  ArvLoggerAPI logger_api{};
  ArvRegistryAPI registries_api{};
  ArvCommandAPI commands_api{};
  ArvEventAPI events_api{};
  ArvSchedulerAPI scheduler_api{};
  ArvServiceAPI services_api{};
  ArvDataAPI data_api{};
  ArvResourceAPI resources_api{};
  ArvWorldAPI worlds_api{};
  ArvEntityAPI entities_api{};
  ArvConfigAPI config_api{};
  ArvPersistentDataAPI persistent_data_api{};
  ArvWorldGenAPI worldgen_api{};
  ArvNetworkAPI network_api{};
  ArvTextAPI text_api{};
  ArvMetricsAPI metrics_api{};
  ArvPermissionsAPI permissions_api{};
  ArvRegionsAPI regions_api{};
  ArvGameModeAPI game_modes_api{};
  ArvAPI api{};
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
  std::vector<PluginExtensionRegistration> extension_registrations{};
  std::vector<PermissionNodeRegistration> permission_nodes{};
  std::vector<PermissionGroupRegistration> permission_groups{};
  std::vector<RegionFlagRegistration> region_flags{};
  std::vector<RuntimeRegionRegistration> runtime_regions{};
  std::vector<RuntimeZoneRegistration> runtime_zones{};
  std::atomic<u64> next_job_id{1};
  u64 next_subscription_id{1};
  u64 next_service_id{1};
  bool persistent_regions_installed{};
  mutable std::mutex jobs_mutex;
};

namespace {

struct PluginCallContext {
  PluginManagerImpl* impl{};
  usize plugin_index{};
  std::string result_message{};
  u64 next_data_document_handle{1U};
  HashMap<u64, data::DataDocument> data_documents{};
  u64 next_text_handle{1U};
  HashMap<u64, protocol::RichText> text_components{};
  u64 next_packet_handle{1U};
  HashMap<u64, PluginNetworkPacket> network_packets{};

  [[nodiscard]] PluginInfo& info() noexcept {
    return impl->plugins[plugin_index].info;
  }

  [[nodiscard]] const PluginInfo& info() const noexcept {
    return impl->plugins[plugin_index].info;
  }
};

[[nodiscard]] ArvResult make_result(
    PluginCallContext* context,
    ArvResultCode code,
    std::string message = {}) {
  if (context == nullptr) {
    return null_context_result(code, "plugin API context is null");
  }
  context->result_message = std::move(message);
  return {code, context->result_message.c_str()};
}

[[nodiscard]] ArvResult ok(PluginCallContext* context) {
  return make_result(context, ARV_RESULT_OK);
}

[[nodiscard]] PluginCallContext* call_context(void* context) noexcept {
  return static_cast<PluginCallContext*>(context);
}

[[nodiscard]] ArvResultCode storage_result_code(data::DataStorageCode code) noexcept {
  switch (code) {
    case data::DataStorageCode::NotFound:
      return ARV_RESULT_NOT_FOUND;
    case data::DataStorageCode::InvalidArgument:
      return ARV_RESULT_INVALID_ARGUMENT;
    case data::DataStorageCode::PermissionDenied:
      return ARV_RESULT_PERMISSION_DENIED;
    case data::DataStorageCode::Unsupported:
      return ARV_RESULT_UNSUPPORTED;
    case data::DataStorageCode::Corrupt:
      return ARV_RESULT_INVALID_ARGUMENT;
    case data::DataStorageCode::InternalError:
      return ARV_RESULT_INTERNAL_ERROR;
  }
  return ARV_RESULT_INTERNAL_ERROR;
}

[[nodiscard]] ArvDataKind data_kind_to_abi(data::DataKind kind) noexcept {
  switch (kind) {
    case data::DataKind::Null:
      return ARV_DATA_NULL;
    case data::DataKind::Bool:
      return ARV_DATA_BOOL;
    case data::DataKind::I64:
      return ARV_DATA_I64;
    case data::DataKind::U64:
      return ARV_DATA_U64;
    case data::DataKind::F64:
      return ARV_DATA_F64;
    case data::DataKind::String:
      return ARV_DATA_STRING;
    case data::DataKind::Bytes:
      return ARV_DATA_BYTES;
    case data::DataKind::Array:
      return ARV_DATA_ARRAY;
    case data::DataKind::Object:
      return ARV_DATA_OBJECT;
  }
  return ARV_DATA_INVALID;
}

[[nodiscard]] data::DataDocument* document_from_handle(
    PluginCallContext* ctx,
    ArvDataDocumentHandle handle) {
  if (ctx == nullptr || handle == 0U) {
    return nullptr;
  }
  const auto found = ctx->data_documents.find(handle);
  return found == ctx->data_documents.end() ? nullptr : &found->second;
}

[[nodiscard]] const data::DataDocument* document_from_handle(
    const PluginCallContext* ctx,
    ArvDataDocumentHandle handle) {
  if (ctx == nullptr || handle == 0U) {
    return nullptr;
  }
  const auto found = ctx->data_documents.find(handle);
  return found == ctx->data_documents.end() ? nullptr : &found->second;
}

[[nodiscard]] ArvDataDocumentHandle store_document(
    PluginCallContext* ctx,
    data::DataDocument document) {
  if (ctx == nullptr) {
    return 0U;
  }
  const u64 handle = ctx->next_data_document_handle++;
  ctx->data_documents.emplace(handle, std::move(document));
  return handle;
}

[[nodiscard]] std::optional<asset::ResourceID> parse_owned_plugin_key(
    PluginCallContext* ctx,
    ArvResourceID key,
    std::string_view purpose,
    ArvResultCode& out_code,
    std::string& out_message) {
  const std::string key_text{abi_view(key)};
  auto parsed = asset::ResourceID::parse(key_text);
  if (!parsed) {
    out_code = ARV_RESULT_INVALID_ARGUMENT;
    out_message = parsed.error();
    return std::nullopt;
  }
  if (!owns_namespace(ctx->info(), parsed.value().name_space)) {
    out_code = ARV_RESULT_PERMISSION_DENIED;
    out_message = std::string{purpose} + " namespace is not owned by plugin pack";
    return std::nullopt;
  }
  return parsed.value();
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

[[nodiscard]] ArvResult require_registration_phase(
    PluginCallContext* context) {
  if (context == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (!context->impl->registration_open) {
    return make_result(
        context,
        ARV_RESULT_INVALID_STATE,
        "API may only be used during plugin registration");
  }
  return ok(context);
}

[[nodiscard]] ArvResult require_sync_context(
    PluginCallContext* context,
    std::string_view api_name) {
  if (context == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (tls_thread_context != PluginThreadContext::Sync) {
    return make_result(
        context,
        ARV_RESULT_THREAD_VIOLATION,
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

[[nodiscard]] player::PlayerState* player_from_id(
    PluginCallContext* context,
    ArvPlayerID player_id) {
  if (context == nullptr || context->impl->runtime.default_world == nullptr) {
    return nullptr;
  }
  return context->impl->runtime.default_world->find_player(PlayerID{player_id});
}

[[nodiscard]] const player::GameModeRegistry& runtime_game_modes(
    const PluginManagerImpl& impl) noexcept {
  static const player::GameModeRegistry empty_modes;
  if (impl.runtime.game_modes != nullptr) {
    return *impl.runtime.game_modes;
  }
  if (impl.options.game_modes != nullptr) {
    return *impl.options.game_modes;
  }
  return empty_modes;
}

[[nodiscard]] const player::PermissionGroupRegistry& runtime_permission_groups(
    const PluginManagerImpl& impl) noexcept {
  static const player::PermissionGroupRegistry empty_groups;
  return impl.runtime.permission_groups == nullptr ? empty_groups
                                                   : *impl.runtime.permission_groups;
}

void refresh_plugin_player_permissions(
    PluginManagerImpl& impl,
    player::PlayerState& player_state) {
  player::PermissionLayer region_layer;
  if (impl.runtime.default_world != nullptr) {
    const world::RegionQuery query{player_state.world_id, player_state.position};
    (void)impl.runtime.default_world->update_player_region_state(
        player_state.player_id,
        query);
    if (const world::PlayerRegionState* state =
            impl.runtime.default_world->player_region_state(player_state.player_id);
        state != nullptr) {
      region_layer =
          impl.runtime.default_world->regions().permission_layer_for_regions(
              state->active_regions);
    }
  }

  player_state.resolved_permissions = player::resolve_permissions(
      runtime_game_modes(impl),
      runtime_permission_groups(impl),
      {
          player_state.game_mode_id,
          &player_state.permission_overrides,
          {},
          &region_layer,
          {},
      });
}

void install_plugin_permission_groups(PluginManagerImpl& impl) {
  if (impl.runtime.permission_groups == nullptr) {
    return;
  }
  for (PermissionGroupRegistration& registration : impl.permission_groups) {
    if (registration.installed) {
      continue;
    }
    auto result =
        impl.runtime.permission_groups->add_group(registration.group);
    if (!result) {
      append_diagnostic(
          impl,
          impl.plugins[registration.owner_index].info.pack_id,
          "permission group registration",
          {},
          result.error().message);
    }
    registration.installed = true;
  }
}

[[nodiscard]] ArvByteBuffer byte_buffer_from_string(std::string text) {
  ArvByteBuffer buffer{};
  buffer.size = text.size();
  if (buffer.size == 0U) {
    return buffer;
  }
  buffer.data = new uint8_t[buffer.size];
  std::memcpy(buffer.data, text.data(), buffer.size);
  return buffer;
}

[[nodiscard]] protocol::RichText* text_from_handle(
    PluginCallContext* ctx,
    ArvTextHandle handle) {
  if (ctx == nullptr || handle.value == 0U) {
    return nullptr;
  }
  const auto found = ctx->text_components.find(handle.value);
  return found == ctx->text_components.end() ? nullptr : &found->second;
}

[[nodiscard]] ArvTextHandle store_text(
    PluginCallContext* ctx,
    protocol::RichText text) {
  if (ctx == nullptr) {
    return {};
  }
  const u64 handle = ctx->next_text_handle++;
  ctx->text_components.emplace(handle, std::move(text));
  return {handle};
}

[[nodiscard]] PluginNetworkPacket* packet_from_handle(
    PluginCallContext* ctx,
    ArvPacketHandle handle) {
  if (ctx == nullptr || handle.value == 0U) {
    return nullptr;
  }
  const auto found = ctx->network_packets.find(handle.value);
  return found == ctx->network_packets.end() ? nullptr : &found->second;
}

[[nodiscard]] ArvPacketHandle store_packet(
    PluginCallContext* ctx,
    PluginNetworkPacket packet) {
  if (ctx == nullptr) {
    return {};
  }
  const u64 handle = ctx->next_packet_handle++;
  ctx->network_packets.emplace(handle, std::move(packet));
  return {handle};
}

void apply_text_style_patch(
    protocol::TextStylePatch& result,
    ArvTextStylePatch style) {
  if (style.has_color) {
    result.color = protocol::RgbColor{style.color.r, style.color.g, style.color.b};
  }
  if (style.has_bold) {
    result.bold = style.bold;
  }
  if (style.has_italic) {
    result.italic = style.italic;
  }
  if (style.has_underlined) {
    result.underlined = style.underlined;
  }
  if (style.has_strikethrough) {
    result.strikethrough = style.strikethrough;
  }
  if (style.has_obfuscated) {
    result.obfuscated = style.obfuscated;
  }
  if (style.has_font) {
    result.font = abi_string(style.font);
  }
}

[[nodiscard]] std::optional<protocol::ChatMessageKind> chat_message_kind_from_abi(
    ArvChatMessageKind kind) noexcept {
  switch (kind) {
    case ARV_CHAT_MESSAGE_CHAT:
      return protocol::ChatMessageKind::Chat;
    case ARV_CHAT_MESSAGE_SYSTEM:
      return protocol::ChatMessageKind::System;
    case ARV_CHAT_MESSAGE_ANNOUNCEMENT:
      return protocol::ChatMessageKind::Announcement;
    case ARV_CHAT_MESSAGE_PRIVATE:
      return protocol::ChatMessageKind::PrivateMessage;
    case ARV_CHAT_MESSAGE_ERROR:
      return protocol::ChatMessageKind::Error;
  }
  return std::nullopt;
}

[[nodiscard]] protocol::ClickAction click_action_from_abi(
    ArvTextClickAction action) noexcept {
  switch (action) {
    case ARV_TEXT_CLICK_OPEN_URL:
      return protocol::ClickAction::OpenUrl;
    case ARV_TEXT_CLICK_SUGGEST_COMMAND:
      return protocol::ClickAction::SuggestCommand;
    case ARV_TEXT_CLICK_RUN_COMMAND:
      return protocol::ClickAction::RunCommand;
    case ARV_TEXT_CLICK_COPY_TO_CLIPBOARD:
      return protocol::ClickAction::CopyToClipboard;
  }
  return protocol::ClickAction::SuggestCommand;
}

[[nodiscard]] protocol::RichTextLimits text_limits_from_abi(
    const ArvTextLimits* limits) noexcept {
  protocol::RichTextLimits result;
  if (limits == nullptr) {
    return result;
  }
  result.max_serialized_size_bytes =
      static_cast<usize>(limits->max_serialized_size_bytes);
  result.max_text_bytes = static_cast<usize>(limits->max_text_bytes);
  result.max_nodes = static_cast<usize>(limits->max_nodes);
  result.max_depth = static_cast<usize>(limits->max_depth);
  result.max_hover_text_bytes =
      static_cast<usize>(limits->max_hover_text_bytes);
  result.max_click_value_bytes =
      static_cast<usize>(limits->max_click_value_bytes);
  return result;
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

ArvResult api_register_block_behavior(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  const std::string_view behavior_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), behavior_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.block_behaviors == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_UNSUPPORTED,
        "block behavior registry is unavailable");
  }
  auto result = ctx->impl->options.block_behaviors->register_handler(std::string{behavior_id});
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_RESULT_DUPLICATE_ID
            : ARV_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

ArvResult api_find_block_variant(
    void* context,
    ArvResourceID id,
    uint32_t* out_block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (out_block_variant == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_block_variant is null");
  }
  *out_block_variant = 0U;
  if (ctx == nullptr || ctx->impl->runtime.block_registry == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "block registry is unavailable");
  }
  const std::string name{abi_view(id)};
  auto found = ctx->impl->runtime.block_registry->find_variant(name);
  if (!found.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "block variant not found: " + name);
  }
  *out_block_variant = found->value;
  return ok(ctx);
}

ArvResult api_block_variant_name(
    void* context,
    uint32_t block_variant,
    ArvStringView* out_name) {
  PluginCallContext* ctx = call_context(context);
  if (out_name == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_name is null");
  }
  *out_name = {};
  if (ctx == nullptr || ctx->impl->runtime.block_registry == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "block registry is unavailable");
  }
  const auto* variant =
      ctx->impl->runtime.block_registry->variant(voxel::BlockVariantID{block_variant});
  if (variant == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "block variant not found");
  }
  *out_name = stable_view(variant->debug_name);
  return ok(ctx);
}

ArvResult api_air_block_variant(void* context, uint32_t* out_block_variant) {
  PluginCallContext* ctx = call_context(context);
  if (out_block_variant == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_block_variant is null");
  }
  if (ctx == nullptr || ctx->impl->runtime.block_registry == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "block registry is unavailable");
  }
  *out_block_variant = ctx->impl->runtime.block_registry->air_variant_id().value;
  return ok(ctx);
}

bool api_is_air_block_variant(void* context, uint32_t block_variant) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr && ctx->impl->runtime.block_registry != nullptr &&
         ctx->impl->runtime.block_registry->is_air(voxel::BlockVariantID{block_variant});
}

ArvResult api_register_item_behavior(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  const std::string_view behavior_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), behavior_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.item_behaviors == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_UNSUPPORTED,
        "item behavior registry is unavailable");
  }
  auto result = ctx->impl->options.item_behaviors->register_handler(std::string{behavior_id});
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_RESULT_DUPLICATE_ID
            : ARV_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

ArvResult api_register_entity_goal(void* context, ArvResourceID id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  const std::string_view goal_id = abi_view(id);
  if (auto error = validate_owned_resource_id(ctx->info(), goal_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (ctx->impl->options.entity_goals == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_UNSUPPORTED,
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
            ? ARV_RESULT_DUPLICATE_ID
            : ARV_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

[[nodiscard]] const char* extension_kind_name(
    PluginExtensionRegistryKind kind) noexcept {
  switch (kind) {
    case PluginExtensionRegistryKind::EntityBehavior:
      return "entity behavior";
    case PluginExtensionRegistryKind::ComponentCodec:
      return "component codec";
    case PluginExtensionRegistryKind::LootFunction:
      return "loot function";
    case PluginExtensionRegistryKind::RecipeType:
      return "recipe type";
    case PluginExtensionRegistryKind::WorldgenFeature:
      return "worldgen feature";
    case PluginExtensionRegistryKind::WorldgenNoise:
      return "worldgen noise";
    case PluginExtensionRegistryKind::WorldgenDensityFunction:
      return "worldgen density function";
  }
  return "extension";
}

ArvResult register_extension_marker(
    void* context,
    ArvResourceID id,
    PluginExtensionRegistryKind kind) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  const std::string marker_id{abi_view(id)};
  if (auto error = validate_owned_resource_id(ctx->info(), marker_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  const auto duplicate = std::find_if(
      ctx->impl->extension_registrations.begin(),
      ctx->impl->extension_registrations.end(),
      [&](const PluginExtensionRegistration& registration) {
        return registration.kind == kind && registration.id == marker_id;
      });
  if (duplicate != ctx->impl->extension_registrations.end()) {
    return make_result(
        ctx,
        ARV_RESULT_DUPLICATE_ID,
        std::string{"duplicate "} + extension_kind_name(kind) + " id: " +
            marker_id);
  }
  ctx->impl->extension_registrations.push_back({
      ctx->plugin_index,
      kind,
      marker_id,
  });
  return ok(ctx);
}

ArvResult api_register_entity_behavior(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::EntityBehavior);
}

ArvResult api_register_component_codec(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::ComponentCodec);
}

ArvResult api_register_loot_function(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::LootFunction);
}

ArvResult api_register_recipe_type(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::RecipeType);
}

ArvResult api_register_executor(
    void* context,
    const ArvCommandDescriptor* descriptor) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (descriptor == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "command descriptor is null");
  }
  if (descriptor->execute == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "command descriptor execute callback is null");
  }
  const std::string executor_id{abi_view(descriptor->executor_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), executor_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  const std::string command_name{abi_view(descriptor->command_name)};
  if (!command_name_valid(command_name)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
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
        ARV_RESULT_DUPLICATE_ID,
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

ArvResult schedule_job(
    PluginCallContext* ctx,
    ScheduledJobKind kind,
    TickIndex delay,
    TickIndex interval,
    bool repeating,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "scheduled job callback is null");
  }
  if (repeating && interval == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "repeating scheduled job interval must be non-zero");
  }
  if (ctx->impl->shutdown_started) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
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
        ScheduledJobTiming::Ticks,
        callback,
        user_data,
        next_run,
        interval,
        {},
        {},
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

ArvResult schedule_millisecond_job(
    PluginCallContext* ctx,
    ScheduledJobKind kind,
    ArvMilliseconds delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    bool repeating,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "scheduled job callback is null");
  }
  if (repeating && interval_milliseconds == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "repeating scheduled job interval must be non-zero");
  }
  if (ctx->impl->shutdown_started) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "plugin manager is shutting down");
  }

  const auto now = std::chrono::steady_clock::now();
  const auto delay = std::chrono::milliseconds{delay_milliseconds};
  const auto interval = std::chrono::milliseconds{interval_milliseconds};
  const u64 id = ctx->impl->next_job_id.fetch_add(1U, std::memory_order_relaxed);
  {
    std::lock_guard lock{ctx->impl->jobs_mutex};
    ctx->impl->jobs.push_back({
        id,
        ctx->plugin_index,
        kind,
        ScheduledJobTiming::Milliseconds,
        callback,
        user_data,
        0U,
        0U,
        now + delay,
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

ArvResult api_sync_now(
    void* context,
    ArvJobCallback callback,
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

ArvResult api_sync_delayed(
    void* context,
    ArvMilliseconds delay_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Sync,
      delay_milliseconds,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_repeating(
    void* context,
    ArvMilliseconds start_delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Sync,
      start_delay_milliseconds,
      interval_milliseconds,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_now(
    void* context,
    ArvJobCallback callback,
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

ArvResult api_async_delayed(
    void* context,
    ArvMilliseconds delay_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Async,
      delay_milliseconds,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_repeating(
    void* context,
    ArvMilliseconds start_delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Async,
      start_delay_milliseconds,
      interval_milliseconds,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_delayed_tick(
    void* context,
    ArvTickIndex delay_ticks,
    ArvJobCallback callback,
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

ArvResult api_sync_repeating_tick(
    void* context,
    ArvTickIndex start_delay_ticks,
    ArvTickIndex interval_ticks,
    ArvJobCallback callback,
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

ArvResult api_async_delayed_tick(
    void* context,
    ArvTickIndex delay_ticks,
    ArvJobCallback callback,
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

ArvResult api_async_repeating_tick(
    void* context,
    ArvTickIndex start_delay_ticks,
    ArvTickIndex interval_ticks,
    ArvJobCallback callback,
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

ArvResult api_cancel_job(void* context, ArvScheduledJobHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  std::lock_guard lock{ctx->impl->jobs_mutex};
  for (ScheduledJob& job : ctx->impl->jobs) {
    if (job.id == handle.value && job.owner_index == ctx->plugin_index) {
      job.canceled = true;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "scheduled job not found");
}

ArvResult api_subscribe_event(
    void* context,
    ArvResourceID event_id,
    ArvEventPriority priority,
    ArvEventCallback callback,
    void* user_data,
    ArvEventSubscriptionHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "event callback is null");
  }
  const std::string id{abi_view(event_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
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

ArvResult api_unsubscribe_event(
    void* context,
    ArvEventSubscriptionHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
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
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "event subscription not found");
}

ArvResult api_publish_service(
    void* context,
    const ArvServiceDescriptor* descriptor,
    ArvServiceHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (descriptor == nullptr || descriptor->api_table == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "service descriptor or API table is null");
  }
  if (descriptor->api_version == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "service API version must be non-zero");
  }
  const std::string id{abi_view(descriptor->service_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
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

ArvResult api_unpublish_service(void* context, ArvServiceHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
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
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "service not found");
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

[[nodiscard]] ArvServiceRef service_ref_for(
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

ArvResult api_get_service(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvServiceRef* out_service) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_service == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "out_service is null");
  }
  const std::string id{abi_view(service_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::vector<const ServiceRegistration*> matches =
      matching_services(*ctx->impl, id, min_api_version, max_api_version);
  if (matches.empty()) {
    *out_service = {};
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "service not found");
  }
  *out_service = service_ref_for(*ctx->impl, *matches.front());
  return ok(ctx);
}

uint64_t api_list_services(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvServiceRef* out_services,
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

void api_release_service(void* context, ArvServiceRef service) {
  (void)context;
  (void)service;
}

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

[[nodiscard]] std::filesystem::path plugin_data_root(PluginCallContext* ctx) {
  if (ctx == nullptr || ctx->impl->runtime.server_save_root.empty()) {
    return {};
  }
  return ctx->impl->runtime.server_save_root / "plugin_data" / ctx->info().pack_id;
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

ArvResult api_text_literal(
    void* context,
    ArvStringView text,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  const std::string value{abi_view(text)};
  if (!protocol::is_valid_utf8(value)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "text is not valid UTF-8");
  }
  *out_text = store_text(ctx, protocol::plain_rich_text(value));
  return ok(ctx);
}

ArvResult api_text_translatable(
    void* context,
    ArvStringView translation_key,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  const std::string key{abi_view(translation_key)};
  if (key.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "translation key is empty");
  }
  if (key.size() > protocol::MaxRichTextTranslationKeyBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "translation key is too long");
  }
  if (!protocol::is_valid_utf8(key)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "translation key is not valid UTF-8");
  }
  *out_text = store_text(ctx, protocol::translatable_rich_text(key));
  return ok(ctx);
}

ArvResult api_text_append(
    void* context,
    ArvTextHandle parent,
    ArvTextHandle child,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  protocol::RichText* parent_text = text_from_handle(ctx, parent);
  protocol::RichText* child_text = text_from_handle(ctx, child);
  if (parent_text == nullptr || child_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  protocol::RichText copy = *parent_text;
  copy.children.push_back(*child_text);
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_style(
    void* context,
    ArvTextHandle text,
    ArvTextStylePatch style,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  protocol::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  protocol::RichText copy = *source;
  apply_text_style_patch(copy.style, style);
  const protocol::RichTextValidation validation = protocol::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_color(
    void* context,
    ArvTextHandle text,
    ArvTextColor color,
    ArvTextHandle* out_text) {
  ArvTextStylePatch style{};
  style.has_color = true;
  style.color = color;
  return api_text_style(context, text, style, out_text);
}

void apply_hover_to_text(protocol::RichText& text, protocol::HoverEvent hover) {
  bool applied = false;
  if (!text.text.empty()) {
    text.hover_event = hover;
    applied = true;
  }
  for (protocol::RichText& child : text.children) {
    apply_hover_to_text(child, hover);
    applied = true;
  }
  if (!applied) {
    text.hover_event = std::move(hover);
  }
}

void apply_click_to_text(protocol::RichText& text, protocol::ClickEvent click) {
  bool applied = false;
  if (!text.text.empty()) {
    text.click_event = click;
    applied = true;
  }
  for (protocol::RichText& child : text.children) {
    apply_click_to_text(child, click);
    applied = true;
  }
  if (!applied) {
    text.click_event = std::move(click);
  }
}

ArvResult api_text_hover_text(
    void* context,
    ArvTextHandle text,
    ArvTextHandle hover_text,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  protocol::RichText* source = text_from_handle(ctx, text);
  protocol::RichText* hover_source = text_from_handle(ctx, hover_text);
  if (source == nullptr || hover_source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  protocol::RichText copy = *source;
  apply_hover_to_text(copy, protocol::make_show_text_hover(*hover_source));
  const protocol::RichTextValidation validation = protocol::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_click(
    void* context,
    ArvTextHandle text,
    ArvTextClickAction action,
    ArvStringView value,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  protocol::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  protocol::RichText copy = *source;
  apply_click_to_text(
      copy,
      protocol::ClickEvent{click_action_from_abi(action), abi_string(value)});
  const protocol::RichTextValidation validation = protocol::validate_rich_text(copy);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  *out_text = store_text(ctx, std::move(copy));
  return ok(ctx);
}

ArvResult api_text_plain_text(
    void* context,
    ArvTextHandle text,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  protocol::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  *out_buffer = byte_buffer_from_string(protocol::to_plain_text(*source));
  return ok(ctx);
}

ArvResult api_text_serialize(
    void* context,
    ArvTextHandle text,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  protocol::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const protocol::RichTextValidation validation = protocol::validate_rich_text(*source);
  if (!validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
  }
  const std::vector<u8> bytes = protocol::serialize_rich_text(*source);
  std::string serialized;
  serialized.resize(bytes.size());
  if (!bytes.empty()) {
    std::memcpy(serialized.data(), bytes.data(), bytes.size());
  }
  *out_buffer = byte_buffer_from_string(std::move(serialized));
  return ok(ctx);
}

ArvResult api_text_validate(
    void* context,
    ArvTextHandle text,
    const ArvTextLimits* limits) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  protocol::RichText* source = text_from_handle(ctx, text);
  if (source == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const protocol::RichTextValidation validation =
      protocol::validate_rich_text(*source, text_limits_from_abi(limits));
  return validation.valid
             ? ok(ctx)
             : make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, validation.message);
}

void api_text_release(void* context, ArvTextHandle text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx != nullptr && text.value != 0U) {
    ctx->text_components.erase(text.value);
  }
}

bool permission_node_owned_by_plugin(
    const PluginInfo& info,
    std::string_view node) {
  for (const std::string& name_space : info.namespaces) {
    if (node == name_space ||
        (node.size() > name_space.size() &&
         node.substr(0, name_space.size()) == name_space &&
         node[name_space.size()] == '.')) {
      return true;
    }
  }
  return false;
}

ArvResult api_register_permission_node(
    void* context,
    const ArvPermissionNodeDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission node definition is null");
  }
  const std::string node{abi_view(definition->node)};
  if (!player::is_valid_custom_permission_node(node)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
  }
  if (!permission_node_owned_by_plugin(ctx->info(), node)) {
    return make_result(
        ctx,
        ARV_RESULT_PERMISSION_DENIED,
        "permission node is not owned by plugin pack");
  }
  const auto duplicate = std::find_if(
      ctx->impl->permission_nodes.begin(),
      ctx->impl->permission_nodes.end(),
      [&](const PermissionNodeRegistration& registration) {
        return registration.node == node;
      });
  if (duplicate != ctx->impl->permission_nodes.end()) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate permission node");
  }
  ctx->impl->permission_nodes.push_back({
      ctx->plugin_index,
      node,
      abi_string(definition->description),
      permission_value_from_abi(definition->default_value),
  });
  return ok(ctx);
}

ArvResult api_permission_check_core(
    void* context,
    ArvPlayerID player,
    ArvCorePermission permission,
    bool* out_allowed) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.check_core");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_allowed == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_allowed is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_allowed =
      resolved_core_permission_value(player_state->resolved_permissions.core, permission);
  return ok(ctx);
}

ArvResult api_permission_check_custom(
    void* context,
    ArvPlayerID player,
    ArvStringView node,
    ArvPermissionValue* out_value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.check_custom");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_value == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_value is null");
  }
  const std::string node_text{abi_view(node)};
  if (!player::is_valid_custom_permission_node(node_text)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_value = permission_value_to_abi(
      player_state->resolved_permissions.custom_value(node_text));
  return ok(ctx);
}

ArvResult api_permission_resolved_core(
    void* context,
    ArvPlayerID player,
    ArvResolvedCorePermissions* out_permissions) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.resolved_core");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_permissions == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_permissions is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  *out_permissions = resolved_core_to_abi(player_state->resolved_permissions.core);
  return ok(ctx);
}

ArvResult api_permission_register_group(
    void* context,
    const ArvPermissionGroupDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "permission group definition is null");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (definition->inherit_count != 0U && definition->inherits == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "permission group inherits is null");
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "permission group custom permissions is null");
  }
  const auto duplicate = std::find_if(
      ctx->impl->permission_groups.begin(),
      ctx->impl->permission_groups.end(),
      [&](const PermissionGroupRegistration& registration) {
        return registration.group.id == id;
      });
  if (duplicate != ctx->impl->permission_groups.end()) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate permission group");
  }

  player::PermissionGroup group;
  group.id = id;
  group.priority = definition->priority;
  group.core = core_permissions_from_abi(definition->core);
  group.inherits.reserve(definition->inherit_count);
  for (usize index = 0; index < definition->inherit_count; ++index) {
    group.inherits.push_back(abi_string(definition->inherits[index]));
  }
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    group.custom[node] = permission_value_from_abi(entry.value);
  }
  ctx->impl->permission_groups.push_back({ctx->plugin_index, std::move(group), false});
  return ok(ctx);
}

ArvResult api_permission_has_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id,
    bool* out_has_group) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.has_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_has_group == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_has_group is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  const std::string id{abi_view(group_id)};
  *out_has_group = std::find(
                       player_state->permission_overrides.groups.begin(),
                       player_state->permission_overrides.groups.end(),
                       id) != player_state->permission_overrides.groups.end();
  return ok(ctx);
}

ArvResult api_permission_add_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.add_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string id{abi_view(group_id)};
  if (runtime_permission_groups(*ctx->impl).find(id) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "permission group not found");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  auto& groups = player_state->permission_overrides.groups;
  if (std::find(groups.begin(), groups.end(), id) == groups.end()) {
    groups.push_back(id);
    refresh_plugin_player_permissions(*ctx->impl, *player_state);
  }
  return ok(ctx);
}

ArvResult api_permission_remove_group(
    void* context,
    ArvPlayerID player,
    ArvStringView group_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "permissions.remove_group");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  const std::string id{abi_view(group_id)};
  auto& groups = player_state->permission_overrides.groups;
  const usize old_size = groups.size();
  groups.erase(std::remove(groups.begin(), groups.end(), id), groups.end());
  if (groups.size() != old_size) {
    refresh_plugin_player_permissions(*ctx->impl, *player_state);
  }
  return ok(ctx);
}

uint64_t api_permission_groups(
    void* context,
    ArvPlayerID player,
    ArvStringView* out_groups,
    uint64_t max_groups) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync) {
    return 0U;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return 0U;
  }
  const std::vector<std::string>& groups = player_state->permission_overrides.groups;
  if (out_groups != nullptr && max_groups > 0U) {
    const uint64_t count = std::min<uint64_t>(groups.size(), max_groups);
    for (uint64_t index = 0; index < count; ++index) {
      out_groups[index] = stable_view(groups[index]);
    }
  }
  return groups.size();
}

[[nodiscard]] const RegionFlagRegistration* find_region_flag_registration(
    const PluginManagerImpl& impl,
    std::string_view id) noexcept {
  const auto found = std::find_if(
      impl.region_flags.begin(),
      impl.region_flags.end(),
      [&](const RegionFlagRegistration& registration) {
        return registration.id == id;
      });
  return found == impl.region_flags.end() ? nullptr : &*found;
}

ArvResult api_register_region_flag(
    void* context,
    const ArvRegionFlagDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "region flag definition is null");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (find_region_flag_registration(*ctx->impl, id) != nullptr) {
    return make_result(ctx, ARV_RESULT_DUPLICATE_ID, "duplicate region flag");
  }
  ctx->impl->region_flags.push_back({
      ctx->plugin_index,
      id,
      abi_string(definition->description),
      region_rule_value_from_abi(definition->default_value),
  });
  return ok(ctx);
}

void fill_region_info(
    const world::Region& region,
    ArvRegionInfo& out_region) noexcept {
  out_region = {
      ArvRegionHandle{region.id.value},
      stable_view(region.name),
      region.world.value,
      region.priority,
  };
}

[[nodiscard]] std::optional<std::filesystem::path> persistent_region_path(
    PluginCallContext* ctx,
    std::string_view region_id,
    std::string& error_message) {
  auto parsed = asset::ResourceID::parse(region_id);
  if (!parsed) {
    error_message = parsed.error();
    return std::nullopt;
  }
  if (!owns_namespace(ctx->info(), parsed.value().name_space)) {
    error_message = "region namespace is not owned by plugin pack";
    return std::nullopt;
  }
  const std::filesystem::path root = plugin_data_root(ctx);
  if (root.empty()) {
    error_message = "plugin persistent data root is unavailable";
    return std::nullopt;
  }
  return root / "regions" / data_relative_path_for(parsed.value(), ".region");
}

[[nodiscard]] world::Region make_region_from_definition(
    const ArvRegionDefinition& definition,
    std::string id) {
  world::Region region;
  region.name = std::move(id);
  region.world = WorldID{static_cast<u32>(definition.world)};
  region.priority = definition.priority;
  region.shape = {
      world::RegionShapeType::Aabb,
      {definition.shape.aabb.min.x, definition.shape.aabb.min.y, definition.shape.aabb.min.z},
      {definition.shape.aabb.max.x, definition.shape.aabb.max.y, definition.shape.aabb.max.z},
  };
  region.core = core_permissions_from_abi(definition.core);
  return region;
}

ArvResult persist_region_to_file(
    PluginCallContext* ctx,
    const world::Region& region,
    const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return make_result(
        ctx,
        ARV_RESULT_INTERNAL_ERROR,
        "failed to create persistent region directory: " + error.message());
  }
  static std::atomic<u64> region_temp_counter{1U};
  const std::filesystem::path temp =
      path.string() + ".tmp." +
      std::to_string(region_temp_counter.fetch_add(1U, std::memory_order_relaxed));
  {
    std::ofstream output(temp, std::ios::trunc);
    if (!output) {
      return make_result(
          ctx,
          ARV_RESULT_INTERNAL_ERROR,
          "failed to open persistent region temp file");
    }
    output << "version 1\n";
    output << "id " << region.name << "\n";
    output << "world " << region.world.value << "\n";
    output << "priority " << region.priority << "\n";
    output << "aabb " << region.shape.min.x << ' ' << region.shape.min.y << ' '
           << region.shape.min.z << ' ' << region.shape.max.x << ' '
           << region.shape.max.y << ' ' << region.shape.max.z << "\n";
    output << "zones 0\n";
    output << "core "
           << tristate_to_int(region.core.can_take_damage) << ' '
           << tristate_to_int(region.core.can_modify_world) << ' '
           << tristate_to_int(region.core.can_interact) << ' '
           << tristate_to_int(region.core.can_open_inventory) << ' '
           << tristate_to_int(region.core.has_creative_inventory) << ' '
           << tristate_to_int(region.core.can_fly) << ' '
           << tristate_to_int(region.core.has_collision) << ' '
           << tristate_to_int(region.core.is_visible_to_others) << "\n";
    output << "custom " << region.permissions.size() << "\n";
    for (const auto& [node, value] : region.permissions) {
      output << node << ' ' << tristate_to_int(value) << "\n";
    }
    output << "flags " << region.flags.size() << "\n";
    for (const auto& [flag, value] : region.flags) {
      output << flag << ' ' << region_rule_to_int(value) << "\n";
    }
    if (!output) {
      return make_result(
          ctx,
          ARV_RESULT_INTERNAL_ERROR,
          "failed to write persistent region file");
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp, cleanup_error);
    return make_result(
        ctx,
        ARV_RESULT_INTERNAL_ERROR,
        "failed to publish persistent region file: " + error.message());
  }
  return ok(ctx);
}

[[nodiscard]] std::optional<world::Region> load_persistent_region_file(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }
  std::string label;
  int version = 0;
  if (!(input >> label >> version) || label != "version" || version != 1) {
    return std::nullopt;
  }
  world::Region region;
  u32 world_value = 0U;
  if (!(input >> label >> region.name) || label != "id") {
    return std::nullopt;
  }
  if (!(input >> label >> world_value) || label != "world") {
    return std::nullopt;
  }
  region.world = WorldID{world_value};
  if (!(input >> label >> region.priority) || label != "priority") {
    return std::nullopt;
  }
  if (!(input >> label >> region.shape.min.x >> region.shape.min.y >>
        region.shape.min.z >> region.shape.max.x >> region.shape.max.y >>
        region.shape.max.z) ||
      label != "aabb") {
    return std::nullopt;
  }
  region.shape.type = world::RegionShapeType::Aabb;
  usize zone_count = 0U;
  if (!(input >> label >> zone_count) || label != "zones") {
    return std::nullopt;
  }
  for (usize index = 0; index < zone_count; ++index) {
    std::string ignored;
    input >> ignored;
  }
  int core_values[8]{};
  if (!(input >> label >> core_values[0] >> core_values[1] >> core_values[2] >>
        core_values[3] >> core_values[4] >> core_values[5] >> core_values[6] >>
        core_values[7]) ||
      label != "core") {
    return std::nullopt;
  }
  region.core = {
      tristate_from_int(core_values[0]),
      tristate_from_int(core_values[1]),
      tristate_from_int(core_values[2]),
      tristate_from_int(core_values[3]),
      tristate_from_int(core_values[4]),
      tristate_from_int(core_values[5]),
      tristate_from_int(core_values[6]),
      tristate_from_int(core_values[7]),
  };
  usize custom_count = 0U;
  if (!(input >> label >> custom_count) || label != "custom") {
    return std::nullopt;
  }
  for (usize index = 0; index < custom_count; ++index) {
    std::string node;
    int value = 0;
    if (!(input >> node >> value)) {
      return std::nullopt;
    }
    region.permissions[node] = tristate_from_int(value);
  }
  usize flag_count = 0U;
  if (!(input >> label >> flag_count) || label != "flags") {
    return std::nullopt;
  }
  for (usize index = 0; index < flag_count; ++index) {
    std::string flag;
    int value = 0;
    if (!(input >> flag >> value)) {
      return std::nullopt;
    }
    region.flags[flag] = region_rule_from_int(value);
  }
  return region;
}

void install_persistent_regions(PluginManagerImpl& impl) {
  if (impl.persistent_regions_installed || impl.runtime.default_world == nullptr ||
      impl.runtime.server_save_root.empty()) {
    return;
  }
  for (usize plugin_index = 0; plugin_index < impl.plugins.size(); ++plugin_index) {
    PluginRecord& plugin = impl.plugins[plugin_index];
    if (plugin.info.state != PluginState::Initialized) {
      continue;
    }
    const std::filesystem::path root =
        impl.runtime.server_save_root / "plugin_data" / plugin.info.pack_id / "regions";
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
      continue;
    }
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         !error && it != end;
         it.increment(error)) {
      if (!it->is_regular_file(error) || it->path().extension() != ".region") {
        continue;
      }
      auto region = load_persistent_region_file(it->path());
      if (!region.has_value()) {
        append_diagnostic(
            impl,
            plugin.info.pack_id,
            "persistent region load",
            it->path(),
            "persistent region file is invalid");
        continue;
      }
      if (auto id_error = validate_owned_resource_id(plugin.info, region->name);
          id_error.has_value()) {
        append_diagnostic(
            impl,
            plugin.info.pack_id,
            "persistent region load",
            it->path(),
            id_error.value());
        continue;
      }
      if (region->world != impl.runtime.default_world->id()) {
        continue;
      }
      const world::RegionID id =
          impl.runtime.default_world->regions().add_region(std::move(region.value()));
      if (id.value == 0U) {
        append_diagnostic(
            impl,
            plugin.info.pack_id,
            "persistent region load",
            it->path(),
            "persistent region could not be added");
        continue;
      }
      impl.runtime_regions.push_back({
          plugin_index,
          id,
          true,
          true,
          it->path(),
      });
    }
  }
  impl.persistent_regions_installed = true;
}

ArvResult api_create_runtime_region(
    void* context,
    const ArvRegionDefinition* definition,
    ArvRegionHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.create_runtime_region");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (definition == nullptr || out_handle == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "region definition or out_handle is null");
  }
  *out_handle = {};
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::filesystem::path persistent_path;
  if (definition->persistent) {
    std::string path_error;
    auto path = persistent_region_path(ctx, id, path_error);
    if (!path.has_value()) {
      return make_result(ctx, ARV_RESULT_INVALID_STATE, path_error);
    }
    persistent_path = path.value();
  }
  world::WorldRuntime* world = world_from_handle(ctx, definition->world);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  if (definition->shape.kind != ARV_REGION_SHAPE_AABB) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "unsupported region shape");
  }
  if (definition->zone_count != 0U && definition->zones == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "region zones is null");
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "region custom permissions is null");
  }

  world::Region region = make_region_from_definition(*definition, id);
  for (usize index = 0; index < definition->zone_count; ++index) {
    const std::string zone_id{abi_view(definition->zones[index])};
    const world::Zone* zone = world->regions().zone(zone_id);
    if (zone == nullptr) {
      return make_result(ctx, ARV_RESULT_NOT_FOUND, "region zone not found: " + zone_id);
    }
    region.zones.push_back(zone->id);
  }
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    region.permissions[node] = permission_value_from_abi(entry.value);
  }

  world::Region persisted_region = region;
  const world::RegionID id_value = world->regions().add_region(std::move(region));
  if (id_value.value == 0U) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "region could not be added");
  }
  if (definition->persistent) {
    if (ArvResult persisted =
            persist_region_to_file(ctx, persisted_region, persistent_path);
        persisted.code != ARV_RESULT_OK) {
      (void)world->regions().remove_region(id_value);
      return persisted;
    }
  }
  ctx->impl->runtime_regions.push_back({
      ctx->plugin_index,
      id_value,
      true,
      definition->persistent,
      persistent_path,
  });
  *out_handle = {id_value.value};
  return ok(ctx);
}

ArvResult api_remove_runtime_region(void* context, ArvRegionHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.remove_runtime_region");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  for (RuntimeRegionRegistration& registration : ctx->impl->runtime_regions) {
    if (registration.id.value == handle.value &&
        registration.owner_index == ctx->plugin_index &&
        registration.active) {
      registration.active = false;
      const bool removed =
          ctx->impl->runtime.default_world->regions().remove_region(registration.id);
      if (!removed) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "region not found");
      }
      if (registration.persistent && !registration.storage_path.empty()) {
        std::error_code error;
        std::filesystem::remove(registration.storage_path, error);
        if (error) {
          return make_result(
              ctx,
              ARV_RESULT_INTERNAL_ERROR,
              "failed to remove persistent region file: " + error.message());
        }
      }
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "runtime region not found");
}

ArvResult api_register_runtime_zone(
    void* context,
    const ArvZoneDefinition* definition,
    ArvRegionHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.register_runtime_zone");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (definition == nullptr || out_handle == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "zone definition or out_handle is null");
  }
  *out_handle = {};
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "zone custom permissions is null");
  }
  world::Zone zone;
  zone.name = id;
  zone.priority = definition->priority;
  zone.core = core_permissions_from_abi(definition->core);
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    zone.permissions[node] = permission_value_from_abi(entry.value);
  }
  const world::ZoneID id_value =
      ctx->impl->runtime.default_world->regions().add_zone(std::move(zone));
  if (id_value.value == 0U) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "zone could not be added");
  }
  ctx->impl->runtime_zones.push_back({ctx->plugin_index, id_value, true});
  *out_handle = {id_value.value};
  return ok(ctx);
}

ArvResult api_remove_runtime_zone(void* context, ArvRegionHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.remove_runtime_zone");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  for (RuntimeZoneRegistration& registration : ctx->impl->runtime_zones) {
    if (registration.id.value == handle.value &&
        registration.owner_index == ctx->plugin_index &&
        registration.active) {
      registration.active = false;
      return ctx->impl->runtime.default_world->regions().remove_zone(registration.id)
                 ? ok(ctx)
                 : make_result(ctx, ARV_RESULT_NOT_FOUND, "zone not found");
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "runtime zone not found");
}

uint64_t api_find_regions_at(
    void* context,
    const ArvRegionQuery* query,
    ArvRegionInfo* out_regions,
    uint64_t max_regions) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || query == nullptr ||
      tls_thread_context != PluginThreadContext::Sync) {
    return 0U;
  }
  world::WorldRuntime* world = world_from_handle(ctx, query->world);
  if (world == nullptr) {
    return 0U;
  }
  const std::vector<world::RegionID> ids = world->regions().find_regions_at({
      WorldID{static_cast<u32>(query->world)},
      {query->position.x, query->position.y, query->position.z},
  });
  if (out_regions != nullptr && max_regions > 0U) {
    const uint64_t count = std::min<uint64_t>(max_regions, ids.size());
    for (uint64_t index = 0; index < count; ++index) {
      if (const world::Region* region = world->regions().region(ids[index]);
          region != nullptr) {
        fill_region_info(*region, out_regions[index]);
      }
    }
  }
  return ids.size();
}

uint64_t api_active_regions(
    void* context,
    ArvPlayerID player,
    ArvRegionInfo* out_regions,
    uint64_t max_regions) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync ||
      ctx->impl->runtime.default_world == nullptr) {
    return 0U;
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return 0U;
  }
  const world::RegionQuery query{player_state->world_id, player_state->position};
  (void)ctx->impl->runtime.default_world->update_player_region_state(
      player_state->player_id,
      query);
  const world::PlayerRegionState* state =
      ctx->impl->runtime.default_world->player_region_state(player_state->player_id);
  if (state == nullptr) {
    return 0U;
  }
  if (out_regions != nullptr && max_regions > 0U) {
    const uint64_t count =
        std::min<uint64_t>(max_regions, state->active_regions.size());
    for (uint64_t index = 0; index < count; ++index) {
      if (const world::Region* region =
              ctx->impl->runtime.default_world->regions().region(
                  state->active_regions[index]);
          region != nullptr) {
        fill_region_info(*region, out_regions[index]);
      }
    }
  }
  return state->active_regions.size();
}

bool api_is_player_in_region(
    void* context,
    ArvPlayerID player,
    ArvResourceID region_id) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr || tls_thread_context != PluginThreadContext::Sync ||
      ctx->impl->runtime.default_world == nullptr) {
    return false;
  }
  const world::Region* region =
      ctx->impl->runtime.default_world->regions().region(abi_view(region_id));
  if (region == nullptr) {
    return false;
  }
  std::vector<ArvRegionInfo> scratch(api_active_regions(context, player, nullptr, 0U));
  if (!scratch.empty()) {
    (void)api_active_regions(context, player, scratch.data(), scratch.size());
  }
  return std::any_of(
      scratch.begin(),
      scratch.end(),
      [&](const ArvRegionInfo& info) {
        return info.handle.value == region->id.value;
      });
}

ArvResult api_region_flag(
    void* context,
    ArvResourceID region_id,
    ArvResourceID flag,
    ArvRegionRuleValue* out_value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.region_flag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_value == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_value is null");
  }
  *out_value = ARV_REGION_RULE_UNSET;
  const std::string flag_id{abi_view(flag)};
  const RegionFlagRegistration* registration =
      find_region_flag_registration(*ctx->impl, flag_id);
  if (registration == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "region flag not registered");
  }
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  if (ctx->impl->runtime.default_world->regions().region(abi_view(region_id)) ==
      nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "region not found");
  }
  *out_value = region_rule_value_to_abi(
      ctx->impl->runtime.default_world->regions().region_flag(
          abi_view(region_id),
          flag_id,
          registration->default_value));
  return ok(ctx);
}

ArvResult api_query_flag(
    void* context,
    const ArvRegionQuery* query,
    ArvResourceID flag,
    ArvRegionRuleValue* out_value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.query_flag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (query == nullptr || out_value == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "query or out_value is null");
  }
  *out_value = ARV_REGION_RULE_UNSET;
  const std::string flag_id{abi_view(flag)};
  const RegionFlagRegistration* registration =
      find_region_flag_registration(*ctx->impl, flag_id);
  if (registration == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "region flag not registered");
  }
  world::WorldRuntime* world = world_from_handle(ctx, query->world);
  if (world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  *out_value = region_rule_value_to_abi(
      world->regions().flag_at(
          {
              WorldID{static_cast<u32>(query->world)},
              {query->position.x, query->position.y, query->position.z},
          },
          flag_id,
          registration->default_value));
  return ok(ctx);
}

ArvResult api_set_region_flag(
    void* context,
    ArvRegionHandle handle,
    ArvResourceID flag,
    ArvRegionRuleValue value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.set_region_flag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string flag_id{abi_view(flag)};
  if (find_region_flag_registration(*ctx->impl, flag_id) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "region flag not registered");
  }
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  for (RuntimeRegionRegistration& registration : ctx->impl->runtime_regions) {
    if (registration.id.value == handle.value &&
        registration.owner_index == ctx->plugin_index &&
        registration.active) {
      if (!ctx->impl->runtime.default_world->regions().set_region_flag(
              registration.id,
              flag_id,
              region_rule_value_from_abi(value))) {
        return make_result(ctx, ARV_RESULT_NOT_FOUND, "region not found");
      }
      if (registration.persistent && !registration.storage_path.empty()) {
        if (const world::Region* region =
                ctx->impl->runtime.default_world->regions().region(registration.id);
            region != nullptr) {
          return persist_region_to_file(ctx, *region, registration.storage_path);
        }
      }
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "runtime region not found");
}

ArvResult api_set_zone_flag(
    void* context,
    ArvRegionHandle handle,
    ArvResourceID flag,
    ArvRegionRuleValue value) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "regions.set_zone_flag");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string flag_id{abi_view(flag)};
  if (find_region_flag_registration(*ctx->impl, flag_id) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "region flag not registered");
  }
  if (ctx->impl->runtime.default_world == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "world not found");
  }
  for (RuntimeZoneRegistration& registration : ctx->impl->runtime_zones) {
    if (registration.id.value == handle.value &&
        registration.owner_index == ctx->plugin_index &&
        registration.active) {
      return ctx->impl->runtime.default_world->regions().set_zone_flag(
                 registration.id,
                 flag_id,
                 region_rule_value_from_abi(value))
                 ? ok(ctx)
                 : make_result(ctx, ARV_RESULT_NOT_FOUND, "zone not found");
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "runtime zone not found");
}

ArvResult api_register_game_mode(
    void* context,
    const ArvGameModeDefinition* definition) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (definition == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "game mode definition is null");
  }
  if (ctx->impl->options.game_modes == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "game mode registry is unavailable");
  }
  if (definition->custom_permission_count != 0U &&
      definition->custom_permissions == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "game mode custom permissions is null");
  }
  const std::string id{abi_view(definition->id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::string base_id{abi_view(definition->base_id)};
  if (base_id.empty()) {
    base_id = std::string{player::SurvivalGameModeID};
  }
  player::CustomPermissionMap custom_permissions;
  for (usize index = 0; index < definition->custom_permission_count; ++index) {
    const ArvCustomPermissionEntry& entry = definition->custom_permissions[index];
    const std::string node{abi_view(entry.node)};
    if (!player::is_valid_custom_permission_node(node)) {
      return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid permission node");
    }
    if (!permission_node_owned_by_plugin(ctx->info(), node)) {
      return make_result(
          ctx,
          ARV_RESULT_PERMISSION_DENIED,
          "permission node is not owned by plugin pack");
    }
    custom_permissions[node] = permission_value_from_abi(entry.value);
  }
  auto result = ctx->impl->options.game_modes->add_custom_mode(
      id,
      base_id,
      core_permissions_from_abi(definition->core),
      std::move(custom_permissions));
  if (!result) {
    return make_result(
        ctx,
        result.error().message.find("duplicate") != std::string::npos
            ? ARV_RESULT_DUPLICATE_ID
            : ARV_RESULT_INVALID_ARGUMENT,
        result.error().message);
  }
  return ok(ctx);
}

bool api_game_mode_exists(void* context, ArvStringView mode_id) {
  PluginCallContext* ctx = call_context(context);
  return ctx != nullptr &&
         runtime_game_modes(*ctx->impl).find(abi_view(mode_id)) != nullptr;
}

void fill_game_mode_info(
    const player::GameModeDefinition& mode,
    ArvGameModeInfo& out_info) noexcept {
  out_info = {
      stable_view(mode.id),
      stable_view(mode.id),
      mode.builtin,
      resolved_core_to_abi(mode.permissions),
  };
}

ArvResult api_game_mode_info(
    void* context,
    ArvStringView mode_id,
    ArvGameModeInfo* out_info) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_info == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_info is null");
  }
  const player::GameModeDefinition* mode =
      runtime_game_modes(*ctx->impl).find(abi_view(mode_id));
  if (mode == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  fill_game_mode_info(*mode, *out_info);
  return ok(ctx);
}

ArvResult api_game_mode_current(
    void* context,
    ArvPlayerID player,
    ArvStringView* out_mode_id) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "gamemodes.current");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_mode_id == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_mode_id is null");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  *out_mode_id = stable_view(player_state->game_mode_id);
  return ok(ctx);
}

ArvResult api_game_mode_set(
    void* context,
    ArvPlayerID player,
    ArvStringView mode_id,
    ArvResourceID reason) {
  PluginCallContext* ctx = call_context(context);
  (void)reason;
  if (ArvResult sync = require_sync_context(ctx, "gamemodes.set");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  const std::string mode{abi_view(mode_id)};
  if (runtime_game_modes(*ctx->impl).find(mode) == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  player_state->game_mode_id = mode;
  refresh_plugin_player_permissions(*ctx->impl, *player_state);
  return ok(ctx);
}

ArvResult api_game_mode_base_core_permissions(
    void* context,
    ArvStringView mode_id,
    ArvResolvedCorePermissions* out_permissions) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_permissions == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_permissions is null");
  }
  const std::optional<player::ResolvedCorePermissions> permissions =
      runtime_game_modes(*ctx->impl).permissions_for(abi_view(mode_id));
  if (!permissions.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "game mode not found");
  }
  *out_permissions = resolved_core_to_abi(permissions.value());
  return ok(ctx);
}

uint64_t api_game_mode_list(
    void* context,
    ArvGameModeInfo* out_modes,
    uint64_t max_modes) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  std::span<const player::GameModeDefinition> modes =
      runtime_game_modes(*ctx->impl).modes();
  if (out_modes != nullptr && max_modes > 0U) {
    const uint64_t count = std::min<uint64_t>(max_modes, modes.size());
    for (uint64_t index = 0; index < count; ++index) {
      fill_game_mode_info(modes[index], out_modes[index]);
    }
  }
  return modes.size();
}

ArvResult unsupported_no_arg(void* context) {
  PluginCallContext* ctx = call_context(context);
  return make_result(
      ctx,
      ARV_RESULT_UNSUPPORTED,
      "this plugin API is not implemented by the engine");
}

ArvResult api_register_worldgen_feature_type(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::WorldgenFeature);
}

ArvResult api_register_worldgen_noise_type(void* context, ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::WorldgenNoise);
}

ArvResult api_register_worldgen_density_function_type(
    void* context,
    ArvResourceID id) {
  return register_extension_marker(
      context,
      id,
      PluginExtensionRegistryKind::WorldgenDensityFunction);
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

ArvResult api_network_create_chat_message(
    void* context,
    ArvTextHandle message,
    ArvStringView channel,
    ArvChatMessageKind kind,
    ArvPacketHandle* out_packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_packet == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_packet is null");
  }
  *out_packet = {};
  protocol::RichText* rich_text = text_from_handle(ctx, message);
  if (rich_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const protocol::RichTextValidation text_validation =
      protocol::validate_rich_text(*rich_text);
  if (!text_validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, text_validation.message);
  }
  std::string channel_id = abi_string(channel);
  if (channel_id.empty()) {
    channel_id = std::string{protocol::ChatChannelSystem};
  }
  if (channel_id.size() > protocol::MaxChatChannelBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "chat channel is too long");
  }
  if (std::optional<std::string> error = validate_resource_id(channel_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, *error);
  }
  std::optional<protocol::ChatMessageKind> message_kind =
      chat_message_kind_from_abi(kind);
  if (!message_kind.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid chat message kind");
  }

  protocol::ClientboundChatMessage packet{
      0U,
      std::move(channel_id),
      *message_kind,
      *rich_text};
  *out_packet = store_packet(
      ctx,
      {
          protocol::MessageType::ClientboundChatMessage,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Gameplay,
          protocol::serialize_clientbound_chat_message(packet),
      });
  return ok(ctx);
}

ArvResult api_network_create_kick_player(
    void* context,
    ArvTextHandle reason,
    ArvPacketHandle* out_packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_packet == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_packet is null");
  }
  *out_packet = {};
  protocol::RichText* rich_text = text_from_handle(ctx, reason);
  if (rich_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const protocol::RichTextValidation text_validation =
      protocol::validate_rich_text(*rich_text);
  if (!text_validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, text_validation.message);
  }
  const protocol::ClientboundKickPlayer packet{
      protocol::clamp_kick_reason(protocol::to_plain_text(*rich_text))};
  *out_packet = store_packet(
      ctx,
      {
          protocol::MessageType::ClientboundKickPlayer,
          protocol::ReliabilityClass::ReliableImmediate,
          protocol::NetworkLane::Session,
          protocol::serialize_clientbound_kick_player(packet),
      });
  return ok(ctx);
}

[[nodiscard]] ArvResult send_network_request(
    PluginCallContext* ctx,
    PluginNetworkSendRequest request) {
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.network_send == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "network sender is unavailable");
  }
  if (request.packet == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "packet handle not found");
  }
  PluginNetworkSendResult result = ctx->impl->runtime.network_send(request);
  if (!result) {
    return make_result(ctx, result.error().code, result.error().message);
  }
  return ok(ctx);
}

ArvResult api_network_send_to_player(
    void* context,
    ArvPlayerID player,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkPacket* network_packet = packet_from_handle(ctx, packet);
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Player;
  request.player = PlayerID{player};
  request.packet = network_packet;
  return send_network_request(ctx, request);
}

ArvResult api_network_send_to_players(
    void* context,
    const ArvPlayerID* players,
    size_t player_count,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (player_count > 0U && players == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "players is null");
  }
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  std::vector<PlayerID> player_ids;
  player_ids.reserve(player_count);
  for (size_t index = 0; index < player_count; ++index) {
    player_ids.push_back(PlayerID{players[index]});
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Players;
  request.players = std::span<const PlayerID>{player_ids.data(), player_ids.size()};
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

ArvResult api_network_broadcast(void* context, ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Broadcast;
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

ArvResult api_network_broadcast_world(
    void* context,
    ArvWorldHandle world,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (world > static_cast<ArvWorldHandle>(
                  std::numeric_limits<WorldID::value_type>::max())) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "world handle is out of range");
  }
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::World;
  request.world = WorldID{static_cast<WorldID::value_type>(world)};
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

void api_network_release_packet(void* context, ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx != nullptr && packet.value != 0U) {
    ctx->network_packets.erase(packet.value);
  }
}

struct CommandExecutionContext {
  explicit CommandExecutionContext(command::CommandCtx& cpp_ctx) : cpp_context(cpp_ctx) {
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
    abi_context.source_kind =
        cpp_ctx.sender.kind == command::CommandSourceKind::Player
            ? ARV_COMMAND_SOURCE_PLAYER
            : ARV_COMMAND_SOURCE_SERVER_CONSOLE;
    if (cpp_ctx.sender.player_id.has_value()) {
      abi_context.has_player = true;
      abi_context.player = cpp_ctx.sender.player_id->value;
    }
    if (cpp_ctx.sender.entity_id.has_value()) {
      abi_context.has_entity = true;
      abi_context.entity = cpp_ctx.sender.entity_id->value;
    }
    if (cpp_ctx.sender.world_id.has_value()) {
      abi_context.has_world = true;
      abi_context.world = cpp_ctx.sender.world_id->value;
    }
    if (cpp_ctx.sender.position.has_value()) {
      abi_context.has_position = true;
      abi_context.position = {
          cpp_ctx.sender.position->x,
          cpp_ctx.sender.position->y,
          cpp_ctx.sender.position->z};
    }
  }

  static void reply(
      ArvCommandContext* command_context,
      ArvCommandMessageKind kind,
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
  ArvCommandContext abi_context{};
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
  ArvResult result =
      registration.callback(&execution.abi_context, registration.user_data);

  command::CommandResult command_result;
  command_result.status = command_status_from_result(result.code);
  command_result.messages = std::move(execution.messages);
  if (result.code != ARV_RESULT_OK && command_result.messages.empty()) {
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
      &api_register_entity_behavior,
      &api_register_component_codec,
      &api_register_loot_function,
      &api_register_recipe_type,
      &api_find_block_variant,
      &api_block_variant_name,
      &api_air_block_variant,
      &api_is_air_block_variant,
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
      &api_sync_delayed_tick,
      &api_sync_repeating_tick,
      &api_async_delayed_tick,
      &api_async_repeating_tick,
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
      &api_entity_add_tag,
      &api_entity_remove_tag,
      &api_entity_has_tag,
  };
  data_api = {
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
  config_api = {
      &api_config_load,
      &api_config_save,
      &api_config_exists,
      &api_config_remove,
  };
  persistent_data_api = {
      &api_persistent_load,
      &api_persistent_save,
      &api_persistent_exists,
      &api_persistent_remove,
  };
  worldgen_api = {
      &api_register_worldgen_feature_type,
      &api_register_worldgen_noise_type,
      &api_register_worldgen_density_function_type,
  };
  network_api = {
      &unsupported_no_arg,
      &api_network_create_chat_message,
      &api_network_create_kick_player,
      &api_network_send_to_player,
      &api_network_send_to_players,
      &api_network_broadcast,
      &api_network_broadcast_world,
      &api_network_release_packet,
  };
  text_api = {
      &unsupported_no_arg,
      &api_text_literal,
      &api_text_translatable,
      &api_text_append,
      &api_text_style,
      &api_text_color,
      &api_text_hover_text,
      &api_text_click,
      &api_text_plain_text,
      &api_text_serialize,
      &api_text_validate,
      &api_text_release,
  };
  metrics_api = {&api_metrics_counter_add};
  permissions_api = {
      &unsupported_no_arg,
      &api_register_permission_node,
      &api_permission_check_core,
      &api_permission_check_custom,
      &api_permission_resolved_core,
      &api_permission_register_group,
      &api_permission_has_group,
      &api_permission_add_group,
      &api_permission_remove_group,
      &api_permission_groups,
  };
  regions_api = {
      &api_register_region_flag,
      &api_create_runtime_region,
      &api_remove_runtime_region,
      &api_register_runtime_zone,
      &api_remove_runtime_zone,
      &api_find_regions_at,
      &api_active_regions,
      &api_is_player_in_region,
      &api_region_flag,
      &api_query_flag,
      &api_set_region_flag,
      &api_set_zone_flag,
  };
  game_modes_api = {
      &api_register_game_mode,
      &api_game_mode_exists,
      &api_game_mode_info,
      &api_game_mode_current,
      &api_game_mode_set,
      &api_game_mode_base_core_permissions,
      &api_game_mode_list,
  };
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
      &data_api,
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
      context,
      &regions_api,
      context,
      &game_modes_api,
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
    auto entrypoint = reinterpret_cast<ArvEntrypoint>(raw_entrypoint);
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
  const player::PermissionGroupRegistry* previous_group_registry =
      impl_->runtime.permission_groups;
  const world::WorldRuntime* previous_world = impl_->runtime.default_world;
  if (previous_group_registry != context.permission_groups) {
    for (PermissionGroupRegistration& registration : impl_->permission_groups) {
      registration.installed = false;
    }
  }
  if (previous_world != context.default_world) {
    impl_->persistent_regions_installed = false;
  }
  impl_->runtime = context;
  install_plugin_permission_groups(*impl_);
  install_persistent_regions(*impl_);
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
  if (impl_->runtime.default_world != nullptr) {
    for (RuntimeRegionRegistration& registration : impl_->runtime_regions) {
      if (registration.active) {
        (void)impl_->runtime.default_world->regions().remove_region(registration.id);
        registration.active = false;
      }
    }
    for (RuntimeZoneRegistration& registration : impl_->runtime_zones) {
      if (registration.active) {
        (void)impl_->runtime.default_world->regions().remove_zone(registration.id);
        registration.active = false;
      }
    }
  }
  impl_->runtime = {};
  impl_->runtime_active = false;
  impl_->persistent_regions_installed = false;
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
    ArvJobCallback callback{};
    void* user_data{};
  };
  std::vector<JobToRun> due;
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      const bool job_due =
          job.timing == ScheduledJobTiming::Ticks
              ? job.next_run <= tick_index
              : job.next_time <= now;
      if (job.canceled || job.running || !job_due) {
        continue;
      }
      due.push_back({job.id, job.owner_index, job.kind, job.callback, job.user_data});
      if (job.kind == ScheduledJobKind::Async) {
        job.running = true;
      }
      if (job.repeating) {
        if (job.timing == ScheduledJobTiming::Ticks) {
          job.next_run = tick_index + job.interval;
        } else {
          job.next_time = now + job.interval_time;
        }
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

  ArvEvent event;
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

bool PluginManager::has_registered_extension(
    PluginExtensionRegistryKind kind,
    std::string_view id) const noexcept {
  return std::any_of(
      impl_->extension_registrations.begin(),
      impl_->extension_registrations.end(),
      [&](const PluginExtensionRegistration& registration) {
        return registration.kind == kind && registration.id == id;
      });
}

std::vector<std::string> PluginManager::registered_extension_ids(
    PluginExtensionRegistryKind kind) const {
  std::vector<std::string> ids;
  for (const PluginExtensionRegistration& registration :
       impl_->extension_registrations) {
    if (registration.kind == kind) {
      ids.push_back(registration.id);
    }
  }
  return ids;
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
