#include "arv/protocol/particles.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> serialize_clientbound_spawn_particles(const ClientboundSpawnParticles& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.world_id.value);
  writer.write_string(packet.particle_type);
  writer.write_string(packet.texture_asset);
  write_vec3(writer, packet.position);
  write_vec3(writer, packet.normal);
  write_vec3(writer, packet.extents);
  write_f32(writer, packet.radius);
  writer.write<u16>(packet.count);
  writer.write<u32>(packet.seed);
  writer.write<ParticleSpawnShape>(packet.shape);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSpawnParticles, ProtocolError> deserialize_clientbound_spawn_particles(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto particle_type = reader.read_string();
  auto texture_asset = reader.read_string();
  auto position = read_vec3(reader);
  auto normal = read_vec3(reader);
  auto extents = read_vec3(reader);
  auto radius = read_f32(reader);
  auto count = reader.read<u16>();
  auto seed = reader.read<u32>();
  auto shape = reader.read<ParticleSpawnShape>();
  if (!world_id || !particle_type || !texture_asset || !position || !normal || !extents || !radius ||
      !count || !seed || !shape) {
    return Expected<ClientboundSpawnParticles, ProtocolError>::failure(
        ProtocolError{"failed to decode spawn particles message"});
  }
  if (particle_type.value().empty() ||
      particle_type.value().size() > MaxParticleTypeNameBytes) {
    return Expected<ClientboundSpawnParticles, ProtocolError>::failure(
        ProtocolError{"invalid particle type name length"});
  }
  if (texture_asset.value().size() > MaxParticleTextureAssetNameBytes) {
    return Expected<ClientboundSpawnParticles, ProtocolError>::failure(
        ProtocolError{"invalid particle texture asset name length"});
  }
  if (count.value() > MaxSpawnParticleCount) {
    return Expected<ClientboundSpawnParticles, ProtocolError>::failure(
        ProtocolError{"too many particles requested"});
  }
  switch (shape.value()) {
    case ParticleSpawnShape::Point:
    case ParticleSpawnShape::Box:
    case ParticleSpawnShape::BlockFace:
    case ParticleSpawnShape::Sphere:
      break;
    default:
      return Expected<ClientboundSpawnParticles, ProtocolError>::failure(
          ProtocolError{"invalid particle spawn shape"});
  }

  auto trailing = require_no_trailing_bytes(reader, "spawn particles message");
  if (!trailing) {
    return Expected<ClientboundSpawnParticles, ProtocolError>::failure(trailing.error());
  }

  ClientboundSpawnParticles packet;
  packet.world_id = WorldID{world_id.value()};
  packet.particle_type = std::move(particle_type.value());
  packet.texture_asset = std::move(texture_asset.value());
  packet.position = position.value();
  packet.normal = normal.value();
  packet.extents = extents.value();
  packet.radius = radius.value();
  packet.count = count.value();
  packet.seed = seed.value();
  packet.shape = shape.value();
  return Expected<ClientboundSpawnParticles, ProtocolError>::success(std::move(packet));
}

} // namespace arv::protocol
