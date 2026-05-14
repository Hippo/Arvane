#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

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

} // namespace

void fill_registry_api(PluginAPIStorage& storage) {
  storage.registries_api = {
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
  storage.worldgen_api = {
      &api_register_worldgen_feature_type,
      &api_register_worldgen_noise_type,
      &api_register_worldgen_density_function_type,
  };
}

} // namespace arv::plugin
