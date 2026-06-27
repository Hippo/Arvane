#include "server_runtime_internal.hpp"

#include "arv/io/data_document.hpp"
#include "arv/text/chat_message.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server::detail {
namespace {

[[nodiscard]] std::string horizontal_facing_from_look(
    Vec3f look_direction,
    std::string_view fallback) {
  if (std::abs(look_direction.x) > std::abs(look_direction.z)) {
    return look_direction.x >= 0.0F ? "east" : "west";
  }
  if (std::abs(look_direction.z) > 0.0001F) {
    return look_direction.z >= 0.0F ? "south" : "north";
  }
  return std::string{fallback};
}

[[nodiscard]] std::string half_from_hit(
    const physics::BlockRaycastHit& hit,
    BlockPos place_pos,
    std::string_view fallback) {
  if (hit.face == voxel::BlockFace::PositiveY) {
    return "bottom";
  }
  if (hit.face == voxel::BlockFace::NegativeY) {
    return "top";
  }
  const f32 local_y = hit.hit_point.y - static_cast<f32>(place_pos.value.y);
  if (local_y > 0.5F) {
    return "top";
  }
  if (local_y >= 0.0F) {
    return "bottom";
  }
  return std::string{fallback};
}

[[nodiscard]] voxel::BlockVariantID resolve_placement_variant(
    const voxel::BlockRegistry& registry,
    voxel::BlockVariantID default_variant,
    const physics::BlockRaycastHit& hit,
    BlockPos place_pos,
    Vec3f look_direction) {
  const voxel::BlockVariantRecord* default_record = registry.variant(default_variant);
  if (default_record == nullptr || default_record->state_values.empty()) {
    return default_variant;
  }
  const voxel::BlockRecord* block = registry.block(default_record->block_id);
  if (block == nullptr) {
    return default_variant;
  }

  std::string variant_name = block->name;
  variant_name += "[";
  for (usize index = 0; index < default_record->state_values.size(); ++index) {
    const voxel::BlockStateValue& state = default_record->state_values[index];
    std::string value = state.value;
    if (state.name == "facing") {
      value = horizontal_facing_from_look(look_direction, state.value);
    } else if (state.name == "half") {
      value = half_from_hit(hit, place_pos, state.value);
    }

    if (index > 0) {
      variant_name += ",";
    }
    variant_name += state.name;
    variant_name += "=";
    variant_name += value;
  }
  variant_name += "]";
  return registry.find_variant(variant_name).value_or(default_variant);
}

void play_right_hand_interaction_animation(world::WorldRuntime& world, PlayerID player_id) {
  const std::optional<EntityID> entity_id = world.entity_for_player(player_id);
  if (!entity_id.has_value()) {
    return;
  }
  (void)world.play_entity_animation(entity_id.value(), "hit_right");
}

[[nodiscard]] bool block_stops_bucket_raycast(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    BlockPos pos) noexcept {
  const voxel::BlockVariantID block = world.get_block(pos);
  const voxel::BlockVariantRecord* variant = registry.variant(block);
  return variant != nullptr && !registry.is_air(block) && !variant->properties.replaceable;
}

[[nodiscard]] std::optional<BlockPos> raycast_source_fluid(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance) {
  direction = normalize_or_zero(direction);
  if (direction == Vec3f{}) {
    return std::nullopt;
  }

  i32 x = floor_to_i32(origin.x);
  i32 y = floor_to_i32(origin.y);
  i32 z = floor_to_i32(origin.z);

  const i32 step_x = direction.x > 0.0F ? 1 : (direction.x < 0.0F ? -1 : 0);
  const i32 step_y = direction.y > 0.0F ? 1 : (direction.y < 0.0F ? -1 : 0);
  const i32 step_z = direction.z > 0.0F ? 1 : (direction.z < 0.0F ? -1 : 0);

  const f32 inf = std::numeric_limits<f32>::infinity();
  auto initial_t_max = [&](f32 origin_axis, f32 direction_axis, i32 cell, i32 step) -> f32 {
    if (step == 0) {
      return inf;
    }
    const f32 boundary = step > 0 ? static_cast<f32>(cell + 1) : static_cast<f32>(cell);
    return (boundary - origin_axis) / direction_axis;
  };

  f32 t_max_x = initial_t_max(origin.x, direction.x, x, step_x);
  f32 t_max_y = initial_t_max(origin.y, direction.y, y, step_y);
  f32 t_max_z = initial_t_max(origin.z, direction.z, z, step_z);
  const f32 t_delta_x = step_x == 0 ? inf : std::abs(1.0F / direction.x);
  const f32 t_delta_y = step_y == 0 ? inf : std::abs(1.0F / direction.y);
  const f32 t_delta_z = step_z == 0 ? inf : std::abs(1.0F / direction.z);

  constexpr usize MaxIterations = 2048;
  for (usize iteration = 0; iteration < MaxIterations; ++iteration) {
    const BlockPos block{{x, y, z}};
    if (!world.find_chunk(block_to_chunk(block))) {
      return std::nullopt;
    }

    const voxel::FluidCell fluid = world.get_fluid(block);
    if (fluid.type == definition.type && fluid.source()) {
      return block;
    }
    if (block_stops_bucket_raycast(world, registry, block)) {
      return std::nullopt;
    }

    const f32 next_t = std::min(t_max_x, std::min(t_max_y, t_max_z));
    if (next_t > max_distance) {
      break;
    }

    if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
      x += step_x;
      t_max_x += t_delta_x;
    } else if (t_max_y <= t_max_z) {
      y += step_y;
      t_max_y += t_delta_y;
    } else {
      z += step_z;
      t_max_z += t_delta_z;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<player::ItemID> find_item_id(
    const player::ItemRegistry& registry,
    std::string_view name) {
  return registry.find_item(name);
}

[[nodiscard]] std::optional<player::Inventory> inventory_after_bucket_fill(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    std::string_view filled_source_item_name) {
  if (filled_source_item_name.empty()) {
    return std::nullopt;
  }
  const std::optional<player::ItemID> filled_source_item =
      find_item_id(item_registry, filled_source_item_name);
  if (!filled_source_item.has_value()) {
    return std::nullopt;
  }

  player::Inventory next_inventory = player_state.inventory;
  const usize slot = player_state.selected_hotbar_slot;
  const std::optional<player::ItemStack>& held = next_inventory.slot(slot);
  if (!held.has_value()) {
    return std::nullopt;
  }

  if (held->count == 1U) {
    if (!next_inventory.set_slot(slot, player::ItemStack{filled_source_item.value(), 1U})) {
      return std::nullopt;
    }
  } else {
    if (!next_inventory.consume_one(slot)) {
      return std::nullopt;
    }
    const player::InventoryInsertResult insert =
        next_inventory.insert_stack({filled_source_item.value(), 1U}, item_registry);
    if (!insert.complete()) {
      return std::nullopt;
    }
  }

  return next_inventory;
}

[[nodiscard]] std::optional<player::Inventory> inventory_after_water_place(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    std::string_view empty_container_item_name) {
  if (empty_container_item_name.empty()) {
    return std::nullopt;
  }
  const std::optional<player::ItemID> empty_container_item =
      find_item_id(item_registry, empty_container_item_name);
  if (!empty_container_item.has_value()) {
    return std::nullopt;
  }

  player::Inventory next_inventory = player_state.inventory;
  const usize slot = player_state.selected_hotbar_slot;
  const std::optional<player::ItemStack>& held = next_inventory.slot(slot);
  if (!held.has_value()) {
    return std::nullopt;
  }

  if (held->count == 1U) {
    if (!next_inventory.set_slot(slot, player::ItemStack{empty_container_item.value(), 1U})) {
      return std::nullopt;
    }
  } else {
    if (!next_inventory.consume_one(slot)) {
      return std::nullopt;
    }
    const player::InventoryInsertResult insert =
        next_inventory.insert_stack({empty_container_item.value(), 1U}, item_registry);
    if (!insert.complete()) {
      return std::nullopt;
    }
  }

  return next_inventory;
}

[[nodiscard]] bool can_place_fluid_source_at(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) noexcept {
  const voxel::BlockVariantID existing_block = world.get_block(pos);
  if (!(registry.is_air(existing_block) || registry.is_replaceable(existing_block))) {
    return false;
  }

  const voxel::FluidCell existing_fluid = world.get_fluid(pos);
  return existing_fluid.empty() ||
         (existing_fluid.type == definition.type && !existing_fluid.source());
}

[[nodiscard]] u8 break_stage_for_progress(f32 progress) noexcept {
  if (progress <= 0.0F) {
    return 0;
  }
  return static_cast<u8>(std::clamp(static_cast<i32>(progress * 10.0F), 0, 9));
}

[[nodiscard]] BlockBreakBehaviorEvaluation evaluate_block_break_behavior(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockVariantRecord& target,
    const BlockBreakingConfig& config,
    const BlockBreakBehaviorRegistry* behaviors) {
  if (config.behavior_id.empty() || behaviors == nullptr) {
    return {};
  }
  const BlockBreakBehaviorRegistry::Handler* handler = behaviors->find(config.behavior_id);
  if (handler == nullptr) {
    return {};
  }
  return (*handler)({
      player_state,
      item_registry,
      block_registry,
      target,
      config,
  });
}

[[nodiscard]] player::ItemID held_item_id(const player::PlayerState& player_state) noexcept {
  const std::optional<player::ItemStack>& held =
      player_state.inventory.slot(player_state.selected_hotbar_slot);
  return held.has_value() ? held->item : player::AirItemID;
}

[[nodiscard]] const player::ItemStack* held_item_stack(
    const player::PlayerState& player_state) noexcept {
  const std::optional<player::ItemStack>& held =
      player_state.inventory.slot(player_state.selected_hotbar_slot);
  return held.has_value() ? &held.value() : nullptr;
}

[[nodiscard]] bool item_stack_is_mason_marker(
    const player::ItemStack& stack) noexcept {
  if (!stack.data.has_value()) {
    return false;
  }
  const io::DataValue* marker = io::find_path(*stack.data, "arv_mason.marker");
  return marker != nullptr && marker->as_bool_or(false);
}

[[nodiscard]] std::optional<std::string_view> marker_string_data(
    const player::ItemStack& stack,
    std::string_view path) noexcept {
  if (!stack.data.has_value()) {
    return std::nullopt;
  }
  const io::DataValue* value = io::find_path(*stack.data, path);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return std::nullopt;
  }
  const std::string_view text = value->as_string_or({});
  return text.empty() ? std::nullopt : std::optional<std::string_view>{text};
}

void replace_all(std::string& text, std::string_view needle, std::string_view value) {
  if (needle.empty()) {
    return;
  }
  for (std::size_t pos = text.find(needle); pos != std::string::npos;
       pos = text.find(needle, pos + value.size())) {
    text.replace(pos, needle.size(), value);
  }
}

[[nodiscard]] std::string format_block_position(BlockPos pos) {
  return std::to_string(pos.value.x) + ", " +
         std::to_string(pos.value.y) + ", " +
         std::to_string(pos.value.z);
}

[[nodiscard]] std::string marker_feedback_message(
    const player::ItemStack& stack,
    std::string_view path,
    BlockPos pos) {
  const std::optional<std::string_view> message_template =
      marker_string_data(stack, path);
  if (!message_template.has_value()) {
    return {};
  }
  std::string message{message_template.value()};
  replace_all(message, "{position}", format_block_position(pos));
  return message;
}

struct MasonMarkerInteractionResult {
  bool handled{};
  std::vector<std::string> feedback_messages{};
};

[[nodiscard]] MasonMarkerInteractionResult handle_mason_marker_interaction(
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const physics::ICollisionQuery& interaction_query,
    Vec3f eye_position,
    Vec3f look_direction,
    f32 reach_distance) {
  const bool marker_button_active =
      command.primary_pressed || command.primary_held ||
      command.secondary_pressed || command.secondary_held;
  const player::ItemStack* held = held_item_stack(player_state);
  if (!marker_button_active || held == nullptr || !item_stack_is_mason_marker(*held)) {
    return {};
  }

  auto hit = physics::raycast_blocks(
      interaction_query,
      eye_position,
      look_direction,
      reach_distance);
  if (!hit.has_value()) {
    return {.handled = true};
  }

  MasonMarkerInteractionResult result{.handled = true};
  player_state.selection.world = player_state.world_id;
  if (command.primary_pressed) {
    player_state.selection.a = hit->block;
    result.feedback_messages.push_back(marker_feedback_message(
        *held,
        "arv_mason.select_a_message",
        hit->block));
  }
  if (command.secondary_pressed) {
    player_state.selection.b = hit->block;
    result.feedback_messages.push_back(marker_feedback_message(
        *held,
        "arv_mason.select_b_message",
        hit->block));
  }
  return result;
}

struct VehicleFuelInteractionResult {
  bool handled{};
  std::string feedback_message{};
};

[[nodiscard]] Aabb3f entity_interaction_bounds(
    const entity::EntityTransform& transform,
    const entity::EntityCollision* collision) noexcept {
  const Vec3f box = collision == nullptr
                        ? Vec3f{1.0F, 1.0F, 1.0F}
                        : Vec3f{
                              std::max(collision->box.x, 0.0F),
                              std::max(collision->box.y, 0.0F),
                              std::max(collision->box.z, 0.0F)};
  const f32 half_x = box.x * 0.5F;
  const f32 half_z = box.z * 0.5F;
  return {
      {transform.position.x - half_x,
       transform.position.y,
       transform.position.z - half_z},
      {transform.position.x + half_x,
       transform.position.y + box.y,
       transform.position.z + half_z}};
}

[[nodiscard]] std::optional<EntityID> targeted_vehicle_entity(
    const world::WorldRuntime& runtime,
    const voxel::BlockRegistry& block_registry,
    const physics::ICollisionQuery& interaction_query,
    PlayerID player_id,
    Vec3f eye_position,
    Vec3f look_direction,
    f32 reach_distance) {
  look_direction = normalize_or_zero(look_direction);
  if (look_direction == Vec3f{}) {
    return std::nullopt;
  }

  f32 max_distance = std::max(0.0F, reach_distance);
  if (const std::optional<physics::BlockRaycastHit> block_hit =
          physics::raycast_blocks(
              interaction_query,
              eye_position,
              look_direction,
              max_distance);
      block_hit.has_value() && !block_registry.is_air(block_hit->variant)) {
    max_distance = std::min(max_distance, block_hit->distance);
  }

  const std::optional<EntityID> local_entity = runtime.entity_for_player(player_id);
  std::optional<EntityID> best_entity;
  f32 best_distance = max_distance;
  for (const EntityID entity : runtime.entity_ids()) {
    if (local_entity.has_value() && entity == local_entity.value()) {
      continue;
    }
    if (runtime.find_entity_vehicle_physics(entity) == nullptr ||
        runtime.find_entity_rideable(entity) == nullptr) {
      continue;
    }
    const entity::EntityTransform* transform = runtime.find_entity_transform(entity);
    if (transform == nullptr) {
      continue;
    }
    const std::optional<physics::RayAabbHit> hit =
        physics::ray_intersects_aabb(
            eye_position,
            look_direction,
            entity_interaction_bounds(
                *transform,
                runtime.find_entity_collision(entity)),
            best_distance);
    if (hit.has_value() && hit->distance <= best_distance) {
      best_distance = hit->distance;
      best_entity = entity;
    }
  }
  return best_entity;
}

[[nodiscard]] VehicleFuelInteractionResult handle_vehicle_fuel_interaction(
    world::WorldRuntime& runtime,
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const physics::ICollisionQuery& interaction_query,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry,
    const entity::VehicleDefinitionRegistry& vehicle_registry,
    Vec3f eye_position,
    Vec3f look_direction,
    f32 reach_distance) {
  if (!command.secondary_pressed) {
    return {};
  }
  const player::ItemStack* held = held_item_stack(player_state);
  if (held == nullptr || held->count == 0U) {
    return {};
  }
  const player::ItemRecord* held_item = item_registry.item(held->item);
  if (held_item == nullptr) {
    return {};
  }

  const std::optional<EntityID> target =
      targeted_vehicle_entity(
          runtime,
          block_registry,
          interaction_query,
          player_state.player_id,
          eye_position,
          look_direction,
          reach_distance);
  if (!target.has_value()) {
    return {};
  }

  entity::VehiclePhysicsComponent* physics =
      runtime.find_entity_vehicle_physics(target.value());
  if (physics == nullptr) {
    return {};
  }
  const entity::VehicleDefinition* definition =
      vehicle_registry.find(physics->definition);
  if (definition == nullptr || !definition->fuel.has_value() ||
      held_item->name != definition->fuel->item) {
    return {};
  }

  const entity::VehicleFuelDefinition& fuel = definition->fuel.value();
  physics->fuel_item = fuel.item;
  physics->fuel_capacity_seconds = fuel.capacity_seconds;
  physics->fuel_burn_per_second = fuel.burn_per_second;
  physics->fuel_seconds =
      std::clamp(physics->fuel_seconds, 0.0F, physics->fuel_capacity_seconds);

  if (physics->fuel_seconds + 0.001F >= physics->fuel_capacity_seconds) {
    return {
        .handled = true,
        .feedback_message = "Rocket fuel tank is already full."};
  }

  const f32 previous_fuel_seconds = physics->fuel_seconds;
  if (!player_state.inventory.consume_one(player_state.selected_hotbar_slot)) {
    return {};
  }
  physics->fuel_seconds =
      std::clamp(
          previous_fuel_seconds + fuel.capacity_seconds,
          0.0F,
          physics->fuel_capacity_seconds);

  std::ostringstream message;
  message << "Rocket fueled: "
          << static_cast<int>(std::round(physics->fuel_seconds))
          << "/"
          << static_cast<int>(std::round(physics->fuel_capacity_seconds))
          << " seconds.";
  return {.handled = true, .feedback_message = message.str()};
}

[[nodiscard]] std::optional<player::ItemID> mined_block_drop_item(
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockVariantRecord& target,
    bool drop_configured_item) {
  if (!drop_configured_item) {
    return std::nullopt;
  }

  const voxel::BlockRecord* block = block_registry.block(target.block_id);
  if (block == nullptr) {
    return std::nullopt;
  }

  switch (block->drops.kind) {
    case voxel::BlockDropKind::Default:
      return block->is_item ? item_registry.find_item(block->name) : std::nullopt;
    case voxel::BlockDropKind::None:
      return std::nullopt;
    case voxel::BlockDropKind::Item:
      return item_registry.find_item(block->drops.item_name);
  }

  return std::nullopt;
}

[[nodiscard]] bool allow_block_placement(const world::WorldRuntime& runtime) noexcept {
  const world::GameRuleState* rule = runtime.game_rule("allowBlockPlacement");
  return rule == nullptr ||
         rule->type != world::GameRuleValueType::Boolean ||
         rule->bool_value;
}

} // namespace

void ServerRuntimeImpl::apply_player_interaction_command(
    world::WorldRuntime& runtime,
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const physics::ICollisionQuery& interaction_query,
    Vec3f eye_position,
    Vec3f look_direction,
    const physics::KinematicConfig& physics_config,
    Seconds tick_seconds) {
  auto send_system_to_player = [this, player_id = player_state.player_id](
                                   std::string_view message) {
    if (message.empty()) {
      return;
    }
    const ServerSession* recipient = nullptr;
    sessions_.for_each_session([&](const ServerSession& session) {
      if (session.player_id == player_id && session.authenticated && !session.closing) {
        recipient = &session;
      }
    });
    if (recipient == nullptr) {
      return;
    }
    send_system_message(
        *recipient,
        arv::text::ChatMessageTemplate::parse(message).to_rich_text());
  };

  const MasonMarkerInteractionResult marker_interaction = handle_mason_marker_interaction(
      player_state,
      command,
      interaction_query,
      eye_position,
      look_direction,
      player_tuning_.reach_distance);
  if (marker_interaction.handled) {
    for (const std::string& message : marker_interaction.feedback_messages) {
      send_system_to_player(message);
    }
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  if (item_registry_ != nullptr && block_registry_ != nullptr &&
      vehicle_registry_ != nullptr) {
    const VehicleFuelInteractionResult fuel_interaction =
        handle_vehicle_fuel_interaction(
            runtime,
            player_state,
            command,
            interaction_query,
            *block_registry_,
            *item_registry_,
            *vehicle_registry_,
            eye_position,
            look_direction,
            player_tuning_.reach_distance);
    if (fuel_interaction.handled) {
      send_system_to_player(fuel_interaction.feedback_message);
      play_right_hand_interaction_animation(runtime, player_state.player_id);
      clear_block_break_attempt(player_state.player_id);
      return;
    }
  }

  tick_block_break_attempt(
      runtime,
      player_state,
      command,
      interaction_query,
      eye_position,
      look_direction,
      tick_seconds);

  if (command.secondary_pressed && player_state.resolved_permissions.core.can_modify_world) {
    const std::optional<player::ItemStack>& held_slot =
        player_state.inventory.slot(player_state.selected_hotbar_slot);
    if (!held_slot.has_value()) {
      return;
    }

    const player::ItemRecord* held_item = item_registry_->item(held_slot->item);
    if (held_item == nullptr) {
      return;
    }

    if (!fluid_containers_.empty_container_item_name.empty() &&
        held_item->name == fluid_containers_.empty_container_item_name) {
      const std::optional<BlockPos> source = raycast_source_fluid(
          runtime,
          *block_registry_,
          fluid_definition_,
          eye_position,
          look_direction,
          player_tuning_.reach_distance);
      if (!source.has_value()) {
        return;
      }
      std::optional<player::Inventory> next_inventory =
          inventory_after_bucket_fill(
              player_state,
              *item_registry_,
              fluid_containers_.filled_source_item_name);
      if (!next_inventory.has_value()) {
        return;
      }
      if (runtime.set_fluid(
              source.value(),
              voxel::empty_fluid_cell(),
              fluid_definition_)) {
        player_state.inventory = std::move(next_inventory.value());
        play_right_hand_interaction_animation(runtime, player_state.player_id);
      }
      return;
    }

    if (!fluid_containers_.filled_source_item_name.empty() &&
        held_item->name == fluid_containers_.filled_source_item_name) {
      if (!allow_block_placement(runtime)) {
        return;
      }
      auto hit = physics::raycast_blocks(
          interaction_query,
          eye_position,
          look_direction,
          player_tuning_.reach_distance);
      if (!hit.has_value()) {
        return;
      }

      BlockPos place_pos = hit->block;
      const voxel::BlockVariantID target_variant = runtime.get_block(hit->block);
      if (!block_registry_->is_replaceable(target_variant)) {
        place_pos.value = place_pos.value + voxel::block_face_offset(hit->face);
      }
      if (!interaction_query.is_block_loaded(place_pos)) {
        return;
      }

      if (!can_place_fluid_source_at(
              runtime,
              *block_registry_,
              fluid_definition_,
              place_pos)) {
        return;
      }

      std::optional<player::Inventory> next_inventory =
          inventory_after_water_place(
              player_state,
              *item_registry_,
              fluid_containers_.empty_container_item_name);
      if (!next_inventory.has_value()) {
        return;
      }
      if (runtime.place_fluid_source(place_pos, fluid_definition_)) {
        player_state.inventory = std::move(next_inventory.value());
        play_right_hand_interaction_animation(runtime, player_state.player_id);
      }
      return;
    }

    if (!held_item->placement_variant.has_value()) {
      return;
    }
    if (!allow_block_placement(runtime)) {
      return;
    }

    auto hit = physics::raycast_blocks(
        interaction_query,
        eye_position,
        look_direction,
        player_tuning_.reach_distance);
    if (!hit.has_value()) {
      return;
    }

    BlockPos place_pos = hit->block;
    const voxel::BlockVariantID target_variant = runtime.get_block(hit->block);
    if (!block_registry_->is_replaceable(target_variant)) {
      place_pos.value = place_pos.value + voxel::block_face_offset(hit->face);
    }

    if (!interaction_query.is_block_loaded(place_pos)) {
      return;
    }

    const voxel::BlockVariantID existing = runtime.get_block(place_pos);
    if (!(block_registry_->is_air(existing) || block_registry_->is_replaceable(existing))) {
      return;
    }

    const voxel::BlockVariantID placed_variant = resolve_placement_variant(
        *block_registry_,
        held_item->placement_variant.value(),
        hit.value(),
        place_pos,
        look_direction);
    const physics::PlayerCollisionPose player_pose =
        player_state.swimming
            ? physics::PlayerCollisionPose::Swimming
            : (player_state.sneaking ? physics::PlayerCollisionPose::Sneaking
                                     : physics::PlayerCollisionPose::Standing);
    const Aabb3f player_bounds =
        physics::make_body_bounds(player_state.position, physics_config, player_pose);
    for (const Aabb3f& local_box : block_registry_->collision_boxes(placed_variant)) {
      Aabb3f world_box{
          {local_box.min.x + static_cast<f32>(place_pos.value.x),
           local_box.min.y + static_cast<f32>(place_pos.value.y),
           local_box.min.z + static_cast<f32>(place_pos.value.z)},
          {local_box.max.x + static_cast<f32>(place_pos.value.x),
           local_box.max.y + static_cast<f32>(place_pos.value.y),
           local_box.max.z + static_cast<f32>(place_pos.value.z)}};
      if (player_bounds.overlaps(world_box)) {
        return;
      }
    }

    if (runtime.set_block(place_pos, placed_variant)) {
      (void)player_state.inventory.consume_one(player_state.selected_hotbar_slot);
      play_right_hand_interaction_animation(runtime, player_state.player_id);
    }
  }
}

void ServerRuntimeImpl::clear_block_break_attempt(PlayerID player_id) {
  block_break_attempts_.erase(player_id);
}

void ServerRuntimeImpl::tick_block_break_attempt(
    world::WorldRuntime& runtime,
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const physics::ICollisionQuery& interaction_query,
    Vec3f eye_position,
    Vec3f look_direction,
    Seconds tick_seconds) {
  if (command.secondary_pressed) {
    clear_block_break_attempt(player_state.player_id);
  }

  const bool wants_break = command.primary_pressed || command.primary_held;
  if (!wants_break) {
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  if (!player_state.resolved_permissions.core.can_modify_world) {
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  auto hit = physics::raycast_blocks(
      interaction_query,
      eye_position,
      look_direction,
      player_tuning_.reach_distance);
  if (!hit.has_value()) {
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  const voxel::BlockVariantID target_variant = runtime.get_block(hit->block);
  const voxel::BlockVariantRecord* target = block_registry_->variant(target_variant);
  if (target == nullptr || block_registry_->is_air(target_variant)) {
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  const f32 hardness = target->properties.hardness;
  if (!std::isfinite(hardness) || hardness < 0.0F) {
    clear_block_break_attempt(player_state.player_id);
    return;
  }

  const BlockBreakBehaviorEvaluation break_evaluation =
      player_state.resolved_permissions.core.has_creative_inventory
          ? BlockBreakBehaviorEvaluation{}
          : evaluate_block_break_behavior(
                player_state,
                *item_registry_,
                *block_registry_,
                *target,
                block_breaking_,
                block_break_behaviors_);

  f32 required_seconds = hardness * block_breaking_.base_seconds_per_hardness;
  if (player_state.resolved_permissions.core.has_creative_inventory) {
    required_seconds *= block_breaking_.creative_inventory_multiplier;
  } else {
    const f32 speed_multiplier = break_evaluation.speed_multiplier;
    if (!std::isfinite(speed_multiplier) || speed_multiplier <= 0.0F) {
      clear_block_break_attempt(player_state.player_id);
      return;
    }
    required_seconds /= speed_multiplier;
  }

  if (!std::isfinite(required_seconds) || required_seconds <= 0.0F) {
    if (runtime.set_block(hit->block, voxel::AirBlockVariantID)) {
      drop_and_remove_block_container(player_state.world_id, hit->block);
      if (!player_state.resolved_permissions.core.has_creative_inventory) {
        const std::optional<player::ItemID> drop_item =
            item_registry_ != nullptr
                ? mined_block_drop_item(
                      *item_registry_,
                      *block_registry_,
                      *target,
                      break_evaluation.drop_configured_item)
                : std::nullopt;
        if (drop_item.has_value()) {
          spawn_dropped_item_stack(
              player_state.world_id,
              {static_cast<f32>(hit->block.value.x) + 0.5F,
               static_cast<f32>(hit->block.value.y) + 0.5F,
               static_cast<f32>(hit->block.value.z) + 0.5F},
              player::ItemStack{drop_item.value(), 1U});
        }
      }
      clear_block_break_attempt(player_state.player_id);
    }
    return;
  }

  BlockBreakAttempt& attempt = block_break_attempts_[player_state.player_id];
  const player::ItemID held_item = held_item_id(player_state);
  if (!(attempt.block == hit->block) ||
      attempt.variant != target_variant ||
      attempt.held_item != held_item) {
    attempt = {};
    attempt.block = hit->block;
    attempt.variant = target_variant;
    attempt.held_item = held_item;
    attempt.required_seconds = required_seconds;
  } else {
    attempt.required_seconds = required_seconds;
  }

  attempt.elapsed_seconds += std::max(tick_seconds, 0.0F);
  attempt.progress = std::clamp(
      attempt.elapsed_seconds / std::max(attempt.required_seconds, 0.0001F),
      0.0F,
      1.0F);
  attempt.stage = break_stage_for_progress(attempt.progress);

  if (attempt.elapsed_seconds >= attempt.required_seconds &&
      runtime.set_block(hit->block, voxel::AirBlockVariantID)) {
    drop_and_remove_block_container(player_state.world_id, hit->block);
    if (!player_state.resolved_permissions.core.has_creative_inventory) {
      const std::optional<player::ItemID> drop_item =
          item_registry_ != nullptr
              ? mined_block_drop_item(
                    *item_registry_,
                    *block_registry_,
                    *target,
                    break_evaluation.drop_configured_item)
              : std::nullopt;
      if (drop_item.has_value()) {
        spawn_dropped_item_stack(
            player_state.world_id,
            {static_cast<f32>(hit->block.value.x) + 0.5F,
             static_cast<f32>(hit->block.value.y) + 0.5F,
             static_cast<f32>(hit->block.value.z) + 0.5F},
            player::ItemStack{drop_item.value(), 1U});
      }
    }
    clear_block_break_attempt(player_state.player_id);
  }
}

} // namespace arv::server::detail
