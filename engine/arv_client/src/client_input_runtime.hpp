#pragma once

#include "arv/client/local_player_controller.hpp"
#include "arv/client/vehicle_debug.hpp"
#include "arv/core/time.hpp"
#include "arv/net/connection_state.hpp"

#include <string>

namespace arv::entity {
class VehicleDefinitionRegistry;
} // namespace arv::entity

namespace arv::client {

class ClientUiState;
class ClientEntityMirrorStore;

namespace detail {

class ClientProtocolSender;
} // namespace detail

class ClientInventoryRuntime;

namespace detail {

class ClientInputRuntime {
public:
  void submit_input(
      InputFrame input,
      ClientUiState& ui,
      LocalPlayerController& player_controller,
      ClientProtocolSender& sender,
      net::ConnectionState connection_state);

  void send_fixed_tick_input(
      TickIndex tick_index,
      LocalPlayerController& player_controller,
      ClientInventoryRuntime& inventory,
      ClientProtocolSender& sender,
      ClientEntityMirrorStore& entities,
      const arv::entity::VehicleDefinitionRegistry* vehicle_registry,
      bool void_space_world,
      f32 environment_gravity_scale,
      const physics::ICollisionQuery* collision_query,
      Seconds tick_seconds);

  void set_movement_send_rate(f32 send_rate_hz) noexcept;
  void apply_vehicle_correction(
      const protocol::ClientboundVehicleCorrection& correction) noexcept;
  void clear_vehicle_prediction() noexcept;
  [[nodiscard]] const std::optional<VehicleDebugTelemetry>& vehicle_debug_telemetry()
      const noexcept {
    return vehicle_debug_;
  }

private:
  void remember_vehicle_impact(Vec3f impact_normal) noexcept;
  void clear_vehicle_impact_memory() noexcept;
  void clear_vehicle_debug_telemetry() noexcept;

  f32 movement_send_rate_hz_{protocol::DefaultClientMovementSendRateHz};
  Seconds movement_send_accumulator_seconds_{};
  u32 next_movement_sequence_{1};
  u32 next_vehicle_movement_sequence_{1};
  protocol::PlayerActionFlags last_sent_action_flags_{protocol::PlayerActionFlags::None};
  u8 last_sent_hotbar_slot_{};
  bool sent_movement_packet_{};
  bool dismount_request_sent_{};
  bool drop_item_pressed_{};
  bool drop_item_held_{};
  Seconds drop_item_held_seconds_{};
  Seconds drop_item_repeat_accumulator_seconds_{};
  std::optional<EntityNetID> predicted_vehicle_id_{};
  Vec3f predicted_vehicle_position_{};
  Vec3f predicted_vehicle_velocity_{};
  Quatf predicted_vehicle_rotation_{};
  f32 predicted_vehicle_yaw_{};
  Vec3f predicted_vehicle_impact_normal_{};
  u8 predicted_vehicle_impact_ticks_{};
  std::string predicted_vehicle_animation_name_{};
  Seconds predicted_vehicle_animation_time_{};
  std::optional<VehicleDebugTelemetry> vehicle_debug_{};
};

} // namespace detail
} // namespace arv::client
