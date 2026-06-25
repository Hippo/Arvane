#include "client_input_runtime.hpp"

#include "client_entity_mirror.hpp"
#include "client_inventory_runtime.hpp"
#include "client_protocol_send.hpp"
#include "client_ui_state.hpp"

#include "arv/entity/components.hpp"
#include "arv/physics/collision_resolution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace arv::client::detail {
namespace {

inline constexpr f32 VehicleGravityBlocksPerSecondSquared = 32.0F;
inline constexpr f32 VehicleTerminalVelocityBlocksPerSecond = 55.0F;
inline constexpr f32 VehicleImpactProjectedStopSpeedBlocksPerSecond = 0.02F;
inline constexpr u8 VehicleImpactMemoryTicks = 8U;
inline constexpr Seconds DropItemRepeatInitialDelaySeconds = 0.35;
inline constexpr Seconds DropItemRepeatIntervalSeconds = 0.10;
inline constexpr u32 MaxDropItemRequestsPerTick = 4U;

[[nodiscard]] bool command_chat_shortcut_requested(const InputFrame& input) noexcept {
  return !input.text_input.empty() && input.text_input.front() == '/';
}

[[nodiscard]] bool player_menu_search_is_receiving_text(
    const ClientUiState& ui,
    const InputFrame& input) noexcept {
  return ui.inventory_open() &&
         ui.player_menu_tab() == PlayerMenuTab::Crafting &&
         (!input.text_input.empty() ||
          input.text_backspace_pressed ||
          !input.text_edit_events.empty());
}

[[nodiscard]] bool player_menu_settings_subpage_is_open(
    const ClientUiState& ui) noexcept {
  return ui.inventory_open() &&
         ui.player_menu_tab() == PlayerMenuTab::Settings &&
         ui.player_menu_settings_section() != PlayerMenuSettingsSection::Overview;
}

[[nodiscard]] bool has_action(protocol::PlayerActionFlags flags) noexcept {
  return flags != protocol::PlayerActionFlags::None;
}

[[nodiscard]] f32 approach(f32 value, f32 target, f32 max_delta) noexcept {
  if (value < target) {
    return std::min(value + max_delta, target);
  }
  return std::max(value - max_delta, target);
}

[[nodiscard]] f32 wrap_angle_radians(f32 radians) noexcept {
  while (radians > PiRadians) {
    radians -= TauRadians;
  }
  while (radians < -PiRadians) {
    radians += TauRadians;
  }
  return radians;
}

[[nodiscard]] f32 approach_angle(f32 value, f32 target, f32 max_delta) noexcept {
  const f32 delta = wrap_angle_radians(target - value);
  if (std::abs(delta) <= max_delta) {
    return target;
  }
  return wrap_angle_radians(value + (delta < 0.0F ? -max_delta : max_delta));
}

[[nodiscard]] bool has_horizontal_vector(
    Vec3f value,
    f32 epsilon = 1.0e-5F) noexcept {
  return horizontal_length_squared(value) > epsilon * epsilon;
}

[[nodiscard]] f32 yaw_from_rotation(Quatf rotation) noexcept {
  const Quatf normalized = normalize_quat(rotation);
  const f32 sin_yaw =
      2.0F * (normalized.w * normalized.y + normalized.x * normalized.z);
  const f32 cos_yaw =
      1.0F - 2.0F * (normalized.y * normalized.y + normalized.z * normalized.z);
  return std::atan2(sin_yaw, cos_yaw);
}

[[nodiscard]] f32 vehicle_gameplay_yaw_from_rotation(Quatf rotation) noexcept {
  return wrap_angle_radians(yaw_from_rotation(rotation) - ModelForwardYawOffsetRadians);
}

[[nodiscard]] Quatf vehicle_render_rotation_from_gameplay_yaw(f32 yaw_radians) noexcept {
  return model_forward_yaw_rotation(yaw_radians);
}

[[nodiscard]] Vec3f model_visual_forward(Quatf rotation) noexcept {
  return rotate_vector(normalize_quat(rotation), {0.0F, 0.0F, -1.0F});
}

struct VehicleMovementProfile {
  entity::VehiclePhysicsKind physics{entity::VehiclePhysicsKind::LivingMount};
  f32 min_forward_speed{};
  f32 max_forward_speed{4.0F};
  f32 max_reverse_speed{4.0F};
  f32 acceleration{8.0F};
  f32 brake_force{10.0F};
  f32 turn_rate{1.5F};
  f32 drag{0.9F};
  f32 lateral_grip{1.0F};
  f32 gravity_scale{1.0F};
  f32 step_height{};
  f32 pitch_rate{};
  f32 yaw_rate{};
  f32 roll_rate{};
  f32 roll_auto_level_rate{};
  f32 lift_strength{};
  f32 stall_speed{};
  f32 stall_gravity_scale{1.0F};
  f32 max_climb_rate{};
  f32 max_descent_rate{};
  f32 ground_takeoff_speed{};
  f32 ground_drag{0.9F};
  f32 ascend_rate{};
  f32 descend_rate{};
  f32 water_drag{0.9F};
  f32 air_drag{0.5F};
  f32 gravity_scale_out_of_water{1.0F};
  f32 min_water_occupancy{};
  f32 preferred_water_occupancy{1.0F};
  Vec3f collision_size{1.0F, 1.0F, 1.0F};
  protocol::VehicleMoveFlags base_flags{protocol::VehicleMoveFlags::OnGround};
};

[[nodiscard]] VehicleMovementProfile vehicle_movement_profile(
    const ReplicatedEntityMirror& mirror,
    const entity::VehicleDefinitionRegistry* registry,
    const player::PlayerCommand& command) noexcept {
  VehicleMovementProfile profile;
  const entity::VehicleDefinition* definition =
      registry != nullptr ? registry->find(mirror.entity_type_name) : nullptr;
  if (definition == nullptr) {
    return profile;
  }

  profile.physics = definition->physics;
  profile.collision_size = definition->collision_size;
  switch (definition->physics) {
    case entity::VehiclePhysicsKind::LivingMount:
      profile.max_forward_speed = command.sprint_held
                                      ? definition->living_mount.sprint_speed
                                      : definition->living_mount.walk_speed;
      profile.max_reverse_speed = definition->living_mount.walk_speed * 0.4F;
      profile.acceleration = definition->living_mount.acceleration;
      profile.brake_force = std::max(definition->living_mount.acceleration, 1.0F);
      profile.turn_rate = definition->living_mount.turn_rate;
      profile.drag = definition->living_mount.friction;
      profile.gravity_scale = definition->living_mount.gravity_scale;
      profile.step_height = definition->living_mount.step_height;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      profile.max_forward_speed = definition->surface_water.max_speed;
      profile.max_reverse_speed = definition->surface_water.max_speed * 0.35F;
      profile.acceleration = definition->surface_water.acceleration;
      profile.brake_force = definition->surface_water.acceleration +
                            std::max(definition->surface_water.water_drag, 0.0F);
      profile.turn_rate = definition->surface_water.turn_rate;
      profile.drag = definition->surface_water.water_drag;
      profile.base_flags = protocol::VehicleMoveFlags::InWater;
      break;
    case entity::VehiclePhysicsKind::GroundVehicle:
      profile.max_forward_speed = definition->ground.max_forward_speed;
      profile.max_reverse_speed = definition->ground.max_reverse_speed;
      profile.acceleration = definition->ground.acceleration;
      profile.brake_force = definition->ground.brake_force;
      profile.turn_rate = definition->ground.turn_rate;
      profile.drag = definition->ground.drag;
      profile.lateral_grip = definition->ground.lateral_grip;
      profile.gravity_scale = definition->ground.gravity_scale;
      profile.step_height = definition->ground.max_step_height;
      profile.base_flags = protocol::VehicleMoveFlags::OnGround;
      break;
    case entity::VehiclePhysicsKind::Aircraft:
      profile.min_forward_speed = definition->aircraft.min_speed;
      profile.max_forward_speed = definition->aircraft.max_speed;
      profile.max_reverse_speed = 0.0F;
      profile.acceleration = definition->aircraft.acceleration;
      profile.brake_force = definition->aircraft.brake_deceleration;
      profile.drag = definition->aircraft.drag;
      profile.gravity_scale = definition->aircraft.gravity_scale;
      profile.pitch_rate = definition->aircraft.pitch_rate;
      profile.yaw_rate = definition->aircraft.yaw_rate;
      profile.roll_rate = definition->aircraft.roll_rate;
      profile.roll_auto_level_rate = definition->aircraft.roll_auto_level_rate;
      profile.lift_strength = definition->aircraft.lift_strength;
      profile.stall_speed = definition->aircraft.stall_speed;
      profile.stall_gravity_scale = definition->aircraft.stall_gravity_scale;
      profile.max_climb_rate = definition->aircraft.max_climb_rate;
      profile.max_descent_rate = definition->aircraft.max_descent_rate;
      profile.ground_takeoff_speed = definition->aircraft.ground_takeoff_speed;
      profile.ground_drag = definition->aircraft.ground_drag;
      profile.base_flags = protocol::VehicleMoveFlags::None;
      break;
    case entity::VehiclePhysicsKind::Submersible:
      profile.max_forward_speed = definition->submersible.max_forward_speed;
      profile.max_reverse_speed = definition->submersible.max_reverse_speed;
      profile.acceleration = definition->submersible.acceleration;
      profile.brake_force = definition->submersible.brake_deceleration;
      profile.turn_rate = definition->submersible.turn_rate;
      profile.pitch_rate = definition->submersible.pitch_rate;
      profile.ascend_rate = definition->submersible.ascend_rate;
      profile.descend_rate = definition->submersible.descend_rate;
      profile.water_drag = definition->submersible.water_drag;
      profile.air_drag = definition->submersible.air_drag;
      profile.gravity_scale_out_of_water = definition->submersible.gravity_scale_out_of_water;
      profile.min_water_occupancy = definition->submersible.min_water_occupancy;
      profile.preferred_water_occupancy = definition->submersible.preferred_water_occupancy;
      profile.base_flags = protocol::VehicleMoveFlags::InWater;
      break;
  }

  profile.min_forward_speed = std::max(profile.min_forward_speed, 0.0F);
  profile.max_forward_speed = std::max(profile.max_forward_speed, 0.0F);
  profile.max_reverse_speed = std::max(profile.max_reverse_speed, 0.0F);
  profile.acceleration = std::max(profile.acceleration, 0.0F);
  profile.brake_force = std::max(profile.brake_force, profile.acceleration);
  profile.turn_rate = std::max(profile.turn_rate, 0.0F);
  profile.drag = std::clamp(profile.drag, 0.0F, 1.0F);
  profile.lateral_grip = std::clamp(profile.lateral_grip, 0.0F, 1.0F);
  profile.gravity_scale = std::max(profile.gravity_scale, 0.0F);
  profile.step_height = std::max(profile.step_height, 0.0F);
  profile.pitch_rate = std::max(profile.pitch_rate, 0.0F);
  profile.yaw_rate = std::max(profile.yaw_rate, 0.0F);
  profile.roll_rate = std::max(profile.roll_rate, 0.0F);
  profile.roll_auto_level_rate = std::max(profile.roll_auto_level_rate, 0.0F);
  profile.lift_strength = std::max(profile.lift_strength, 0.0F);
  profile.stall_speed = std::max(profile.stall_speed, 0.0F);
  profile.stall_gravity_scale = std::max(profile.stall_gravity_scale, 0.0F);
  profile.max_climb_rate = std::max(profile.max_climb_rate, 0.0F);
  profile.max_descent_rate = std::max(profile.max_descent_rate, 0.0F);
  profile.ground_takeoff_speed = std::max(profile.ground_takeoff_speed, 0.0F);
  profile.ground_drag = std::clamp(profile.ground_drag, 0.0F, 1.0F);
  profile.ascend_rate = std::max(profile.ascend_rate, 0.0F);
  profile.descend_rate = std::max(profile.descend_rate, 0.0F);
  profile.water_drag = std::clamp(profile.water_drag, 0.0F, 1.0F);
  profile.air_drag = std::clamp(profile.air_drag, 0.0F, 1.0F);
  profile.gravity_scale_out_of_water = std::max(profile.gravity_scale_out_of_water, 0.0F);
  profile.min_water_occupancy = std::clamp(profile.min_water_occupancy, 0.0F, 1.0F);
  profile.preferred_water_occupancy =
      std::clamp(profile.preferred_water_occupancy, profile.min_water_occupancy, 1.0F);
  profile.collision_size = {
      std::max(profile.collision_size.x, 0.01F),
      std::max(profile.collision_size.y, 0.01F),
      std::max(profile.collision_size.z, 0.01F)};
  return profile;
}

[[nodiscard]] std::string_view vehicle_motion_animation_name(
    const VehicleMovementProfile& profile,
    Quatf rotation,
    Vec3f velocity) noexcept {
  if (profile.physics != entity::VehiclePhysicsKind::GroundVehicle) {
    return {};
  }
  constexpr f32 AnimationSpeedEpsilonBlocksPerSecond = 0.05F;
  const f32 signed_speed = dot(velocity, model_visual_forward(rotation));
  if (signed_speed > AnimationSpeedEpsilonBlocksPerSecond) {
    return "driving";
  }
  if (signed_speed < -AnimationSpeedEpsilonBlocksPerSecond) {
    return "reverse";
  }
  return {};
}

[[nodiscard]] Aabb3f vehicle_bounds(Vec3f position, Vec3f size) noexcept {
  const Vec3f half{size.x * 0.5F, 0.0F, size.z * 0.5F};
  return {
      {position.x - half.x, position.y, position.z - half.z},
      {position.x + half.x, position.y + size.y, position.z + half.z}};
}

[[nodiscard]] bool vehicle_has_ground_support(
    const physics::ICollisionQuery& collision_query,
    Vec3f position,
    Vec3f size) noexcept {
  return physics::has_support_below(
      collision_query,
      vehicle_bounds(position, size),
      std::max(physics::CollisionEpsilon * 2.0F, 0.05F));
}

[[nodiscard]] std::optional<physics::MovementResolution> try_vehicle_step_up(
    const physics::ICollisionQuery& collision_query,
    const Aabb3f& bounds,
    Vec3f attempted_delta,
    const physics::MovementResolution& normal_movement,
    f32 step_height) {
  constexpr f32 Epsilon = physics::CollisionEpsilon;
  const Vec3f horizontal_delta{attempted_delta.x, 0.0F, attempted_delta.z};
  if (step_height <= Epsilon ||
      horizontal_length_squared(horizontal_delta) <= Epsilon * Epsilon ||
      attempted_delta.y > Epsilon ||
      !(normal_movement.hit_x || normal_movement.hit_z)) {
    return std::nullopt;
  }

  const physics::MovementResolution lift =
      physics::resolve_movement(collision_query, bounds, {0.0F, step_height, 0.0F});
  if (lift.applied_delta.y <= Epsilon) {
    return std::nullopt;
  }

  const Aabb3f lifted_bounds = translated(bounds, lift.applied_delta);
  const physics::MovementResolution horizontal =
      physics::resolve_movement(collision_query, lifted_bounds, horizontal_delta);
  const f32 normal_horizontal_distance =
      horizontal_length_squared(normal_movement.applied_delta);
  const f32 stepped_horizontal_distance =
      horizontal_length_squared(horizontal.applied_delta);
  if (horizontal.hit_x || horizontal.hit_z ||
      stepped_horizontal_distance <= normal_horizontal_distance + Epsilon * Epsilon) {
    return std::nullopt;
  }

  const Aabb3f after_horizontal = translated(lifted_bounds, horizontal.applied_delta);
  const physics::MovementResolution settle =
      physics::resolve_movement(
          collision_query,
          after_horizontal,
          {0.0F, -lift.applied_delta.y, 0.0F});
  const Aabb3f settled_bounds = translated(after_horizontal, settle.applied_delta);
  if (!settle.hit_down &&
      !physics::has_support_below(collision_query, settled_bounds, Epsilon * 2.0F)) {
    return std::nullopt;
  }

  physics::MovementResolution stepped;
  stepped.attempted_delta = attempted_delta;
  stepped.applied_delta = {
      horizontal.applied_delta.x,
      lift.applied_delta.y + settle.applied_delta.y,
      horizontal.applied_delta.z};
  stepped.hit_x = horizontal.hit_x;
  stepped.hit_y = lift.hit_y || settle.hit_y;
  stepped.hit_z = horizontal.hit_z;
  stepped.hit_down = settle.hit_down;
  stepped.grounded = true;
  stepped.resolved_bounds = settled_bounds;
  return stepped;
}

[[nodiscard]] bool vehicle_sample_contains_fluid(
    const physics::ICollisionQuery& collision_query,
    Vec3f sample) noexcept {
  const BlockPos block{{
      floor_to_i32(sample.x),
      floor_to_i32(sample.y),
      floor_to_i32(sample.z),
  }};
  return collision_query.is_block_loaded(block) &&
         !collision_query.fluid_cell(block).empty();
}

[[nodiscard]] f32 vehicle_water_occupancy(
    const physics::ICollisionQuery* collision_query,
    Vec3f position,
    Vec3f size,
    f32 fallback) noexcept {
  if (collision_query == nullptr) {
    return std::clamp(fallback, 0.0F, 1.0F);
  }
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  const f32 mid_y = position.y + size.y * 0.5F;
  const f32 top_y = position.y + size.y * 0.9F;
  const f32 bottom_y = position.y + size.y * 0.1F;
  const std::array<Vec3f, 11> samples{{
      {position.x, mid_y, position.z},
      {position.x, bottom_y, position.z},
      {position.x, top_y, position.z},
      {position.x, mid_y, position.z - half_z},
      {position.x, mid_y, position.z + half_z},
      {position.x - half_x, mid_y, position.z},
      {position.x + half_x, mid_y, position.z},
      {position.x - half_x, mid_y, position.z - half_z},
      {position.x + half_x, mid_y, position.z - half_z},
      {position.x - half_x, mid_y, position.z + half_z},
      {position.x + half_x, mid_y, position.z + half_z},
  }};
  u32 wet_samples = 0U;
  for (const Vec3f& sample : samples) {
    if (vehicle_sample_contains_fluid(*collision_query, sample)) {
      ++wet_samples;
    }
  }
  return static_cast<f32>(wet_samples) / static_cast<f32>(samples.size());
}

[[nodiscard]] f32 normalized_look_axis(i16 value, f32 divisor) noexcept {
  return std::clamp(static_cast<f32>(value) / divisor, -1.0F, 1.0F);
}

[[nodiscard]] Quatf apply_local_vehicle_rotation(
    Quatf rotation,
    f32 pitch_radians,
    f32 yaw_radians,
    f32 roll_radians) noexcept {
  const Quatf delta =
      multiply_quat(
          pitch_rotation(pitch_radians),
          multiply_quat(yaw_rotation(yaw_radians), roll_rotation(roll_radians)));
  return normalize_quat(multiply_quat(normalize_quat(rotation), delta));
}

[[nodiscard]] Vec3f vehicle_horizontal_impact_normal_from_delta(Vec3f delta) noexcept {
  Vec3f normal{};
  if (std::abs(delta.x) > physics::CollisionEpsilon) {
    normal.x = delta.x > 0.0F ? -1.0F : 1.0F;
  }
  if (std::abs(delta.z) > physics::CollisionEpsilon) {
    normal.z = delta.z > 0.0F ? -1.0F : 1.0F;
  }
  return normal;
}

[[nodiscard]] Vec3f vehicle_horizontal_impact_normal(
    const physics::MovementResolution& resolved,
    Vec3f attempted_delta) noexcept {
  Vec3f impact_delta{};
  if (resolved.hit_x) {
    impact_delta.x = attempted_delta.x;
  }
  if (resolved.hit_z) {
    impact_delta.z = attempted_delta.z;
  }
  return vehicle_horizontal_impact_normal_from_delta(impact_delta);
}

[[nodiscard]] std::optional<Vec3f> vehicle_impact_normal(
    const physics::MovementResolution& resolved,
    Vec3f pre_impact_velocity) noexcept {
  Vec3f best_normal{};
  f32 best_impact = 0.0F;
  const auto consider = [&](Vec3f normal) {
    const f32 impact = std::max(0.0F, dot(pre_impact_velocity * -1.0F, normal));
    if (impact > best_impact) {
      best_impact = impact;
      best_normal = normal;
    }
  };
  if (resolved.hit_x) {
    const f32 direction =
        std::abs(resolved.attempted_delta.x) > 0.0001F
            ? resolved.attempted_delta.x
            : pre_impact_velocity.x;
    consider({direction > 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F});
  }
  if (resolved.hit_y) {
    const f32 direction =
        std::abs(resolved.attempted_delta.y) > 0.0001F
            ? resolved.attempted_delta.y
            : pre_impact_velocity.y;
    consider({0.0F, direction > 0.0F ? -1.0F : 1.0F, 0.0F});
  }
  if (resolved.hit_z) {
    const f32 direction =
        std::abs(resolved.attempted_delta.z) > 0.0001F
            ? resolved.attempted_delta.z
            : pre_impact_velocity.z;
    consider({0.0F, 0.0F, direction > 0.0F ? -1.0F : 1.0F});
  }
  if (best_impact <= 0.0F) {
    return std::nullopt;
  }
  return best_normal;
}

[[nodiscard]] Vec3f project_velocity_out_of_impact(
    Vec3f velocity,
    Vec3f impact_normal) noexcept {
  if (!has_horizontal_vector(impact_normal)) {
    return velocity;
  }
  if (impact_normal.x != 0.0F && velocity.x * impact_normal.x < 0.0F) {
    velocity.x = 0.0F;
  }
  if (impact_normal.z != 0.0F && velocity.z * impact_normal.z < 0.0F) {
    velocity.z = 0.0F;
  }
  if (horizontal_length(velocity) < VehicleImpactProjectedStopSpeedBlocksPerSecond) {
    velocity.x = 0.0F;
    velocity.z = 0.0F;
  }
  return velocity;
}

[[nodiscard]] const entity::VehicleSeatDefinition* find_vehicle_seat(
    const entity::VehicleDefinition* definition,
    std::string_view seat_id) noexcept {
  if (definition == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(
      definition->seats.begin(),
      definition->seats.end(),
      [&](const entity::VehicleSeatDefinition& seat) {
        return seat.id == seat_id;
      });
  return found == definition->seats.end() ? nullptr : &*found;
}

[[nodiscard]] bool should_reset_vehicle_prediction(
    EntityNetID vehicle_id,
    const ReplicatedEntityMirror& mirror,
    const std::optional<EntityNetID>& predicted_vehicle_id,
    Vec3f predicted_position,
    Vec3f predicted_velocity) noexcept {
  if (!predicted_vehicle_id.has_value() || predicted_vehicle_id.value() != vehicle_id) {
    return true;
  }
  if (distance(predicted_position, mirror.position) > 2.0F) {
    return true;
  }
  return distance(predicted_velocity, mirror.velocity) > 6.0F;
}

} // namespace

void ClientInputRuntime::submit_input(
    InputFrame input,
    ClientUiState& ui,
    LocalPlayerController& player_controller,
    ClientProtocolSender& sender,
    net::ConnectionState connection_state) {
  ui.set_tab_menu_visible(input.tab_menu_held);
  const bool was_chat_open = ui.chat_open();
  const bool was_inventory_open = ui.inventory_open();

  const bool inventory_back_pressed =
      input.open_inventory_pressed && player_menu_settings_subpage_is_open(ui);
  if ((input.text_cancel_pressed || inventory_back_pressed) &&
      player_menu_settings_subpage_is_open(ui)) {
    ui.set_player_menu_settings_section(PlayerMenuSettingsSection::Overview);
  } else if (input.text_cancel_pressed && (ui.inventory_open() || ui.chat_open())) {
    ui.close_ui();
  } else if (!ui.chat_open() && input.open_inventory_pressed &&
             !player_menu_search_is_receiving_text(ui, input)) {
    ui.toggle_inventory_if_allowed(player_controller.resolved_permissions());
  }
  if (!was_chat_open && input.open_chat_pressed) {
    ui.open_chat();
  } else if (!was_chat_open && command_chat_shortcut_requested(input)) {
    ui.open_chat_with_draft("/");
  }

  std::optional<std::string> chat_submission = ui.handle_chat_input(input, was_chat_open);
  ui.handle_player_menu_input(input, was_inventory_open);
  if (chat_submission.has_value() && connection_state == net::ConnectionState::InGame) {
    (void)sender.send_chat_message({std::move(chat_submission.value()), std::nullopt});
  }

  if (ui.inventory_open() || ui.chat_open()) {
    input.move_forward = false;
    input.move_backward = false;
    input.move_left = false;
    input.move_right = false;
    input.jump_pressed = false;
    input.jump_held = false;
    input.sneak_held = false;
    input.sprint_held = false;
    input.toggle_fly_pressed = false;
    input.mount_pressed = false;
    input.vehicle_roll_left_held = false;
    input.vehicle_roll_right_held = false;
    input.primary_pressed = false;
    input.primary_held = false;
    input.secondary_pressed = false;
    input.secondary_held = false;
    input.drop_item_pressed = false;
    input.drop_item_held = false;
    input.hotbar_select.reset();
    input.hotbar_scroll_steps = 0;
    input.mouse_delta_x = 0.0F;
    input.mouse_delta_y = 0.0F;
  }

  if (input.drop_item_pressed) {
    drop_item_pressed_ = true;
  }
  drop_item_held_ = input.drop_item_held;

  player_controller.submit_input(std::move(input));
}

void ClientInputRuntime::send_fixed_tick_input(
    TickIndex tick_index,
    LocalPlayerController& player_controller,
    ClientInventoryRuntime& inventory,
    ClientProtocolSender& sender,
    ClientEntityMirrorStore& entities,
    const entity::VehicleDefinitionRegistry* vehicle_registry,
    const physics::ICollisionQuery* collision_query,
    Seconds tick_seconds) {
  u32 drop_requests = 0U;
  if (drop_item_pressed_) {
    drop_requests = 1U;
    drop_item_held_seconds_ = 0.0;
    drop_item_repeat_accumulator_seconds_ = 0.0;
  }
  if (drop_item_held_) {
    drop_item_held_seconds_ += tick_seconds;
    if (!drop_item_pressed_ &&
        drop_item_held_seconds_ >= DropItemRepeatInitialDelaySeconds) {
      drop_item_repeat_accumulator_seconds_ += tick_seconds;
      while (drop_item_repeat_accumulator_seconds_ >= DropItemRepeatIntervalSeconds &&
             drop_requests < MaxDropItemRequestsPerTick) {
        drop_item_repeat_accumulator_seconds_ -= DropItemRepeatIntervalSeconds;
        ++drop_requests;
      }
    }
  } else {
    drop_item_held_seconds_ = 0.0;
    drop_item_repeat_accumulator_seconds_ = 0.0;
  }
  drop_item_pressed_ = false;
  for (u32 index = 0U; index < drop_requests; ++index) {
    (void)inventory.request_drop_slot(
        sender,
        player_controller.selected_hotbar_slot(),
        net::ConnectionState::InGame);
  }

  const player::PlayerCommand command = player_controller.consume_command(tick_index);
  if (player_controller.mounted_vehicle_id().has_value()) {
    if (command.mount_pressed) {
      if (!dismount_request_sent_) {
        (void)sender.send_dismount_request(
            {player_controller.mounted_vehicle_id().value()});
        dismount_request_sent_ = true;
      }
      predicted_vehicle_id_.reset();
      clear_vehicle_impact_memory();
      clear_vehicle_debug_telemetry();
      return;
    }
    dismount_request_sent_ = false;
    if (!player_controller.mounted_controls_vehicle()) {
      predicted_vehicle_id_.reset();
      clear_vehicle_impact_memory();
      clear_vehicle_debug_telemetry();
      return;
    }
    const EntityNetID vehicle_id = player_controller.mounted_vehicle_id().value();
    const ReplicatedEntityMirror* mirror = entities.find(vehicle_id);
    if (mirror == nullptr) {
      predicted_vehicle_id_.reset();
      clear_vehicle_impact_memory();
      clear_vehicle_debug_telemetry();
      return;
    }
    const entity::VehicleDefinition* definition =
        vehicle_registry == nullptr ? nullptr : vehicle_registry->find(mirror->entity_type_name);
    const entity::VehicleSeatDefinition* seat =
        find_vehicle_seat(definition, player_controller.mounted_seat_id());
    const VehicleMovementProfile profile =
        vehicle_movement_profile(*mirror, vehicle_registry, command);
    if (should_reset_vehicle_prediction(
            vehicle_id,
            *mirror,
            predicted_vehicle_id_,
            predicted_vehicle_position_,
            predicted_vehicle_velocity_)) {
      predicted_vehicle_id_ = vehicle_id;
      predicted_vehicle_position_ = mirror->position;
      predicted_vehicle_velocity_ = mirror->velocity;
      predicted_vehicle_rotation_ = mirror->rotation;
      predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(mirror->rotation);
      predicted_vehicle_animation_name_.clear();
      predicted_vehicle_animation_time_ = 0.0;
      clear_vehicle_impact_memory();
    }

    const Vec3f debug_previous_velocity = predicted_vehicle_velocity_;
    movement_send_accumulator_seconds_ += tick_seconds;
    const Seconds send_interval =
        1.0 / static_cast<Seconds>(std::max(movement_send_rate_hz_, 1.0F));
    const bool send_this_tick =
        movement_send_accumulator_seconds_ >= send_interval ||
        command.jump_pressed || command.secondary_pressed || command.mount_pressed;

    const f32 dt = std::clamp(static_cast<f32>(tick_seconds), 1.0F / 240.0F, 0.1F);
    const f32 throttle = std::clamp(static_cast<f32>(command.move_z), -1.0F, 1.0F);
    const f32 steer = std::clamp(static_cast<f32>(command.move_x), -1.0F, 1.0F);
    const f32 previous_yaw = predicted_vehicle_yaw_;
    const bool living_mount =
        profile.physics == entity::VehiclePhysicsKind::LivingMount;
    const bool ground_vehicle =
        profile.physics == entity::VehiclePhysicsKind::GroundVehicle;
    const bool aircraft =
        profile.physics == entity::VehiclePhysicsKind::Aircraft;
    const bool submersible =
        profile.physics == entity::VehiclePhysicsKind::Submersible;
    if (!ground_vehicle) {
      clear_vehicle_impact_memory();
    }
    bool vehicle_grounded = protocol::has_vehicle_move_flag(
        profile.base_flags,
        protocol::VehicleMoveFlags::OnGround);
    if ((living_mount || ground_vehicle || aircraft || submersible) &&
        collision_query != nullptr) {
      vehicle_grounded = vehicle_has_ground_support(
          *collision_query,
          predicted_vehicle_position_,
          profile.collision_size);
    }
    f32 water_occupancy = 0.0F;
    bool submersible_in_water = false;
    protocol::VehicleMoveFlags predicted_flags = protocol::VehicleMoveFlags::None;
    Vec3f reported_angular_velocity{};
    Vec3f attempted_delta{};

    if (aircraft) {
      const bool near_takeoff_surface =
          vehicle_grounded ||
          (collision_query != nullptr &&
           physics::has_support_below(
               *collision_query,
               vehicle_bounds(predicted_vehicle_position_, profile.collision_size),
               0.35F));
      const Vec3f speed_axis =
          near_takeoff_surface
              ? horizontal_forward(predicted_vehicle_yaw_)
              : model_visual_forward(predicted_vehicle_rotation_);
      f32 forward_speed = dot(predicted_vehicle_velocity_, speed_axis);
      if (!std::isfinite(forward_speed)) {
        forward_speed = 0.0F;
      }
      if (command.secondary_held) {
        forward_speed = approach(forward_speed, 0.0F, profile.brake_force * dt);
      } else if (throttle > 0.01F) {
        forward_speed = std::max(forward_speed, profile.min_forward_speed);
        forward_speed += throttle * profile.acceleration * dt;
      } else if (throttle < -0.01F) {
        forward_speed =
            approach(forward_speed, 0.0F, profile.brake_force * -throttle * dt);
      } else {
        const f32 drag =
            vehicle_grounded ? profile.ground_drag : profile.drag;
        forward_speed *= std::pow(drag, std::max(dt, 0.0F) * 60.0F);
        if (std::abs(forward_speed) < 0.02F) {
          forward_speed = 0.0F;
        }
      }
      forward_speed = std::clamp(
          forward_speed,
          profile.min_forward_speed,
          profile.max_forward_speed);

      const f32 pitch_input = std::clamp(
          (command.jump_held ? 1.0F : 0.0F) -
              (command.sneak_held ? 1.0F : 0.0F),
          -1.0F,
          1.0F);
      const f32 yaw_input = steer;
      const f32 roll_input = std::clamp(
          (command.vehicle_roll_right_held ? 1.0F : 0.0F) -
              (command.vehicle_roll_left_held ? 1.0F : 0.0F),
          -1.0F,
          1.0F);
      const bool stationary_without_collision_query =
          collision_query == nullptr &&
          length(predicted_vehicle_velocity_) <= 0.05F &&
          std::abs(throttle) <= 0.01F &&
          std::abs(steer) <= 0.01F &&
          std::abs(pitch_input) <= 0.01F &&
          std::abs(roll_input) <= 0.01F &&
          !command.secondary_held;
      f32 roll_delta = roll_input * profile.roll_rate * dt;
      if (std::abs(roll_input) < 0.01F && profile.roll_auto_level_rate > 0.0F) {
        const Vec3f right = rotate_vector(
            normalize_quat(predicted_vehicle_rotation_),
            {1.0F, 0.0F, 0.0F});
        roll_delta += std::clamp(
                          -right.y,
                          -1.0F,
                          1.0F) *
                      profile.roll_auto_level_rate * dt;
      }
      const f32 pitch_delta = pitch_input * profile.pitch_rate * dt;
      const f32 yaw_delta = yaw_input * profile.yaw_rate * dt;
      predicted_vehicle_rotation_ =
          apply_local_vehicle_rotation(
              predicted_vehicle_rotation_,
              pitch_delta,
              yaw_delta,
              roll_delta);
      predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(predicted_vehicle_rotation_);
      reported_angular_velocity = {pitch_delta / dt, yaw_delta / dt, roll_delta / dt};

      Vec3f forward = normalize_or_zero(model_visual_forward(predicted_vehicle_rotation_));
      if (length(forward) <= 0.0F) {
        forward = horizontal_forward(predicted_vehicle_yaw_);
      }
      if (near_takeoff_surface) {
        Vec3f ground_forward{forward.x, 0.0F, forward.z};
        if (length(ground_forward) <= 0.0F) {
          ground_forward = horizontal_forward(predicted_vehicle_yaw_);
        } else {
          ground_forward = normalize_or_zero(ground_forward);
        }
        predicted_vehicle_velocity_ = ground_forward * forward_speed;
      } else {
        predicted_vehicle_velocity_ = forward * forward_speed;
      }
      const f32 assisted_takeoff_speed = profile.ground_takeoff_speed * 0.6F;
      const bool takeoff_ready =
          !near_takeoff_surface ||
          forward_speed >= profile.ground_takeoff_speed ||
          (command.jump_held && throttle > 0.01F &&
           forward_speed >= assisted_takeoff_speed);
      if (stationary_without_collision_query) {
        predicted_vehicle_velocity_ = {};
      } else if (takeoff_ready) {
        const f32 speed_fraction =
            profile.max_forward_speed <= 0.0F
                ? 0.0F
                : std::clamp(forward_speed / profile.max_forward_speed, 0.0F, 1.0F);
        predicted_vehicle_velocity_.y +=
            speed_fraction * speed_fraction * profile.lift_strength * dt;
        predicted_vehicle_velocity_.y -=
            VehicleGravityBlocksPerSecondSquared * profile.gravity_scale * dt;
        if (forward_speed < profile.stall_speed) {
          predicted_vehicle_velocity_.y -=
              VehicleGravityBlocksPerSecondSquared *
              profile.stall_gravity_scale *
              dt;
        }
        if (near_takeoff_surface && command.jump_held) {
          predicted_vehicle_velocity_.y = std::max(
              predicted_vehicle_velocity_.y,
              profile.max_climb_rate * 0.28F);
        }
        if (throttle > 0.01F) {
          const f32 post_gravity_forward_speed =
              dot(predicted_vehicle_velocity_, forward);
          if (post_gravity_forward_speed < forward_speed) {
            predicted_vehicle_velocity_ =
                predicted_vehicle_velocity_ +
                forward * (forward_speed - post_gravity_forward_speed);
          }
        }
      } else {
        predicted_vehicle_velocity_.y = std::max(predicted_vehicle_velocity_.y, 0.0F);
      }
      predicted_vehicle_velocity_.y = std::clamp(
          predicted_vehicle_velocity_.y,
          -profile.max_descent_rate,
          profile.max_climb_rate);
      attempted_delta = predicted_vehicle_velocity_ * dt;
    } else if (submersible) {
      water_occupancy = vehicle_water_occupancy(
          collision_query,
          predicted_vehicle_position_,
          profile.collision_size,
          profile.preferred_water_occupancy);
      submersible_in_water = water_occupancy >= profile.min_water_occupancy;
      const f32 vertical_input =
          (command.jump_held ? 1.0F : 0.0F) -
          (command.sneak_held ? 1.0F : 0.0F);
      if (submersible_in_water) {
        const f32 pitch_input =
            -normalized_look_axis(command.look_delta_y, 160.0F);
        const f32 pitch_delta = pitch_input * profile.pitch_rate * dt;
        const f32 yaw_delta = steer * profile.turn_rate * dt;
        predicted_vehicle_rotation_ =
            apply_local_vehicle_rotation(
                predicted_vehicle_rotation_,
                pitch_delta,
                yaw_delta,
                0.0F);
        predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(predicted_vehicle_rotation_);
        reported_angular_velocity = {pitch_delta / dt, yaw_delta / dt, 0.0F};

        Vec3f forward = normalize_or_zero(model_visual_forward(predicted_vehicle_rotation_));
        if (length(forward) <= 0.0F) {
          forward = horizontal_forward(predicted_vehicle_yaw_);
        }
        const f32 max_speed = throttle < 0.0F
                                  ? profile.max_reverse_speed
                                  : profile.max_forward_speed;
        const f32 target_speed = throttle * max_speed;
        const f32 current_speed = dot(predicted_vehicle_velocity_, forward);
        f32 next_speed = current_speed;
        if (command.secondary_held) {
          next_speed = approach(current_speed, 0.0F, profile.brake_force * dt);
        } else if (std::abs(throttle) > 0.01F) {
          const f32 accel =
              (current_speed * target_speed < 0.0F) ? profile.brake_force : profile.acceleration;
          next_speed = approach(current_speed, target_speed, accel * dt);
        } else {
          next_speed = current_speed *
                       std::pow(profile.water_drag, std::max(dt, 0.0F) * 60.0F);
          if (std::abs(next_speed) < 0.02F) {
            next_speed = 0.0F;
          }
        }
        predicted_vehicle_velocity_ = forward * next_speed;
        const f32 vertical_rate =
            vertical_input >= 0.0F ? profile.ascend_rate : profile.descend_rate;
        predicted_vehicle_velocity_.y += vertical_input * vertical_rate;
        predicted_vehicle_velocity_ =
            predicted_vehicle_velocity_ *
            std::pow(profile.water_drag, std::max(dt, 0.0F) * 60.0F);
        if (water_occupancy < profile.preferred_water_occupancy &&
            predicted_vehicle_velocity_.y > 0.0F) {
          const f32 surface_damping = std::clamp(
              water_occupancy / std::max(profile.preferred_water_occupancy, 0.01F),
              0.25F,
              1.0F);
          predicted_vehicle_velocity_.y *= surface_damping;
        }
      } else {
        const f32 air_drag =
            std::pow(profile.air_drag, std::max(dt, 0.0F) * 60.0F);
        predicted_vehicle_velocity_.x *= air_drag;
        predicted_vehicle_velocity_.z *= air_drag;
        predicted_vehicle_velocity_.y = std::max(
            predicted_vehicle_velocity_.y -
                VehicleGravityBlocksPerSecondSquared *
                    profile.gravity_scale_out_of_water *
                    dt,
            -VehicleTerminalVelocityBlocksPerSecond);
        if (command.secondary_held) {
          predicted_vehicle_velocity_.x =
              approach(predicted_vehicle_velocity_.x, 0.0F, profile.brake_force * dt);
          predicted_vehicle_velocity_.z =
              approach(predicted_vehicle_velocity_.z, 0.0F, profile.brake_force * dt);
        }
      }
      attempted_delta = predicted_vehicle_velocity_ * dt;
    } else {
      const f32 max_speed = throttle < 0.0F
                                ? profile.max_reverse_speed
                                : profile.max_forward_speed;
      const f32 target_speed = throttle * max_speed;
      if (ground_vehicle) {
        const f32 speed_fraction =
            profile.max_forward_speed <= 0.0F
                ? 0.0F
                : std::clamp(
                      horizontal_length(predicted_vehicle_velocity_) /
                          profile.max_forward_speed,
                      0.0F,
                      1.0F);
        const f32 traction = vehicle_grounded ? 1.0F : 0.1F;
        const f32 steering_response =
            std::abs(throttle) > 0.01F || speed_fraction > 0.05F
                ? std::max(speed_fraction, 0.25F) * traction
                : 0.0F;
        predicted_vehicle_yaw_ = wrap_angle_radians(
            predicted_vehicle_yaw_ + steer * profile.turn_rate * steering_response * dt);
      } else {
        const f32 desired_yaw =
            std::abs(steer) > 0.01F
                ? wrap_angle_radians(predicted_vehicle_yaw_ + steer * profile.turn_rate)
                : player_controller.camera().yaw_radians();
        predicted_vehicle_yaw_ = approach_angle(
            predicted_vehicle_yaw_,
            desired_yaw,
            profile.turn_rate * dt);
      }
      const Vec3f forward = horizontal_forward(predicted_vehicle_yaw_);
      const f32 current_speed = dot(predicted_vehicle_velocity_, forward);
      f32 next_speed = current_speed;
      const bool has_ground_traction =
          !(living_mount || ground_vehicle) || vehicle_grounded;
      if (command.secondary_held && has_ground_traction) {
        next_speed = approach(current_speed, 0.0F, profile.brake_force * dt);
      } else if (std::abs(throttle) > 0.01F && has_ground_traction) {
        const f32 accel =
            (current_speed * target_speed < 0.0F) ? profile.brake_force : profile.acceleration;
        next_speed = approach(current_speed, target_speed, accel * dt);
      } else {
        const f32 drag_factor =
            std::pow(profile.drag, std::max(dt, 0.0F) * 60.0F);
        next_speed = current_speed * drag_factor;
        if (std::abs(next_speed) < 0.02F) {
          next_speed = 0.0F;
        }
      }
      f32 next_vertical_speed = predicted_vehicle_velocity_.y;
      if (living_mount || ground_vehicle) {
        if (vehicle_grounded) {
          next_vertical_speed = std::max(next_vertical_speed, 0.0F);
        } else {
          next_vertical_speed = std::max(
              next_vertical_speed -
                  VehicleGravityBlocksPerSecondSquared * profile.gravity_scale * dt,
              -VehicleTerminalVelocityBlocksPerSecond);
        }
      }
      Vec3f next_horizontal_velocity = forward * next_speed;
      if (ground_vehicle && predicted_vehicle_impact_ticks_ > 0U) {
        next_horizontal_velocity = project_velocity_out_of_impact(
            next_horizontal_velocity,
            predicted_vehicle_impact_normal_);
      }
      predicted_vehicle_velocity_ = next_horizontal_velocity;
      predicted_vehicle_velocity_.y = next_vertical_speed;
      predicted_vehicle_rotation_ =
          vehicle_render_rotation_from_gameplay_yaw(predicted_vehicle_yaw_);
      const f32 yaw_delta = wrap_angle_radians(predicted_vehicle_yaw_ - previous_yaw);
      reported_angular_velocity = {0.0F, yaw_delta / dt, 0.0F};
      attempted_delta = predicted_vehicle_velocity_ * dt;
    }

    bool hit_collision_this_tick = false;
    Vec3f collision_normal{};
    f32 collision_normal_impact_speed = 0.0F;
    const Vec3f pre_collision_velocity = predicted_vehicle_velocity_;
    if (collision_query != nullptr) {
      const Aabb3f bounds =
          vehicle_bounds(predicted_vehicle_position_, profile.collision_size);
      physics::MovementResolution resolved =
          physics::resolve_movement(*collision_query, bounds, attempted_delta);
      if (living_mount && vehicle_grounded) {
        if (std::optional<physics::MovementResolution> stepped =
                try_vehicle_step_up(
                    *collision_query,
                    bounds,
                    attempted_delta,
                    resolved,
                    profile.step_height);
            stepped.has_value()) {
          resolved = stepped.value();
        }
      }
      hit_collision_this_tick = resolved.hit_x || resolved.hit_y || resolved.hit_z;
      if (hit_collision_this_tick) {
        if (const std::optional<Vec3f> normal =
                vehicle_impact_normal(resolved, pre_collision_velocity);
            normal.has_value()) {
          collision_normal = normal.value();
          collision_normal_impact_speed =
              std::max(0.0F, dot(pre_collision_velocity * -1.0F, collision_normal));
        }
      }
      predicted_vehicle_position_ =
          predicted_vehicle_position_ + resolved.applied_delta;
      if (resolved.hit_x) {
        predicted_vehicle_velocity_.x = 0.0F;
      }
      if (resolved.hit_y) {
        predicted_vehicle_velocity_.y = 0.0F;
      }
      if (resolved.hit_z) {
        predicted_vehicle_velocity_.z = 0.0F;
      }
      if (living_mount || ground_vehicle || aircraft || submersible) {
        vehicle_grounded = resolved.grounded;
      }
      if (ground_vehicle) {
        const Vec3f impact_normal =
            vehicle_horizontal_impact_normal(resolved, attempted_delta);
        if (has_horizontal_vector(impact_normal)) {
          remember_vehicle_impact(impact_normal);
        } else if (predicted_vehicle_impact_ticks_ > 0U) {
          --predicted_vehicle_impact_ticks_;
          if (predicted_vehicle_impact_ticks_ == 0U) {
            predicted_vehicle_impact_normal_ = {};
          }
        }
      }
    } else {
      predicted_vehicle_position_ =
          predicted_vehicle_position_ + attempted_delta;
      if (predicted_vehicle_impact_ticks_ > 0U) {
        --predicted_vehicle_impact_ticks_;
        if (predicted_vehicle_impact_ticks_ == 0U) {
          predicted_vehicle_impact_normal_ = {};
        }
      }
    }
    if (aircraft || submersible) {
      const f32 rotation_delta = std::max(dt, 1.0F / 240.0F);
      if (length(reported_angular_velocity) <= 0.0F) {
        const f32 yaw_delta = wrap_angle_radians(predicted_vehicle_yaw_ - previous_yaw);
        reported_angular_velocity = {0.0F, yaw_delta / rotation_delta, 0.0F};
      }
    }
    if (aircraft && vehicle_grounded) {
      predicted_flags |= protocol::VehicleMoveFlags::OnGround;
    } else if (submersible) {
      if (submersible_in_water) {
        predicted_flags |= protocol::VehicleMoveFlags::InWater;
      } else if (vehicle_grounded) {
        predicted_flags |= protocol::VehicleMoveFlags::OnGround;
      }
    } else if (profile.physics == entity::VehiclePhysicsKind::SurfaceWaterVehicle) {
      predicted_flags |= protocol::VehicleMoveFlags::InWater;
    } else if (vehicle_grounded) {
      predicted_flags |= protocol::VehicleMoveFlags::OnGround;
    }

    const std::string_view animation_name =
        vehicle_motion_animation_name(
            profile,
            predicted_vehicle_rotation_,
            predicted_vehicle_velocity_);
    if (animation_name.empty()) {
      predicted_vehicle_animation_name_.clear();
      predicted_vehicle_animation_time_ = 0.0;
    } else if (predicted_vehicle_animation_name_ != animation_name) {
      predicted_vehicle_animation_name_ = std::string{animation_name};
      predicted_vehicle_animation_time_ = 0.0;
    } else {
      predicted_vehicle_animation_time_ += dt;
    }
    std::array<protocol::EntityAnimationPlayback, 1> active_animations{};
    std::span<const protocol::EntityAnimationPlayback> active_animation_span{};
    if (!predicted_vehicle_animation_name_.empty()) {
      active_animations[0] = {
          predicted_vehicle_animation_name_,
          predicted_vehicle_animation_time_};
      active_animation_span = std::span<const protocol::EntityAnimationPlayback>{
          active_animations.data(),
          1U};
    }

    player_controller.apply_mounted_vehicle_transform(
        vehicle_id,
        predicted_vehicle_position_,
        predicted_vehicle_rotation_,
        seat == nullptr ? Vec3f{} : seat->local_position,
        seat == nullptr ? Quatf{} : seat->local_rotation,
        predicted_vehicle_velocity_);
    entities.apply_local_vehicle_prediction(
        vehicle_id,
        predicted_vehicle_position_,
        predicted_vehicle_velocity_,
        predicted_vehicle_rotation_,
        active_animation_span);

    protocol::VehicleMoveFlags reported_flags = predicted_flags;
    if (command.jump_pressed) {
      reported_flags |= protocol::VehicleMoveFlags::Jump;
    }
    if (command.secondary_held) {
      reported_flags |= protocol::VehicleMoveFlags::Brake;
    }
    if (command.sprint_held) {
      reported_flags |= protocol::VehicleMoveFlags::Sprint;
    }

    const f32 acceleration_dt = std::max(dt, 1.0F / 240.0F);
    VehicleDebugTelemetry telemetry;
    telemetry.vehicle_id = vehicle_id;
    telemetry.definition = definition == nullptr ? mirror->entity_type_name : definition->name;
    telemetry.physics = profile.physics;
    telemetry.position = predicted_vehicle_position_;
    telemetry.velocity = predicted_vehicle_velocity_;
    telemetry.previous_velocity = debug_previous_velocity;
    telemetry.acceleration =
        (predicted_vehicle_velocity_ - debug_previous_velocity) *
        (1.0F / acceleration_dt);
    telemetry.angular_velocity = reported_angular_velocity;
    telemetry.rotation = predicted_vehicle_rotation_;
    telemetry.speed = length(predicted_vehicle_velocity_);
    telemetry.horizontal_speed = horizontal_length(predicted_vehicle_velocity_);
    telemetry.throttle = throttle;
    telemetry.steer = steer;
    telemetry.dt_seconds = dt;
    telemetry.client_tick = tick_index;
    telemetry.last_sent_sequence =
        send_this_tick
            ? next_vehicle_movement_sequence_
            : (next_vehicle_movement_sequence_ > 1U ? next_vehicle_movement_sequence_ - 1U : 0U);
    telemetry.flags = reported_flags;
    telemetry.sent_this_tick = send_this_tick;
    telemetry.hit_collision_this_tick = hit_collision_this_tick;
    telemetry.collision_normal = collision_normal;
    telemetry.collision_normal_impact_speed = collision_normal_impact_speed;
    telemetry.impact_memory_ticks = predicted_vehicle_impact_ticks_;
    telemetry.has_crash_tuning = definition != nullptr && definition->crash.enabled;
    if (definition != nullptr) {
      telemetry.crash = definition->crash;
    }
    vehicle_debug_ = std::move(telemetry);

    if (!send_this_tick) {
      return;
    }
    movement_send_accumulator_seconds_ = 0.0;

    protocol::ServerboundVehicleMove move;
    move.vehicle_id = vehicle_id;
    move.sequence = next_vehicle_movement_sequence_++;
    move.client_tick = tick_index;
    move.dt_seconds = dt;
    move.position = predicted_vehicle_position_;
    move.velocity = predicted_vehicle_velocity_;
    move.rotation = predicted_vehicle_rotation_;
    move.angular_velocity = reported_angular_velocity;
    move.throttle = throttle;
    move.steer = steer;
    move.flags = reported_flags;
    (void)sender.send_vehicle_move(move);
    return;
  }
  predicted_vehicle_id_.reset();
  clear_vehicle_impact_memory();
  clear_vehicle_debug_telemetry();
  dismount_request_sent_ = false;
  player_controller.update_local_movement(command, collision_query, tick_seconds);

  movement_send_accumulator_seconds_ += tick_seconds;
  const Seconds send_interval =
      1.0 / static_cast<Seconds>(std::max(movement_send_rate_hz_, 1.0F));
  const protocol::PlayerActionFlags action_flags =
      action_flags_from_player_command(command);
  const bool action_flags_changed =
      !sent_movement_packet_ || action_flags != last_sent_action_flags_;
  const bool hotbar_changed =
      !sent_movement_packet_ || command.selected_hotbar_slot != last_sent_hotbar_slot_;
  if (movement_send_accumulator_seconds_ < send_interval && !has_action(action_flags) &&
      !action_flags_changed && !hotbar_changed) {
    return;
  }
  movement_send_accumulator_seconds_ = 0.0;
  const protocol::ServerboundPlayerMove move =
      player_controller.build_movement_packet(
          next_movement_sequence_++,
          tick_index,
          action_flags);
  (void)sender.send_player_move(move);
  last_sent_action_flags_ = action_flags;
  last_sent_hotbar_slot_ = command.selected_hotbar_slot;
  sent_movement_packet_ = true;
}

void ClientInputRuntime::set_movement_send_rate(f32 send_rate_hz) noexcept {
  movement_send_rate_hz_ = std::max(send_rate_hz, 1.0F);
}

void ClientInputRuntime::remember_vehicle_impact(Vec3f impact_normal) noexcept {
  if (!has_horizontal_vector(impact_normal)) {
    return;
  }
  if (predicted_vehicle_impact_ticks_ == 0U ||
      !has_horizontal_vector(predicted_vehicle_impact_normal_)) {
    predicted_vehicle_impact_normal_ = {};
  }
  if (impact_normal.x != 0.0F) {
    predicted_vehicle_impact_normal_.x = impact_normal.x;
  }
  if (impact_normal.z != 0.0F) {
    predicted_vehicle_impact_normal_.z = impact_normal.z;
  }
  predicted_vehicle_impact_ticks_ = VehicleImpactMemoryTicks;
}

void ClientInputRuntime::clear_vehicle_impact_memory() noexcept {
  predicted_vehicle_impact_normal_ = {};
  predicted_vehicle_impact_ticks_ = 0U;
}

void ClientInputRuntime::clear_vehicle_debug_telemetry() noexcept {
  vehicle_debug_.reset();
}

void ClientInputRuntime::apply_vehicle_correction(
    const protocol::ClientboundVehicleCorrection& correction) noexcept {
  const Vec3f correction_delta = predicted_vehicle_position_ - correction.position;
  predicted_vehicle_id_ = correction.vehicle_id;
  predicted_vehicle_position_ = correction.position;
  predicted_vehicle_velocity_ = correction.velocity;
  predicted_vehicle_rotation_ = normalize_quat(correction.rotation);
  predicted_vehicle_yaw_ = vehicle_gameplay_yaw_from_rotation(correction.rotation);
  predicted_vehicle_animation_name_.clear();
  predicted_vehicle_animation_time_ = 0.0;
  clear_vehicle_impact_memory();
  if (correction.reason == protocol::VehicleCorrectionReason::Collision) {
    remember_vehicle_impact(vehicle_horizontal_impact_normal_from_delta(correction_delta));
  }
  movement_send_accumulator_seconds_ = 0.0;
}

void ClientInputRuntime::clear_vehicle_prediction() noexcept {
  predicted_vehicle_id_.reset();
  predicted_vehicle_position_ = {};
  predicted_vehicle_velocity_ = {};
  predicted_vehicle_rotation_ = {};
  predicted_vehicle_yaw_ = 0.0F;
  clear_vehicle_impact_memory();
  clear_vehicle_debug_telemetry();
  predicted_vehicle_animation_name_.clear();
  predicted_vehicle_animation_time_ = 0.0;
}

} // namespace arv::client::detail
