#pragma once

#include "arv/client/item_animation.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/types.hpp"
#include "arv/platform/input_snapshot.hpp"

namespace arv::web {

struct BrowserInputState {
  platform::InputSnapshot snapshot{};
  bool primary_animation_pressed{};
  bool secondary_animation_pressed{};
  bool cursor_initialized{};
  double last_frame_time{};
  double movement_accumulator_seconds{};
  u64 client_tick{};
  client::HeldItemAnimationPlayer held_item_animation{};
  bool held_item_secondary_use_active{};
  client::LocalBlockMiningParticleState block_mining_particles{};
  client::LocalSprintGroundParticleState sprint_ground_particles{};
  client::LocalParticleSeedState particle_seeds{};
};

} // namespace arv::web
