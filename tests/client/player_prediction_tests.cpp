#include "arv/client/player_prediction.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] arv::protocol::InputSample forward_input() noexcept {
  arv::protocol::InputSample input;
  input.move_z = 1;
  return input;
}

[[nodiscard]] arv::protocol::InputSample right_input() noexcept {
  arv::protocol::InputSample input;
  input.move_x = 1;
  return input;
}

[[nodiscard]] arv::protocol::ClientboundPlayerCorrection correction_at(
    arv::TickIndex tick,
    arv::Vec3f position) {
  arv::protocol::ClientboundPlayerCorrection correction;
  correction.server_tick = tick;
  correction.position = position;
  correction.resolved_core_permissions.has_collision = false;
  return correction;
}

[[nodiscard]] arv::movement::PlayerMovementAuthority no_collision_authority() noexcept {
  return {false, false};
}

} // namespace

TEST(PlayerPredictionTests, StaleCorrectionIsIgnored) {
  arv::client::LocalPlayerPrediction prediction;
  prediction.reset({}, 10, arv::DefaultBaseTps);

  const arv::protocol::ClientboundPlayerCorrection stale =
      correction_at(10, {100.0F, 100.0F, 100.0F});
  const arv::client::PlayerReconciliationResult result =
      prediction.reconcile_authoritative_state(
          stale,
          nullptr,
          arv::player::default_player_tuning(),
          arv::tick_duration_seconds(arv::DefaultBaseTps));

  EXPECT_TRUE(result.stale);
  EXPECT_EQ(prediction.state().position, (arv::Vec3f{}));
  EXPECT_EQ(prediction.debug_stats().dropped_stale_correction_count, 1U);
}

TEST(PlayerPredictionTests, AcknowledgedInputsAreRemovedAndRemainingInputsReplay) {
  arv::client::LocalPlayerPrediction prediction;
  const arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  const arv::Seconds dt = arv::tick_duration_seconds(arv::DefaultBaseTps);
  prediction.reset({}, 0, arv::DefaultBaseTps);

  prediction.predict_local_input(1, forward_input(), nullptr, tuning, no_collision_authority(), dt);
  prediction.predict_local_input(2, right_input(), nullptr, tuning, no_collision_authority(), dt);
  ASSERT_EQ(prediction.pending_inputs().size(), 2U);

  const arv::client::PlayerReconciliationResult result =
      prediction.reconcile_authoritative_state(
          correction_at(1, {0.0F, 0.0F, 1.0F}),
          nullptr,
          tuning,
          dt);

  ASSERT_EQ(prediction.pending_inputs().size(), 1U);
  EXPECT_EQ(prediction.pending_inputs().front().input_tick, 2U);
  EXPECT_EQ(result.acknowledged_input_count, 1U);
  EXPECT_EQ(result.replayed_input_count, 1U);
  EXPECT_NEAR(prediction.state().position.x, tuning.fly_speed * dt, 0.0001F);
  EXPECT_NEAR(prediction.state().position.z, 1.0F, 0.0001F);
}

TEST(PlayerPredictionTests, MissingHistoryFallsBackToAuthoritativeState) {
  arv::client::LocalPlayerPrediction prediction;
  arv::client::PlayerPredictionSettings settings;
  settings.history_seconds = 1;
  const arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  constexpr arv::TickRate TestTickRate = 1;
  constexpr arv::Seconds Dt = 1.0F;
  prediction.reset({}, 0, TestTickRate, settings);

  prediction.predict_local_input(1, forward_input(), nullptr, tuning, no_collision_authority(), Dt);
  prediction.predict_local_input(2, forward_input(), nullptr, tuning, no_collision_authority(), Dt);
  prediction.predict_local_input(3, right_input(), nullptr, tuning, no_collision_authority(), Dt);
  ASSERT_EQ(prediction.pending_inputs().size(), 1U);
  ASSERT_EQ(prediction.pending_inputs().front().input_tick, 3U);

  const arv::client::PlayerReconciliationResult result =
      prediction.reconcile_authoritative_state(
          correction_at(1, {10.0F, 0.0F, 10.0F}),
          nullptr,
          tuning,
          Dt);

  EXPECT_TRUE(result.missing_history);
  EXPECT_EQ(prediction.debug_stats().missing_history_fallback_count, 1U);
  EXPECT_EQ(result.replayed_input_count, 1U);
  EXPECT_FLOAT_EQ(prediction.state().position.x, 10.0F + tuning.fly_speed);
  EXPECT_FLOAT_EQ(prediction.state().position.z, 10.0F);
}

TEST(PlayerPredictionTests, CorrectionErrorClassifiesIgnoreSmoothAndSnap) {
  arv::client::LocalPlayerPrediction prediction;
  const arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  const arv::Seconds dt = arv::tick_duration_seconds(arv::DefaultBaseTps);

  prediction.reset({}, 0, arv::DefaultBaseTps);
  prediction.predict_local_input(1, forward_input(), nullptr, tuning, no_collision_authority(), dt);
  arv::client::PlayerReconciliationResult result =
      prediction.reconcile_authoritative_state(
          correction_at(1, prediction.state().position),
          nullptr,
          tuning,
          dt);
  EXPECT_EQ(result.visual_action, arv::client::PlayerCorrectionVisualAction::Ignore);

  prediction.reset({}, 0, arv::DefaultBaseTps);
  prediction.predict_local_input(1, forward_input(), nullptr, tuning, no_collision_authority(), dt);
  result = prediction.reconcile_authoritative_state(
      correction_at(1, {0.1F, 0.0F, prediction.state().position.z}),
      nullptr,
      tuning,
      dt);
  EXPECT_EQ(result.visual_action, arv::client::PlayerCorrectionVisualAction::Smooth);

  prediction.reset({}, 0, arv::DefaultBaseTps);
  prediction.predict_local_input(1, forward_input(), nullptr, tuning, no_collision_authority(), dt);
  result = prediction.reconcile_authoritative_state(
      correction_at(1, {5.0F, 0.0F, 5.0F}),
      nullptr,
      tuning,
      dt);
  EXPECT_EQ(result.visual_action, arv::client::PlayerCorrectionVisualAction::Snap);
  EXPECT_EQ(prediction.debug_stats().hard_snap_count, 1U);
}
