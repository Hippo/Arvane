#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

struct ClientboundExplosionEvent {
  WorldID world_id{};
  Vec3f origin{};
  std::string profile_id{};
  f32 radius{};
  u64 seed{};
  bool visual_enabled{true};
  bool future_sound_enabled{true};
  std::string future_sound_id{};
};

inline constexpr usize MaxExplosionProfileIDBytes = 128U;
inline constexpr usize MaxExplosionSoundIDBytes = 160U;

std::vector<u8> serialize_clientbound_explosion_event(
    const ClientboundExplosionEvent& packet);
Expected<ClientboundExplosionEvent, ProtocolError> deserialize_clientbound_explosion_event(
    std::span<const u8> bytes);

} // namespace arv::protocol
