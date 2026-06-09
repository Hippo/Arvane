#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/entity/components.hpp"
#include "arv/protocol/vehicles.hpp"

#include <string>

namespace arv::client {

struct VehicleDebugTelemetry {
  EntityNetID vehicle_id{};
  std::string definition{};
  entity::VehiclePhysicsKind physics{entity::VehiclePhysicsKind::LivingMount};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f previous_velocity{};
  Vec3f acceleration{};
  Vec3f angular_velocity{};
  Quatf rotation{};
  f32 speed{};
  f32 horizontal_speed{};
  f32 throttle{};
  f32 steer{};
  Seconds dt_seconds{};
  TickIndex client_tick{};
  u32 last_sent_sequence{};
  protocol::VehicleMoveFlags flags{protocol::VehicleMoveFlags::None};
  bool sent_this_tick{};
  bool hit_collision_this_tick{};
  Vec3f collision_normal{};
  f32 collision_normal_impact_speed{};
  u8 impact_memory_ticks{};
  bool has_crash_tuning{};
  entity::VehicleCrashDefinition crash{};
};

} // namespace arv::client
