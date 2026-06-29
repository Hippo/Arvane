#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

struct ClientboundSpaceBodyView {
  std::string body_id{};
  std::string display_name{};
  std::string target_world_id{};
  std::string render_model{};
  std::string render_texture{};
  Vec3f position{};
  f32 visual_radius{};
  f32 entry_radius{};
  bool entry_enabled{};
};

struct ClientboundWorldEnvironment {
  f32 gravity_scale{1.0F};
  bool has_atmosphere{true};
  bool has_oxygen{true};
  std::string sky{"overworld"};
  f32 ambient_light{0.15F};
  std::string fog{"none"};
};

struct ClientboundWorldJoinResult {
  bool accepted{};
  std::string reject_reason{};
  WorldID world_id{};
  PlayerID player_id{};
  EntityNetID local_entity_id{};
  Vec3f spawn_position{};
  u64 initial_tick{};
  u32 tick_rate{};
  i32 effective_view_distance{};
  ClientboundWorldEnvironment environment{};
  bool space_environment{};
  std::vector<ClientboundSpaceBodyView> space_bodies{};
};

struct ClientboundWorldLeave {
  WorldID world_id{};
  u16 reason{};
};

std::vector<u8> serialize_clientbound_world_join_result(const ClientboundWorldJoinResult& result);
Expected<ClientboundWorldJoinResult, ProtocolError> deserialize_clientbound_world_join_result(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_world_leave(const ClientboundWorldLeave& leave);
Expected<ClientboundWorldLeave, ProtocolError> deserialize_clientbound_world_leave(std::span<const u8> bytes);

} // namespace arv::protocol
