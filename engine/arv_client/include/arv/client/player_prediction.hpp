#pragma once

#include "arv/core/time.hpp"
#include "arv/movement/player_movement.hpp"
#include "arv/protocol/player_input.hpp"

#include <span>
#include <vector>

namespace arv::client {

struct PredictedPlayerState {
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  bool sneaking{};
  bool swimming{};
  f32 breath_seconds{player::DefaultBreathSeconds};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  TickIndex last_predicted_tick{};
  TickIndex last_confirmed_server_tick{};
};

struct PendingInputSample {
  TickIndex input_tick{};
  protocol::InputSample input{};
  PredictedPlayerState predicted_state{};
};

struct PlayerPredictionSettings {
  u32 history_seconds{5};
  f32 position_error_ignore_threshold{0.005F};
  f32 position_error_smooth_threshold{0.5F};
  f32 position_error_snap_threshold{1.0F};
  u32 smoothing_ticks{6};
};

enum class PlayerCorrectionVisualAction : u8 {
  None,
  Ignore,
  Smooth,
  Snap,
};

struct PlayerReconciliationResult {
  bool stale{};
  bool missing_history{};
  PlayerCorrectionVisualAction visual_action{PlayerCorrectionVisualAction::None};
  f32 position_error{};
  usize acknowledged_input_count{};
  usize replayed_input_count{};
};

struct PlayerPredictionDebugStats {
  usize pending_input_count{};
  TickIndex last_predicted_tick{};
  TickIndex last_confirmed_server_tick{};
  u64 correction_count{};
  f32 average_correction_distance{};
  f32 max_correction_distance{};
  u64 hard_snap_count{};
  u64 smoothed_correction_count{};
  u64 ignored_correction_count{};
  u64 dropped_stale_correction_count{};
  u64 missing_history_fallback_count{};
  u64 dropped_old_input_count{};
};

class LocalPlayerPrediction {
public:
  void reset(
      movement::PlayerKinematicState state,
      TickIndex initial_server_tick,
      TickRate tick_rate,
      PlayerPredictionSettings settings = {});
  void clear() noexcept;

  [[nodiscard]] const PredictedPlayerState& state() const noexcept;
  [[nodiscard]] const PlayerPredictionSettings& settings() const noexcept;
  [[nodiscard]] movement::PlayerKinematicState kinematic_state() const noexcept;
  [[nodiscard]] std::span<const PendingInputSample> pending_inputs() const noexcept;
  [[nodiscard]] PlayerPredictionDebugStats debug_stats() const noexcept;

  void predict_local_input(
      TickIndex input_tick,
      protocol::InputSample input,
      const physics::ICollisionQuery* collision_query,
      const player::PlayerTuning& tuning,
      movement::PlayerMovementAuthority authority,
      Seconds tick_seconds);

  [[nodiscard]] PlayerReconciliationResult reconcile_authoritative_state(
      const protocol::PlayerCorrection& correction,
      const physics::ICollisionQuery* collision_query,
      const player::PlayerTuning& tuning,
      Seconds tick_seconds);

private:
  [[nodiscard]] usize max_history_ticks() const noexcept;
  void reserve_history();
  void prune_old_inputs(TickIndex newest_tick);
  void apply_step_state(
      movement::PlayerKinematicState state,
      TickIndex predicted_tick) noexcept;

  PredictedPlayerState state_{};
  std::vector<PendingInputSample> pending_inputs_{};
  PlayerPredictionSettings settings_{};
  TickRate tick_rate_{DefaultBaseTps};
  u64 correction_count_{};
  f64 correction_distance_sum_{};
  f32 max_correction_distance_{};
  u64 hard_snap_count_{};
  u64 smoothed_correction_count_{};
  u64 ignored_correction_count_{};
  u64 dropped_stale_correction_count_{};
  u64 missing_history_fallback_count_{};
  u64 dropped_old_input_count_{};
};

[[nodiscard]] PredictedPlayerState predicted_state_from_kinematic(
    const movement::PlayerKinematicState& state,
    TickIndex predicted_tick,
    TickIndex confirmed_server_tick) noexcept;

[[nodiscard]] movement::PlayerKinematicState kinematic_state_from_prediction(
    const PredictedPlayerState& state) noexcept;

[[nodiscard]] movement::PlayerKinematicState kinematic_state_from_correction(
    const protocol::PlayerCorrection& correction) noexcept;

} // namespace arv::client
