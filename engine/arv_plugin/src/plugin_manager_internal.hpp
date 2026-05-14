#pragma once

#include "arv/plugin/plugin_manager.hpp"

#include "dynamic_library.hpp"
#include "plugin_alias_data.hpp"
#include "plugin_native_loading.hpp"
#include "plugin_paths.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/core/log.hpp"
#include "arv/data/document.hpp"
#include "arv/data/storage.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::plugin {

enum class PluginThreadContext : u8 {
  None,
  Sync,
  Async,
};

extern thread_local PluginThreadContext tls_thread_context;

class ThreadContextScope {
public:
  explicit ThreadContextScope(PluginThreadContext context) noexcept;
  ~ThreadContextScope();

private:
  PluginThreadContext previous_{PluginThreadContext::None};
};

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

struct PluginCallContext {
  PluginManagerImpl* impl{};
  usize plugin_index{};
  std::string result_message{};
  u64 next_data_document_handle{1U};
  HashMap<u64, data::DataDocument> data_documents{};
  u64 next_text_handle{1U};
  HashMap<u64, arv::text::RichText> text_components{};
  u64 next_packet_handle{1U};
  HashMap<u64, PluginNetworkPacket> network_packets{};

  [[nodiscard]] PluginInfo& info() noexcept {
    return impl->plugins[plugin_index].info;
  }

  [[nodiscard]] const PluginInfo& info() const noexcept {
    return impl->plugins[plugin_index].info;
  }
};

[[nodiscard]] const char* safe_cstr(const char* text) noexcept;
[[nodiscard]] std::string_view abi_view(ArvStringView view) noexcept;
[[nodiscard]] std::string abi_string(ArvStringView view);
[[nodiscard]] ArvStringView stable_view(const std::string& text) noexcept;
[[nodiscard]] ArvStringView literal_view(const char* text) noexcept;
[[nodiscard]] bool command_name_valid(std::string_view name) noexcept;
[[nodiscard]] command::CommandDomain command_domain_from_abi(
    ArvCommandDomain domain) noexcept;
[[nodiscard]] command::CommandMessageKind command_message_kind_from_abi(
    ArvCommandMessageKind kind) noexcept;
[[nodiscard]] player::TriState permission_value_from_abi(
    ArvPermissionValue value) noexcept;
[[nodiscard]] ArvPermissionValue permission_value_to_abi(
    player::TriState value) noexcept;
[[nodiscard]] world::RegionRuleValue region_rule_value_from_abi(
    ArvRegionRuleValue value) noexcept;
[[nodiscard]] ArvRegionRuleValue region_rule_value_to_abi(
    world::RegionRuleValue value) noexcept;
[[nodiscard]] int tristate_to_int(player::TriState value) noexcept;
[[nodiscard]] player::TriState tristate_from_int(int value) noexcept;
[[nodiscard]] int region_rule_to_int(world::RegionRuleValue value) noexcept;
[[nodiscard]] world::RegionRuleValue region_rule_from_int(int value) noexcept;
[[nodiscard]] player::CorePermissionSet core_permissions_from_abi(
    const ArvCorePermissionSet& permissions) noexcept;
[[nodiscard]] ArvResolvedCorePermissions resolved_core_to_abi(
    const player::ResolvedCorePermissions& permissions) noexcept;
[[nodiscard]] bool resolved_core_permission_value(
    const player::ResolvedCorePermissions& permissions,
    ArvCorePermission permission) noexcept;
[[nodiscard]] command::CommandStatus command_status_from_result(
    ArvResultCode code) noexcept;
[[nodiscard]] std::string result_code_name(ArvResultCode code);
[[nodiscard]] bool owns_namespace(
    const PluginInfo& info,
    std::string_view name_space) noexcept;
[[nodiscard]] std::optional<std::string> validate_owned_resource_id(
    const PluginInfo& info,
    std::string_view id);
[[nodiscard]] std::optional<std::string> validate_resource_id(std::string_view id);
[[nodiscard]] ArvResult null_context_result(
    ArvResultCode code,
    const char* message) noexcept;
[[nodiscard]] ArvResult make_result(
    PluginCallContext* context,
    ArvResultCode code,
    std::string message = {});
