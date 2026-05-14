#include "plugin_manager_internal.hpp"

#include "arv/core/string.hpp"

namespace arv::plugin {

thread_local PluginThreadContext tls_thread_context = PluginThreadContext::None;

ThreadContextScope::ThreadContextScope(PluginThreadContext context) noexcept
    : previous_(tls_thread_context) {
  tls_thread_context = context;
}

ThreadContextScope::~ThreadContextScope() {
  tls_thread_context = previous_;
}

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

[[nodiscard]] bool command_name_valid(std::string_view name) noexcept {
  return !name.empty() && !contains_ascii_space(name);
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

[[nodiscard]] ArvResult null_context_result(
    ArvResultCode code,
    const char* message) noexcept {
  return {code, message};
}

[[nodiscard]] ArvResult make_result(
    PluginCallContext* context,
    ArvResultCode code,
    std::string message) {
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

[[nodiscard]] ArvResultCode storage_result_code(io::DataStorageCode code) noexcept {
  switch (code) {
    case io::DataStorageCode::NotFound:
      return ARV_RESULT_NOT_FOUND;
    case io::DataStorageCode::InvalidArgument:
      return ARV_RESULT_INVALID_ARGUMENT;
    case io::DataStorageCode::PermissionDenied:
      return ARV_RESULT_PERMISSION_DENIED;
    case io::DataStorageCode::Unsupported:
      return ARV_RESULT_UNSUPPORTED;
    case io::DataStorageCode::Corrupt:
      return ARV_RESULT_INVALID_ARGUMENT;
    case io::DataStorageCode::InternalError:
      return ARV_RESULT_INTERNAL_ERROR;
  }
  return ARV_RESULT_INTERNAL_ERROR;
}

[[nodiscard]] ArvDataKind data_kind_to_abi(io::DataKind kind) noexcept {
  switch (kind) {
    case io::DataKind::Null:
      return ARV_DATA_NULL;
    case io::DataKind::Bool:
      return ARV_DATA_BOOL;
    case io::DataKind::I64:
      return ARV_DATA_I64;
    case io::DataKind::U64:
      return ARV_DATA_U64;
    case io::DataKind::F64:
      return ARV_DATA_F64;
    case io::DataKind::String:
      return ARV_DATA_STRING;
    case io::DataKind::Bytes:
      return ARV_DATA_BYTES;
    case io::DataKind::Array:
      return ARV_DATA_ARRAY;
    case io::DataKind::Object:
      return ARV_DATA_OBJECT;
  }
  return ARV_DATA_INVALID;
}

[[nodiscard]] io::DataDocument* document_from_handle(
    PluginCallContext* ctx,
    ArvDataDocumentHandle handle) {
  if (ctx == nullptr || handle == 0U) {
    return nullptr;
  }
  const auto found = ctx->data_documents.find(handle);
  return found == ctx->data_documents.end() ? nullptr : &found->second;
}

[[nodiscard]] const io::DataDocument* document_from_handle(
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
    io::DataDocument document) {
  if (ctx == nullptr) {
    return 0U;
  }
  const u64 handle = ctx->next_data_document_handle++;
  ctx->data_documents.emplace(handle, std::move(document));
  return handle;
}

ArvDataDocumentHandle api_data_create_document(void* context, uint32_t schema_version) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  return store_document(ctx, io::DataDocument{schema_version, io::DataValue::object()});
}

[[nodiscard]] std::optional<io::ResourceKey> parse_owned_plugin_key(
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
  return io::ResourceKey{
      .name_space = parsed.value().name_space,
      .path = parsed.value().path};
}

void append_diagnostic(PluginManagerImpl& impl, PluginError diagnostic);

void append_diagnostic(
    PluginManagerImpl& impl,
    std::string pack_id,
    std::string phase,
    std::filesystem::path file,
    std::string message) {
  append_diagnostic(
      impl,
      PluginError{
          std::move(pack_id),
          std::move(phase),
          std::move(file),
          std::move(message)});
}

void append_diagnostic(PluginManagerImpl& impl, PluginError diagnostic) {
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

[[nodiscard]] arv::text::RichText* text_from_handle(
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
    arv::text::RichText text) {
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
    arv::text::TextStylePatch& result,
    ArvTextStylePatch style) {
  if (style.has_color) {
    result.color = arv::text::RgbColor{style.color.r, style.color.g, style.color.b};
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

[[nodiscard]] arv::text::ClickAction click_action_from_abi(
    ArvTextClickAction action) noexcept {
  switch (action) {
    case ARV_TEXT_CLICK_OPEN_URL:
      return arv::text::ClickAction::OpenUrl;
    case ARV_TEXT_CLICK_SUGGEST_COMMAND:
      return arv::text::ClickAction::SuggestCommand;
    case ARV_TEXT_CLICK_RUN_COMMAND:
      return arv::text::ClickAction::RunCommand;
    case ARV_TEXT_CLICK_COPY_TO_CLIPBOARD:
      return arv::text::ClickAction::CopyToClipboard;
  }
  return arv::text::ClickAction::SuggestCommand;
}

[[nodiscard]] arv::text::RichTextLimits text_limits_from_abi(
    const ArvTextLimits* limits) noexcept {
  arv::text::RichTextLimits result;
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

std::filesystem::path plugin_data_root(PluginCallContext* ctx) {
  if (ctx == nullptr || ctx->impl->runtime.server_save_root.empty()) {
    return {};
  }
  return ctx->impl->runtime.server_save_root / "plugin_data" / ctx->info().pack_id;
}

bool permission_node_owned_by_plugin(
    const PluginInfo& info,
    std::string_view node) {
  auto matches_domain = [](std::string_view value, std::string_view domain) {
    return value == domain ||
           (value.size() > domain.size() &&
            value.substr(0, domain.size()) == domain &&
            value[domain.size()] == '.');
  };
  for (const std::string& name_space : info.namespaces) {
    if (matches_domain(node, name_space)) {
      return true;
    }
    constexpr std::string_view first_party_prefix = "arv_";
    if (std::string_view{name_space}.starts_with(first_party_prefix)) {
      const std::string_view short_domain =
          std::string_view{name_space}.substr(first_party_prefix.size());
      if (matches_domain(node, short_domain)) {
        return true;
      }
    }
  }
  constexpr std::string_view first_party_prefix = "arv_";
  if (std::string_view{info.pack_id}.starts_with(first_party_prefix)) {
    const std::string_view short_domain =
        std::string_view{info.pack_id}.substr(first_party_prefix.size());
    if (matches_domain(node, short_domain)) {
      return true;
    }
  }
  return false;
}

ArvResult unsupported_no_arg(void* context) {
  PluginCallContext* ctx = call_context(context);
  return make_result(
      ctx,
      ARV_RESULT_UNSUPPORTED,
      "this plugin API is not implemented by the engine");
}

} // namespace arv::plugin
