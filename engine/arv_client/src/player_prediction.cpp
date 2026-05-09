#include "arv/client/player_prediction.hpp"

#include <algorithm>
#include <cmath>

namespace arv::client {
namespace {

[[nodiscard]] f32 position_distance(Vec3f lhs, Vec3f rhs) noexcept {
  return length(lhs - rhs);
}

[[nodiscard]] PlayerCorrectionVisualAction classify_position_error(
    f32 error,
    const PlayerPredictionSettings& settings) noexcept {
  if (error <= settings.position_error_ignore_threshold) {
    return PlayerCorrectionVisualAction::Ignore;
  }
  const f32 snap_threshold = std::max(
      settings.position_error_snap_threshold,
      settings.position_error_smooth_threshold);
  if (error >= snap_threshold) {
    return PlayerCorrectionVisualAction::Snap;
  }
  return PlayerCorrectionVisualAction::Smooth;
}

[[nodiscard]] movement::PlayerMovementAuthority authority_from_correction(
    const protocol::PlayerCorrection& correction) noexcept {
  return {
      correction.resolved_core_permissions.can_fly,
      correction.resolved_core_permissions.has_collision};
}

} // namespace

PredictedPlayerState predicted_state_from_kinematic(
    const movement::PlayerKinematicState& state,
    TickIndex predicted_tick,
    TickIndex confirmed_server_tick) noexcept {
  return {
      state.position,
      state.velocity,
      state.yaw_radians,
      state.pitch_radians,
      state.grounded,
      state.sneaking,
      state.swimming,
      state.breath_seconds,
      state.movement_mode,
      predicted_tick,
      confirmed_server_tick};
}

movement::PlayerKinematicState kinematic_state_from_prediction(
    const PredictedPlayerState& state) noexcept {
  return {
      state.position,
      state.velocity,
      state.yaw_radians,
      state.pitch_radians,
      state.grounded,
      state.sneaking,
      state.swimming,
      state.breath_seconds,
      state.movement_mode};
}

movement::PlayerKinematicState kinematic_state_from_correction(
    const protocol::PlayerCorrection& correction) noexcept {
  return {
      correction.position,
      correction.velocity,
      correction.yaw_radians,
      correction.pitch_radians,
      correction.grounded,
      correction.sneaking,
      correction.swimming,
      correction.breath_seconds,
      correction.movement_mode};
}

void LocalPlayerPrediction::reset(
    movement::PlayerKinematicState state,
    TickIndex initial_server_tick,
    TickRate tick_rate,
    PlayerPredictionSettings settings) {
  settings_ = settings;
  tick_rate_ = tick_rate == 0U ? DefaultBaseTps : tick_rate;
  reserve_history();
  pending_inputs_.clear();
  state_ = predicted_state_from_kinematic(
      state,
      initial_server_tick,
      initial_server_tick);
  correction_count_ = 0;
  correction_distance_sum_ = 0.0;
  max_correction_distance_ = 0.0F;
  hard_snap_count_ = 0;
  smoothed_correction_count_ = 0;
  ignored_correction_count_ = 0;
  dropped_stale_correction_count_ = 0;
  missing_history_fallback_count_ = 0;
  dropped_old_input_count_ = 0;
}

void LocalPlayerPrediction::clear() noexcept {
  pending_inputs_.clear();
  state_ = {};
  correction_count_ = 0;
  correction_distance_sum_ = 0.0;
  max_correction_distance_ = 0.0F;
  hard_snap_count_ = 0;
  smoothed_correction_count_ = 0;
  ignored_correction_count_ = 0;
  dropped_stale_correction_count_ = 0;
  missing_history_fallback_count_ = 0;
  dropped_old_input_count_ = 0;
}

const PredictedPlayerState& LocalPlayerPrediction::state() const noexcept {
  return state_;
}

const PlayerPredictionSettings& LocalPlayerPrediction::settings() const noexcept {
  return settings_;
}

movement::PlayerKinematicState LocalPlayerPrediction::kinematic_state() const noexcept {
  return kinematic_state_from_prediction(state_);
}

std::span<const PendingInputSample> LocalPlayerPrediction::pending_inputs() const noexcept {
  return pending_inputs_;
}

PlayerPredictionDebugStats LocalPlayerPrediction::debug_stats() const noexcept {
  return {
      pending_inputs_.size(),
      state_.last_predicted_tick,
      state_.last_confirmed_server_tick,
      correction_count_,
      correction_count_ == 0U
          ? 0.0F
          : static_cast<f32>(correction_distance_sum_ / static_cast<f64>(correction_count_)),
      max_correction_distance_,
      hard_snap_count_,
      smoothed_correction_count_,
      ignored_correction_count_,
      dropped_stale_correction_count_,
      missing_history_fallback_count_,
      dropped_old_input_count_};
}

void LocalPlayerPrediction::predict_local_input(
    TickIndex input_tick,
    protocol::InputSample input,
    const physics::ICollisionQuery* collision_query,
    const player::PlayerTuning& tuning,
    movement::PlayerMovementAuthority authority,
    Seconds tick_seconds) {
  if (input_tick <= state_.last_confirmed_server_tick ||
      (!pending_inputs_.empty() && input_tick <= pending_inputs_.back().input_tick)) {
    ++dropped_old_input_count_;
    return;
  }

  reserve_history();
  pending_inputs_.push_back({input_tick, input, state_});

  const player::PlayerCommand command =
      protocol::input_sample_to_command(input, input_tick);
  const movement::PlayerMovementStep step = movement::simulate_player_movement_tick(
      collision_query,
      kinematic_state_from_prediction(state_),
      command,
      tuning,
      authority,
      tick_seconds);
  apply_step_state(step.state, input_tick);
  pending_inputs_.back().predicted_state = state_;
  prune_old_inputs(input_tick);
}

PlayerReconciliationResult LocalPlayerPrediction::reconcile_authoritative_state(
    const protocol::PlayerCorrection& correction,
    const physics::ICollisionQuery* collision_query,
    const player::PlayerTuning& tuning,
    Seconds tick_seconds) {
  PlayerReconciliationResult result;
  if (correction.server_tick <= state_.last_confirmed_server_tick) {
    result.stale = true;
    ++dropped_stale_correction_count_;
    return result;
  }

  const auto predicted_at_tick = std::find_if(
      pending_inputs_.begin(),
      pending_inputs_.end(),
      [&](const PendingInputSample& sample) {
        return sample.input_tick == correction.server_tick;
      });
  if (predicted_at_tick != pending_inputs_.end()) {
    result.position_error =
        position_distance(predicted_at_tick->predicted_state.position, correction.position);
  } else {
    result.position_error = position_distance(state_.position, correction.position);
    result.missing_history = correction.server_tick < state_.last_predicted_tick;
    if (result.missing_history) {
      ++missing_history_fallback_count_;
    }
  }

  result.visual_action = classify_position_error(result.position_error, settings_);
  ++correction_count_;
  correction_distance_sum_ += static_cast<f64>(result.position_error);
  max_correction_distance_ = std::max(max_correction_distance_, result.position_error);
  switch (result.visual_action) {
    case PlayerCorrectionVisualAction::Ignore:
      ++ignored_correction_count_;
      break;
    case PlayerCorrectionVisualAction::Smooth:
      ++smoothed_correction_count_;
      break;
    case PlayerCorrectionVisualAction::Snap:
      ++hard_snap_count_;
      break;
    case PlayerCorrectionVisualAction::None:
      break;
  }

  usize acknowledged_count = 0;
  while (acknowledged_count < pending_inputs_.size() &&
         pending_inputs_[acknowledged_count].input_tick <= correction.server_tick) {
    ++acknowledged_count;
  }
  if (acknowledged_count > 0U) {
    pending_inputs_.erase(
        pending_inputs_.begin(),
        pending_inputs_.begin() + static_cast<isize>(acknowledged_count));
  }
  result.acknowledged_input_count = acknowledged_count;

  state_ = predicted_state_from_kinematic(
      kinematic_state_from_correction(correction),
      correction.server_tick,
      correction.server_tick);

  const movement::PlayerMovementAuthority authority = authority_from_correction(correction);
  for (PendingInputSample& sample : pending_inputs_) {
    const player::PlayerCommand command =
        protocol::input_sample_to_command(sample.input, sample.input_tick);
    const movement::PlayerMovementStep step = movement::simulate_player_movement_tick(
        collision_query,
        kinematic_state_from_prediction(state_),
        command,
        tuning,
        authority,
        tick_seconds);
    apply_step_state(step.state, sample.input_tick);
    sample.predicted_state = state_;
    ++result.replayed_input_count;
  }

  prune_old_inputs(state_.last_predicted_tick);
  return result;
}

usize LocalPlayerPrediction::max_history_ticks() const noexcept {
  return std::max<usize>(
      1U,
      static_cast<usize>(std::max<u32>(settings_.history_seconds, 1U)) *
          static_cast<usize>(std::max<TickRate>(tick_rate_, 1U)));
}

void LocalPlayerPrediction::reserve_history() {
  const usize target = max_history_ticks();
  if (pending_inputs_.capacity() < target) {
    pending_inputs_.reserve(target);
  }
}

void LocalPlayerPrediction::prune_old_inputs(TickIndex newest_tick) {
  const usize max_ticks = max_history_ticks();
  usize drop_count = 0;
  while (drop_count < pending_inputs_.size()) {
    const PendingInputSample& sample = pending_inputs_[drop_count];
    const bool too_many = pending_inputs_.size() - drop_count > max_ticks;
    const bool too_old = newest_tick > sample.input_tick &&
                         newest_tick - sample.input_tick > max_ticks;
    if (!too_many && !too_old) {
      break;
    }
    ++drop_count;
  }
  if (drop_count == 0U) {
    return;
  }
  pending_inputs_.erase(
      pending_inputs_.begin(),
      pending_inputs_.begin() + static_cast<isize>(drop_count));
  dropped_old_input_count_ += drop_count;
}

void LocalPlayerPrediction::apply_step_state(
    movement::PlayerKinematicState state,
    TickIndex predicted_tick) noexcept {
  state_ = predicted_state_from_kinematic(
      state,
      predicted_tick,
      state_.last_confirmed_server_tick);
}

} // namespace arv::client
