#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

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
  return root / "regions" /
         detail::data_relative_path_for(parsed.value(), ".region");
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

void install_persistent_regions_impl(PluginManagerImpl& impl) {
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

} // namespace

void install_persistent_regions(PluginManagerImpl& impl) {
  install_persistent_regions_impl(impl);
}

void fill_region_api(PluginAPIStorage& storage) {
  storage.regions_api = {
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
}

} // namespace arv::plugin
