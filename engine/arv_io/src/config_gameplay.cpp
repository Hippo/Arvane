#include "config_internal.hpp"

namespace arv::io::detail {

void read_physics_config(const DataValue& physics, PhysicsConfig& config) {
  read_scalar(physics, "standing_eye_height", config.standing_eye_height);
  read_scalar(physics, "sneaking_eye_height", config.sneaking_eye_height);
  read_scalar(physics, "swimming_eye_height", config.swimming_eye_height);
  read_scalar(physics, "eye_height_lerp_per_second", config.eye_height_lerp_per_second);
  read_scalar(physics, "reach_distance", config.reach_distance);
  read_scalar(physics, "mouse_sensitivity_radians", config.mouse_sensitivity_radians);
  read_scalar(physics, "walk_speed", config.walk_speed);
  read_scalar(physics, "sprint_speed", config.sprint_speed);
  read_scalar(physics, "sneak_speed", config.sneak_speed);
  read_scalar(physics, "air_speed", config.air_speed);
  read_scalar(
      physics,
      "air_control_acceleration_per_second",
      config.air_control_acceleration_per_second);
  read_scalar(physics, "default_surface_slipperiness", config.default_surface_slipperiness);
  read_scalar(physics, "ground_acceleration_per_second", config.ground_acceleration_per_second);
  read_scalar(physics, "ground_deceleration_per_second", config.ground_deceleration_per_second);
  read_scalar(physics, "min_surface_control", config.min_surface_control);
  read_scalar(physics, "max_surface_control", config.max_surface_control);
  read_scalar(physics, "fly_speed", config.fly_speed);
  read_scalar(physics, "fly_vertical_speed", config.fly_vertical_speed);
  read_scalar(physics, "jump_velocity", config.jump_velocity);
  read_scalar(physics, "sprint_jump_horizontal_boost", config.sprint_jump_horizontal_boost);
  read_scalar(physics, "gravity_per_second", config.gravity_per_second);
  read_scalar(physics, "terminal_velocity", config.terminal_velocity);
  read_scalar(physics, "body_width", config.body_width);
  read_scalar(physics, "body_height", config.body_height);
  read_scalar(physics, "sneaking_body_height", config.sneaking_body_height);
  read_scalar(physics, "swimming_body_width", config.swimming_body_width);
  read_scalar(physics, "swimming_body_height", config.swimming_body_height);
  read_scalar(physics, "step_height", config.step_height);
  read_scalar(physics, "sneak_support_probe_depth", config.sneak_support_probe_depth);
  read_scalar(physics, "collision_epsilon", config.collision_epsilon);
  read_scalar(physics, "surface_swim_speed", config.surface_swim_speed);
  read_scalar(physics, "underwater_swim_speed", config.underwater_swim_speed);
  read_scalar(physics, "fast_surface_swim_speed", config.fast_surface_swim_speed);
  read_scalar(physics, "fast_underwater_swim_speed", config.fast_underwater_swim_speed);
  read_scalar(physics, "upward_swim_acceleration", config.upward_swim_acceleration);
  read_scalar(physics, "downward_swim_acceleration", config.downward_swim_acceleration);
  read_scalar(physics, "liquid_drag", config.liquid_drag);
  read_scalar(physics, "surface_float_depth", config.surface_float_depth);
  read_scalar(physics, "surface_buoyancy", config.surface_buoyancy);
  read_scalar(physics, "underwater_buoyancy", config.underwater_buoyancy);
  read_scalar(physics, "breath_seconds", config.breath_seconds);
  read_scalar(physics, "max_head_body_yaw_radians", config.max_head_body_yaw_radians);
}

void read_gameplay_config(const DataValue& gameplay, GameplayConfig& config) {
  const DataValue* block_breaking = gameplay.get("block_breaking");
  if (block_breaking != nullptr) {
    read_scalar(
        *block_breaking,
        "base_seconds_per_hardness",
        config.block_breaking.base_seconds_per_hardness);
    read_scalar(
        *block_breaking,
        "creative_inventory_multiplier",
        config.block_breaking.creative_inventory_multiplier);
    read_scalar(
        *block_breaking,
        "hand_speed_multiplier",
        config.block_breaking.hand_speed_multiplier);
    read_scalar(
        *block_breaking,
        "wrong_tool_speed_multiplier",
        config.block_breaking.wrong_tool_speed_multiplier);
  }

  const DataValue* dropped_items = gameplay.get("dropped_items");
  if (dropped_items != nullptr) {
    read_scalar(*dropped_items, "pickup_radius", config.dropped_items.pickup_radius);
    read_scalar(
        *dropped_items,
        "pickup_delay_seconds",
        config.dropped_items.pickup_delay_seconds);
  }

  const DataValue* combat = gameplay.get("combat");
  if (combat != nullptr) {
    read_scalar(*combat, "default_attack_damage", config.combat.default_attack_damage);
    read_scalar(*combat, "default_attack_range", config.combat.default_attack_range);
    read_scalar(
        *combat,
        "default_attack_cooldown_seconds",
        config.combat.default_attack_cooldown_seconds);
    read_scalar(
        *combat,
        "attack_validation_grace_blocks",
        config.combat.attack_validation_grace_blocks);
    read_scalar(
        *combat,
        "max_attack_validation_range",
        config.combat.max_attack_validation_range);
    read_scalar(
        *combat,
        "existing_horizontal_retention",
        config.combat.existing_horizontal_retention);
    read_scalar(
        *combat,
        "base_horizontal_knockback",
        config.combat.base_horizontal_knockback);
    read_scalar(
        *combat,
        "sprint_horizontal_bonus",
        config.combat.sprint_horizontal_bonus);
    read_scalar(
        *combat,
        "base_vertical_knockback",
        config.combat.base_vertical_knockback);
    read_scalar(
        *combat,
        "airborne_vertical_knockback",
        config.combat.airborne_vertical_knockback);
    read_scalar(
        *combat,
        "vertical_knockback_cap",
        config.combat.vertical_knockback_cap);
    read_scalar(
        *combat,
        "knockback_control_suppression_ticks",
        config.combat.knockback_control_suppression_ticks);
    read_scalar(
        *combat,
        "use_current_block_obstruction_check",
        config.combat.use_current_block_obstruction_check);
  }
}

void put_physics_config(DataValue& root, const PhysicsConfig& physics) {
  put(root, "physics.standing_eye_height", physics.standing_eye_height);
  put(root, "physics.sneaking_eye_height", physics.sneaking_eye_height);
  put(root, "physics.swimming_eye_height", physics.swimming_eye_height);
  put(root, "physics.eye_height_lerp_per_second", physics.eye_height_lerp_per_second);
  put(root, "physics.reach_distance", physics.reach_distance);
  put(root, "physics.mouse_sensitivity_radians", physics.mouse_sensitivity_radians);
  put(root, "physics.walk_speed", physics.walk_speed);
  put(root, "physics.sprint_speed", physics.sprint_speed);
  put(root, "physics.sneak_speed", physics.sneak_speed);
  put(root, "physics.air_speed", physics.air_speed);
  put(root, "physics.air_control_acceleration_per_second", physics.air_control_acceleration_per_second);
  put(root, "physics.default_surface_slipperiness", physics.default_surface_slipperiness);
  put(root, "physics.ground_acceleration_per_second", physics.ground_acceleration_per_second);
  put(root, "physics.ground_deceleration_per_second", physics.ground_deceleration_per_second);
  put(root, "physics.min_surface_control", physics.min_surface_control);
  put(root, "physics.max_surface_control", physics.max_surface_control);
  put(root, "physics.fly_speed", physics.fly_speed);
  put(root, "physics.fly_vertical_speed", physics.fly_vertical_speed);
  put(root, "physics.jump_velocity", physics.jump_velocity);
  put(root, "physics.sprint_jump_horizontal_boost", physics.sprint_jump_horizontal_boost);
  put(root, "physics.gravity_per_second", physics.gravity_per_second);
  put(root, "physics.terminal_velocity", physics.terminal_velocity);
  put(root, "physics.body_width", physics.body_width);
  put(root, "physics.body_height", physics.body_height);
  put(root, "physics.sneaking_body_height", physics.sneaking_body_height);
  put(root, "physics.swimming_body_width", physics.swimming_body_width);
  put(root, "physics.swimming_body_height", physics.swimming_body_height);
  put(root, "physics.step_height", physics.step_height);
  put(root, "physics.sneak_support_probe_depth", physics.sneak_support_probe_depth);
  put(root, "physics.collision_epsilon", physics.collision_epsilon);
  put(root, "physics.surface_swim_speed", physics.surface_swim_speed);
  put(root, "physics.underwater_swim_speed", physics.underwater_swim_speed);
  put(root, "physics.fast_surface_swim_speed", physics.fast_surface_swim_speed);
  put(root, "physics.fast_underwater_swim_speed", physics.fast_underwater_swim_speed);
  put(root, "physics.upward_swim_acceleration", physics.upward_swim_acceleration);
  put(root, "physics.downward_swim_acceleration", physics.downward_swim_acceleration);
  put(root, "physics.liquid_drag", physics.liquid_drag);
  put(root, "physics.surface_float_depth", physics.surface_float_depth);
  put(root, "physics.surface_buoyancy", physics.surface_buoyancy);
  put(root, "physics.underwater_buoyancy", physics.underwater_buoyancy);
  put(root, "physics.breath_seconds", physics.breath_seconds);
  put(root, "physics.max_head_body_yaw_radians", physics.max_head_body_yaw_radians);
}

void put_gameplay_config(DataValue& root, const GameplayConfig& gameplay) {
  put(
      root,
      "gameplay.block_breaking.base_seconds_per_hardness",
      gameplay.block_breaking.base_seconds_per_hardness);
  put(
      root,
      "gameplay.block_breaking.creative_inventory_multiplier",
      gameplay.block_breaking.creative_inventory_multiplier);
  put(
      root,
      "gameplay.block_breaking.hand_speed_multiplier",
      gameplay.block_breaking.hand_speed_multiplier);
  put(
      root,
      "gameplay.block_breaking.wrong_tool_speed_multiplier",
      gameplay.block_breaking.wrong_tool_speed_multiplier);
  put(root, "gameplay.dropped_items.pickup_radius", gameplay.dropped_items.pickup_radius);
  put(root, "gameplay.dropped_items.pickup_delay_seconds", gameplay.dropped_items.pickup_delay_seconds);
  put(root, "gameplay.combat.default_attack_damage", gameplay.combat.default_attack_damage);
  put(root, "gameplay.combat.default_attack_range", gameplay.combat.default_attack_range);
  put(root, "gameplay.combat.default_attack_cooldown_seconds", gameplay.combat.default_attack_cooldown_seconds);
  put(root, "gameplay.combat.attack_validation_grace_blocks", gameplay.combat.attack_validation_grace_blocks);
  put(root, "gameplay.combat.max_attack_validation_range", gameplay.combat.max_attack_validation_range);
  put(root, "gameplay.combat.existing_horizontal_retention", gameplay.combat.existing_horizontal_retention);
  put(root, "gameplay.combat.base_horizontal_knockback", gameplay.combat.base_horizontal_knockback);
  put(root, "gameplay.combat.sprint_horizontal_bonus", gameplay.combat.sprint_horizontal_bonus);
  put(root, "gameplay.combat.base_vertical_knockback", gameplay.combat.base_vertical_knockback);
  put(root, "gameplay.combat.airborne_vertical_knockback", gameplay.combat.airborne_vertical_knockback);
  put(root, "gameplay.combat.vertical_knockback_cap", gameplay.combat.vertical_knockback_cap);
  put(root, "gameplay.combat.knockback_control_suppression_ticks", gameplay.combat.knockback_control_suppression_ticks);
  put(root, "gameplay.combat.use_current_block_obstruction_check", gameplay.combat.use_current_block_obstruction_check);
}

void read_gameplay_sections(const DataValue& root, EngineConfig& config) {
  const DataValue* physics = root.get("physics");
  if (physics != nullptr) {
    read_physics_config(*physics, config.physics);
  }

  const DataValue* gameplay = root.get("gameplay");
  if (gameplay != nullptr) {
    read_gameplay_config(*gameplay, config.gameplay);
  }
}

void put_gameplay_full(DataValue& root, const EngineConfig& config) {
  put_physics_config(root, config.physics);
  put_gameplay_config(root, config.gameplay);
}

void put_gameplay_shared(DataValue& root, const EngineConfig& config) {
  put_physics_config(root, config.physics);
  put_gameplay_config(root, config.gameplay);
}

void put_gameplay_client(DataValue& root, const EngineConfig& config) {
  put(root, "physics.mouse_sensitivity_radians", config.physics.mouse_sensitivity_radians);
}

} // namespace arv::io::detail