[[nodiscard]] ArvResult ok(PluginCallContext* context);
[[nodiscard]] PluginCallContext* call_context(void* context) noexcept;
[[nodiscard]] ArvResultCode storage_result_code(data::DataStorageCode code) noexcept;
[[nodiscard]] ArvDataKind data_kind_to_abi(data::DataKind kind) noexcept;
[[nodiscard]] data::DataDocument* document_from_handle(
    PluginCallContext* ctx,
    ArvDataDocumentHandle handle);
[[nodiscard]] const data::DataDocument* document_from_handle(
    const PluginCallContext* ctx,
    ArvDataDocumentHandle handle);
[[nodiscard]] ArvDataDocumentHandle store_document(
    PluginCallContext* ctx,
    data::DataDocument document);
[[nodiscard]] ArvDataDocumentHandle api_data_create_document(
    void* context,
    uint32_t schema_version);
[[nodiscard]] std::optional<asset::ResourceID> parse_owned_plugin_key(
    PluginCallContext* ctx,
    ArvResourceID key,
    std::string_view purpose,
    ArvResultCode& out_code,
    std::string& out_message);
void append_diagnostic(PluginManagerImpl& impl, PluginError diagnostic);
void append_diagnostic(
    PluginManagerImpl& impl,
    std::string pack_id,
    std::string phase,
    std::filesystem::path file,
    std::string message);
[[nodiscard]] ArvResult require_registration_phase(
    PluginCallContext* context);
[[nodiscard]] ArvResult require_sync_context(
    PluginCallContext* context,
    std::string_view api_name);
[[nodiscard]] world::WorldRuntime* world_from_handle(
    PluginCallContext* context,
    ArvWorldHandle handle);
[[nodiscard]] player::PlayerState* player_from_id(
    PluginCallContext* context,
    ArvPlayerID player_id);
[[nodiscard]] const player::GameModeRegistry& runtime_game_modes(
    const PluginManagerImpl& impl) noexcept;
[[nodiscard]] const player::PermissionGroupRegistry& runtime_permission_groups(
    const PluginManagerImpl& impl) noexcept;
void refresh_plugin_player_permissions(
    PluginManagerImpl& impl,
    player::PlayerState& player_state);
void install_plugin_permission_groups(PluginManagerImpl& impl);
[[nodiscard]] ArvByteBuffer byte_buffer_from_string(std::string text);
[[nodiscard]] arv::text::RichText* text_from_handle(
    PluginCallContext* ctx,
    ArvTextHandle handle);
[[nodiscard]] ArvTextHandle store_text(
    PluginCallContext* ctx,
    arv::text::RichText text);
[[nodiscard]] PluginNetworkPacket* packet_from_handle(
    PluginCallContext* ctx,
    ArvPacketHandle handle);
[[nodiscard]] ArvPacketHandle store_packet(
    PluginCallContext* ctx,
    PluginNetworkPacket packet);
void apply_text_style_patch(
    arv::text::TextStylePatch& result,
    ArvTextStylePatch style);
[[nodiscard]] std::optional<protocol::ChatMessageKind> chat_message_kind_from_abi(
    ArvChatMessageKind kind) noexcept;
[[nodiscard]] arv::text::ClickAction click_action_from_abi(
    ArvTextClickAction action) noexcept;
[[nodiscard]] arv::text::RichTextLimits text_limits_from_abi(
    const ArvTextLimits* limits) noexcept;
[[nodiscard]] std::filesystem::path plugin_data_root(PluginCallContext* ctx);
[[nodiscard]] bool permission_node_owned_by_plugin(
    const PluginInfo& info,
    std::string_view node);
ArvResult unsupported_no_arg(void* context);
void install_persistent_regions(PluginManagerImpl& impl);

void fill_context_api(PluginAPIStorage& storage);
void fill_registry_api(PluginAPIStorage& storage);
void fill_command_api(PluginAPIStorage& storage);
void fill_event_api(PluginAPIStorage& storage);
void fill_scheduler_api(PluginAPIStorage& storage);
void fill_service_api(PluginAPIStorage& storage);
void fill_resource_api(PluginAPIStorage& storage);
void fill_world_entity_api(PluginAPIStorage& storage);
void fill_data_api(PluginAPIStorage& storage);
void fill_text_api(PluginAPIStorage& storage);
void fill_permission_api(PluginAPIStorage& storage);
void fill_region_api(PluginAPIStorage& storage);
void fill_game_mode_api(PluginAPIStorage& storage);
void fill_network_api(PluginAPIStorage& storage);

} // namespace arv::plugin
