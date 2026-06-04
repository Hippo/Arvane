#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

enum class ParticleSpawnShape : u8 {
  Point = 0,
  Box = 1,
  BlockFace = 2,
  Sphere = 3,
};

struct ClientboundSpawnParticles {
  WorldID world_id{};
  std::string particle_type{};
  std::string texture_asset{};
  Vec3f position{};
  Vec3f normal{0.0F, 1.0F, 0.0F};
  Vec3f extents{};
  f32 radius{};
  u16 count{};
  u32 seed{};
  ParticleSpawnShape shape{ParticleSpawnShape::Point};
};

inline constexpr usize MaxParticleTypeNameBytes = 128U;
inline constexpr usize MaxParticleTextureAssetNameBytes = 160U;
inline constexpr u16 MaxSpawnParticleCount = 4096U;

std::vector<u8> serialize_clientbound_spawn_particles(const ClientboundSpawnParticles& packet);
Expected<ClientboundSpawnParticles, ProtocolError> deserialize_clientbound_spawn_particles(
    std::span<const u8> bytes);

} // namespace arv::protocol
