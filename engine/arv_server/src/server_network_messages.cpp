#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/io/files.hpp"
#include "arv/physics/player_movement.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/server/skin_runtime.hpp"
#include "arv/text/chat_message.hpp"
#include "arv/text/language.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace arv::server::detail {
namespace {

inline constexpr f32 VehicleCollisionStopSpeedEpsilonBlocksPerSecond = 0.02F;
inline constexpr f32 MinVehicleValidationDtSeconds = 1.0F / 60.0F;
inline constexpr f32 MaxVehicleValidationDtSeconds = 0.25F;
inline constexpr std::string_view ChatSendPermission = "arvane.chat.send";
inline constexpr std::string_view ChatColorPermission = "arvane.chat.color";
inline constexpr std::string_view ChatClickPermission = "arvane.chat.click";
inline constexpr std::string_view ChatHoverPermission = "arvane.chat.hover";

void configure_skin_transfer_state(
    SkinTransferSessionState& state,
    const io::SkinConfig& skins) {
  if (state.limiters_initialized) {
    return;
  }
  state.upload_begin_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute),
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute) / 60.0};
  state.completed_upload_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute),
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute) / 60.0};
  state.completed_upload_hour_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour),
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour) / 3600.0};
  state.upload_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour),
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour) / 3600.0};
  state.failed_decode_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute),
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute) / 60.0};
  state.request_second_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_second),
      static_cast<double>(skins.downloads.max_requests_per_second)};
  state.request_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_minute),
      static_cast<double>(skins.downloads.max_requests_per_minute) / 60.0};
  state.download_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_bytes_per_minute),
      static_cast<double>(skins.downloads.max_bytes_per_minute) / 60.0};
  state.limiters_initialized = true;
}

[[nodiscard]] TickIndex seconds_to_ticks(u32 seconds, TickRate tick_rate) noexcept {
  const TickIndex ticks_per_second = std::max<TickIndex>(1U, tick_rate);
  if (seconds > std::numeric_limits<TickIndex>::max() / ticks_per_second) {
    return std::numeric_limits<TickIndex>::max();
  }
  return std::max<TickIndex>(1U, static_cast<TickIndex>(seconds) * ticks_per_second);
}

[[nodiscard]] bool elapsed_ticks_exceeds(
    TickIndex now,
    TickIndex then,
    TickIndex limit) noexcept {
  return now < then || now - then > limit;
}

[[nodiscard]] bool skin_upload_timed_out(
    const ActiveSkinUpload& upload,
    TickIndex tick_index,
    const io::SkinConfig& skins,
    TickRate tick_rate) noexcept {
  return elapsed_ticks_exceeds(
             tick_index,
             upload.started_tick,
             seconds_to_ticks(skins.uploads.timeout_seconds, tick_rate)) ||
         elapsed_ticks_exceeds(
             tick_index,
             upload.last_packet_tick,
             seconds_to_ticks(skins.uploads.idle_timeout_seconds, tick_rate));
}

[[nodiscard]] bool is_command_chat_message(std::string_view message) noexcept {
  return !message.empty() && message.front() == '/';
}

[[nodiscard]] bool command_message_is_error(
    command::CommandMessageKind kind) noexcept {
  return kind == command::CommandMessageKind::Error;
}

[[nodiscard]] bool command_status_is_error(
    command::CommandStatus status) noexcept {
  return status != command::CommandStatus::Success;
}

[[nodiscard]] arv::text::RichText authored_chat_message(std::string_view message) {
  return arv::text::ChatMessageTemplate::parse(message).to_rich_text();
}

[[nodiscard]] std::string fallback_command_result_message(
    const arv::text::LanguageCatalog* language,
    command::CommandStatus status) {
  switch (status) {
    case command::CommandStatus::Success:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.success",
          "Command succeeded.");
    case command::CommandStatus::Failure:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.failure",
          "Command failed.");
    case command::CommandStatus::UsageError:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.usage_error",
          "Invalid command usage.");
    case command::CommandStatus::PermissionDenied:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.permission_denied",
          "Permission denied.");
    case command::CommandStatus::UnknownCommand:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.unknown_command",
          "Unknown command.");
    case command::CommandStatus::InvalidSource:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.invalid_source",
          "Command cannot be used from here.");
    case command::CommandStatus::InternalError:
      return arv::text::text_or_fallback(
          language,
          "ui.chat.command.internal_error",
          "Command failed internally.");
  }
  return arv::text::text_or_fallback(
      language,
      "ui.chat.command.failure",
      "Command failed.");
}

void clear_active_skin_upload(
    SkinTransferSessionState& state,
    u32& active_skin_uploads_global) noexcept {
  if (!state.active_upload.has_value()) {
    return;
  }
  state.active_upload.reset();
  if (active_skin_uploads_global > 0U) {
    --active_skin_uploads_global;
  }
}

[[nodiscard]] std::optional<u64> regular_file_size(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return std::nullopt;
  }
  error.clear();
  const u64 size = std::filesystem::file_size(path, error);
  if (error) {
    return std::nullopt;
  }
  return size;
}

[[nodiscard]] std::string_view skin_request_deny_reason_name(
    protocol::SkinRequestDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinRequestDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinRequestDenyReason::InvalidHash:
      return "InvalidHash";
    case protocol::SkinRequestDenyReason::NotFound:
      return "NotFound";
    case protocol::SkinRequestDenyReason::NotAdvertisedToClient:
      return "NotAdvertisedToClient";
    case protocol::SkinRequestDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinRequestDenyReason::TooLarge:
      return "TooLarge";
    case protocol::SkinRequestDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] std::string_view skin_upload_deny_reason_name(
    protocol::SkinUploadDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinUploadDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinUploadDenyReason::UploadsDisabled:
      return "UploadsDisabled";
    case protocol::SkinUploadDenyReason::NotAuthenticated:
      return "NotAuthenticated";
    case protocol::SkinUploadDenyReason::InvalidFormat:
      return "InvalidFormat";
    case protocol::SkinUploadDenyReason::InvalidResolution:
      return "InvalidResolution";
    case protocol::SkinUploadDenyReason::ResolutionNotAllowed:
      return "ResolutionNotAllowed";
    case protocol::SkinUploadDenyReason::UploadTooLarge:
      return "UploadTooLarge";
    case protocol::SkinUploadDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinUploadDenyReason::UploadAlreadyPending:
      return "UploadAlreadyPending";
    case protocol::SkinUploadDenyReason::ServerStorageFull:
      return "ServerStorageFull";
    case protocol::SkinUploadDenyReason::DecodeFailed:
      return "DecodeFailed";
    case protocol::SkinUploadDenyReason::HashMismatch:
      return "HashMismatch";
    case protocol::SkinUploadDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] player::PlayerCommand action_command_from_move(
    const protocol::ServerboundPlayerMove& move) noexcept {
  player::PlayerCommand command;
  command.client_tick = move.client_tick;
  command.primary_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::PrimaryPressed);
  command.primary_held =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::PrimaryHeld);
  command.secondary_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::SecondaryPressed);
  command.secondary_held =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::SecondaryHeld);
  command.toggle_fly_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::ToggleFlyPressed);
  command.selected_hotbar_slot = move.selected_hotbar_slot;
  command.sneak_held =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sneaking);
  command.sprint_held =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sprinting);
  return command;
}

[[nodiscard]] physics::PlayerCollisionPose collision_pose_from_flags(
    protocol::PlayerMoveFlags flags) noexcept {
  if (protocol::has_move_flag(flags, protocol::PlayerMoveFlags::Swimming)) {
    return physics::PlayerCollisionPose::Swimming;
  }
  if (protocol::has_move_flag(flags, protocol::PlayerMoveFlags::Sneaking)) {
    return physics::PlayerCollisionPose::Sneaking;
  }
  return physics::PlayerCollisionPose::Standing;
}

[[nodiscard]] MovementValidationContext make_movement_validation_context(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const player::PlayerState& player_state,
    const protocol::ServerboundPlayerMove& move,
    TickIndex tick_index,
    TickRate tick_rate,
    const player::PlayerTuning& tuning) {
  MovementValidationContext context;
  context.receive_time_seconds =
      static_cast<double>(tick_index) / static_cast<double>(std::max<TickRate>(tick_rate, 1U));
  context.server_tick = tick_index;
  context.can_fly = player_state.resolved_permissions.core.can_fly;
  context.has_collision = player_state.resolved_permissions.core.has_collision;
  context.server_velocity = player_state.velocity;
  context.knockback_control_suppression_ticks =
      player_state.knockback_control_suppression_ticks;

  const BlockPos destination_block{{
      static_cast<i32>(std::floor(move.position.x)),
      static_cast<i32>(std::floor(move.position.y)),
      static_cast<i32>(std::floor(move.position.z)),
  }};
  context.target_chunk_loaded = world.find_chunk(block_to_chunk(destination_block)) != nullptr;

  if (context.has_collision) {
    world::WorldCollisionQuery query(world, block_registry, false);
    const physics::KinematicConfig physics_config =
        physics::kinematic_config_from_tuning(tuning);
    const Aabb3f bounds = physics::make_body_bounds(
        move.position,
        physics_config,
        collision_pose_from_flags(move.flags));
    context.final_aabb_overlaps_solid = physics::overlaps_solid_blocks(
        query,
        bounds,
        physics_config.collision_epsilon);
  }
  return context;
}

[[nodiscard]] u32 knockback_suppression_ticks_to_consume(
    const AcceptedPlayerMovementState& previous,
    TickIndex tick_index,
    u32 remaining_ticks) noexcept {
  if (remaining_ticks == 0U) {
    return 0U;
  }
  if (!previous.has_accepted_move || tick_index <= previous.last_server_tick) {
    return 1U;
  }
  const TickIndex elapsed_ticks = tick_index - previous.last_server_tick;
  const TickIndex capped_ticks = std::min<TickIndex>(
      elapsed_ticks,
      static_cast<TickIndex>(remaining_ticks));
  return static_cast<u32>(std::max<TickIndex>(1U, capped_ticks));
}

[[nodiscard]] Aabb3f vehicle_bounds(Vec3f position, Vec3f size) noexcept {
  const Vec3f half{size.x * 0.5F, 0.0F, size.z * 0.5F};
  return {
      {position.x - half.x, position.y, position.z - half.z},
      {position.x + half.x, position.y + size.y, position.z + half.z}};
}

[[nodiscard]] f32 vehicle_validation_elapsed_seconds(
    const protocol::ServerboundVehicleMove& move,
    const AcceptedVehicleMovementState& previous,
    double receive_time_seconds) noexcept {
  if (previous.has_accepted_move) {
    const double raw_elapsed = receive_time_seconds - previous.last_receive_time_seconds;
    if (std::isfinite(raw_elapsed) && raw_elapsed > 0.0) {
      return std::clamp(
          static_cast<f32>(raw_elapsed),
          MinVehicleValidationDtSeconds,
          MaxVehicleValidationDtSeconds);
    }
  }
  return std::clamp(
      move.dt_seconds,
      MinVehicleValidationDtSeconds,
      MaxVehicleValidationDtSeconds);
}

[[nodiscard]] Vec3f collision_adjusted_vehicle_velocity(
    Vec3f velocity,
    const physics::MovementResolution& resolved) noexcept {
  if (resolved.hit_x) {
    velocity.x = 0.0F;
  }
  if (resolved.hit_y) {
    velocity.y = 0.0F;
  }
  if (resolved.hit_z) {
    velocity.z = 0.0F;
  }
  if (length(velocity) < VehicleCollisionStopSpeedEpsilonBlocksPerSecond) {
    return {};
  }
  return velocity;
}

[[nodiscard]] Vec3f crash_axis_normal(f32 attempted_delta, f32 velocity_axis) noexcept {
  const f32 direction =
      std::abs(attempted_delta) > 0.0001F ? attempted_delta : velocity_axis;
  if (direction > 0.0F) {
    return {-1.0F, 0.0F, 0.0F};
  }
  return {1.0F, 0.0F, 0.0F};
}

[[nodiscard]] std::optional<VehicleCrashContact> vehicle_crash_contact_from_resolution(
    const physics::MovementResolution& resolved,
    Vec3f pre_impact_velocity,
    bool resolved_safe) noexcept {
  VehicleCrashContact contact;
  contact.pre_impact_velocity = pre_impact_velocity;
  contact.resolved_safe = resolved_safe;
  f32 best_impact = 0.0F;

  const auto consider = [&](Vec3f normal) {
    const f32 impact = std::max(0.0F, dot(pre_impact_velocity * -1.0F, normal));
    if (impact > best_impact) {
      best_impact = impact;
      contact.normal = normal;
    }
  };
  if (resolved.hit_x) {
    consider(crash_axis_normal(resolved.attempted_delta.x, pre_impact_velocity.x));
  }
  if (resolved.hit_y) {
    Vec3f normal{0.0F, 1.0F, 0.0F};
    const f32 direction =
        std::abs(resolved.attempted_delta.y) > 0.0001F
            ? resolved.attempted_delta.y
            : pre_impact_velocity.y;
    if (direction > 0.0F) {
      normal.y = -1.0F;
    }
    consider(normal);
  }
  if (resolved.hit_z) {
    Vec3f normal{0.0F, 0.0F, 1.0F};
    const f32 direction =
        std::abs(resolved.attempted_delta.z) > 0.0001F
            ? resolved.attempted_delta.z
            : pre_impact_velocity.z;
    if (direction > 0.0F) {
      normal.z = -1.0F;
    }
    consider(normal);
  }
  if (best_impact <= 0.0F) {
    return std::nullopt;
  }
  return contact;
}

void record_vehicle_crash_contact(
    VehicleMovementValidationContext& context,
    const physics::MovementResolution& resolved,
    Vec3f pre_impact_velocity,
    bool resolved_safe) noexcept {
  const std::optional<VehicleCrashContact> contact =
      vehicle_crash_contact_from_resolution(
          resolved,
          pre_impact_velocity,
          resolved_safe);
  if (!contact.has_value()) {
    return;
  }
  const f32 existing_impact =
      context.has_collision_contact
          ? std::max(
                0.0F,
                dot(context.collision_contact.pre_impact_velocity * -1.0F,
                    context.collision_contact.normal))
          : -1.0F;
  const f32 next_impact =
      std::max(0.0F, dot(pre_impact_velocity * -1.0F, contact->normal));
  if (!context.has_collision_contact || next_impact > existing_impact) {
    context.has_collision_contact = true;
    context.collision_contact = contact.value();
  }
}

[[nodiscard]] AcceptedVehicleMovementState collision_resolved_vehicle_state(
    const protocol::ServerboundVehicleMove& move,
    const AcceptedVehicleMovementState& previous,
    const physics::MovementResolution& resolved,
    TickIndex tick_index,
    double receive_time_seconds) noexcept {
  AcceptedVehicleMovementState state = previous;
  state.position = previous.position + resolved.applied_delta;
  state.velocity = collision_adjusted_vehicle_velocity(move.velocity, resolved);
  state.rotation = normalize_quat(move.rotation);
  state.angular_velocity =
      length(state.velocity) < VehicleCollisionStopSpeedEpsilonBlocksPerSecond
          ? Vec3f{}
          : move.angular_velocity;
  state.flags = move.flags;
  state.last_sequence = move.sequence;
  state.last_server_tick = tick_index;
  state.last_receive_time_seconds = receive_time_seconds;
  state.has_accepted_move = true;
  return state;
}

[[nodiscard]] u32 vehicle_water_support_samples(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    Vec3f position,
    Vec3f size) noexcept {
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  const std::array<Vec3f, 5> samples{{
      position,
      {position.x - half_x, position.y, position.z - half_z},
      {position.x + half_x, position.y, position.z - half_z},
      {position.x - half_x, position.y, position.z + half_z},
      {position.x + half_x, position.y, position.z + half_z},
  }};
  u32 count = 0U;
  for (const Vec3f& sample : samples) {
    const BlockPos block{{
        floor_to_i32(sample.x),
        floor_to_i32(sample.y),
        floor_to_i32(sample.z),
    }};
    if (world::world_cell_contains_fluid(world, block_registry, block)) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] f32 vehicle_water_occupancy(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    Vec3f position,
    Vec3f size) noexcept {
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  const f32 mid_y = position.y + size.y * 0.5F;
  const f32 top_y = position.y + size.y * 0.9F;
  const f32 bottom_y = position.y + size.y * 0.1F;
  const std::array<Vec3f, 11> samples{{
      {position.x, mid_y, position.z},
      {position.x, bottom_y, position.z},
      {position.x, top_y, position.z},
      {position.x, mid_y, position.z - half_z},
      {position.x, mid_y, position.z + half_z},
      {position.x - half_x, mid_y, position.z},
      {position.x + half_x, mid_y, position.z},
      {position.x - half_x, mid_y, position.z - half_z},
      {position.x + half_x, mid_y, position.z - half_z},
      {position.x - half_x, mid_y, position.z + half_z},
      {position.x + half_x, mid_y, position.z + half_z},
  }};
  u32 count = 0U;
  for (const Vec3f& sample : samples) {
    const BlockPos block{{
        floor_to_i32(sample.x),
        floor_to_i32(sample.y),
        floor_to_i32(sample.z),
    }};
    if (world::world_cell_contains_fluid(world, block_registry, block)) {
      ++count;
    }
  }
  return static_cast<f32>(count) / static_cast<f32>(samples.size());
}

[[nodiscard]] bool vehicle_position_chunk_loaded(
    const world::WorldRuntime& world,
    Vec3f position) noexcept {
  const BlockPos block{{
      floor_to_i32(position.x),
      floor_to_i32(position.y),
      floor_to_i32(position.z),
  }};
  return world.find_chunk(block_to_chunk(block)) != nullptr;
}

[[nodiscard]] bool vehicle_path_chunks_loaded(
    const world::WorldRuntime& world,
    Vec3f from,
    Vec3f to) noexcept {
  const f32 path_length = distance(from, to);
  const u32 steps = static_cast<u32>(
      std::clamp(std::ceil(path_length / 8.0F), 1.0F, 32.0F));
  for (u32 index = 0U; index <= steps; ++index) {
    const f32 alpha = static_cast<f32>(index) / static_cast<f32>(steps);
    if (!vehicle_position_chunk_loaded(world, lerp(from, to, alpha))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Vec3f model_visual_forward(Quatf rotation) noexcept {
  return rotate_vector(normalize_quat(rotation), {0.0F, 0.0F, -1.0F});
}

[[nodiscard]] std::string_view vehicle_motion_animation_name(
    const entity::VehicleDefinition& definition,
    const AcceptedVehicleMovementState& accepted) noexcept {
  if (definition.physics != entity::VehiclePhysicsKind::GroundVehicle) {
    return {};
  }
  constexpr f32 AnimationSpeedEpsilonBlocksPerSecond = 0.05F;
  const f32 signed_speed = dot(accepted.velocity, model_visual_forward(accepted.rotation));
  if (signed_speed > AnimationSpeedEpsilonBlocksPerSecond) {
    return "driving";
  }
  if (signed_speed < -AnimationSpeedEpsilonBlocksPerSecond) {
    return "reverse";
  }
  return {};
}

void update_vehicle_motion_animation(
    world::WorldRuntime& world,
    EntityID vehicle_entity,
    const entity::VehicleDefinition& definition,
    const AcceptedVehicleMovementState& accepted) {
  const auto anim = std::string{vehicle_motion_animation_name(definition, accepted)};
  (void)world.set_entity_animation(vehicle_entity, anim);
}

void apply_vehicle_accepted_state_to_world(
    world::WorldRuntime& world,
    EntityID vehicle_entity,
    const entity::VehicleDefinition& definition,
    const AcceptedVehicleMovementState& accepted) {
  (void)world.set_entity_transform(
      vehicle_entity,
      accepted.position,
      accepted.rotation);
  if (entity::EntityVelocity* velocity =
          world.find_entity_velocity(vehicle_entity);
      velocity != nullptr) {
    velocity->linear = accepted.velocity;
  }
  if (entity::VehiclePhysicsComponent* physics =
          world.find_entity_vehicle_physics(vehicle_entity);
      physics != nullptr) {
    physics->linear_velocity = accepted.velocity;
    physics->angular_velocity = accepted.angular_velocity;
    physics->grounded =
        protocol::has_vehicle_move_flag(accepted.flags, protocol::VehicleMoveFlags::OnGround);
    physics->in_water =
        protocol::has_vehicle_move_flag(accepted.flags, protocol::VehicleMoveFlags::InWater);
  }
  update_vehicle_motion_animation(
      world,
      vehicle_entity,
      definition,
      accepted);
  (void)world.update_passenger_attachment_transforms();
  if (const entity::RideableComponent* rideable =
          world.find_entity_rideable(vehicle_entity);
      rideable != nullptr) {
    for (const entity::Seat& seat : rideable->seats) {
      if (seat.occupant.value == 0U) {
        continue;
      }
      const std::optional<PlayerID> player_id =
          world.player_for_entity(seat.occupant);
      if (!player_id.has_value()) {
        continue;
      }
      player::PlayerState* player_state =
          world.find_player(player_id.value());
      const entity::EntityTransform* passenger_transform =
          world.find_entity_transform(seat.occupant);
      if (player_state != nullptr && passenger_transform != nullptr) {
        player_state->position = passenger_transform->position;
        player_state->velocity = accepted.velocity;
      }
    }
  }
}

[[nodiscard]] VehicleMovementValidationContext make_vehicle_validation_context(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    EntityID passenger_entity,
    EntityID vehicle_entity,
    const protocol::ServerboundVehicleMove& move,
    const AcceptedVehicleMovementState& previous,
    TickIndex tick_index,
    TickRate tick_rate) {
  VehicleMovementValidationContext context;
  context.receive_time_seconds =
      static_cast<double>(tick_index) / static_cast<double>(std::max<TickRate>(tick_rate, 1U));
  context.server_tick = tick_index;
  const std::optional<EntityID> mounted = world.mounted_rideable(passenger_entity);
  context.passenger_is_mounted = mounted.has_value() && mounted.value() == vehicle_entity;
  if (const entity::RideableComponent* rideable = world.find_entity_rideable(vehicle_entity);
      rideable != nullptr) {
    context.passenger_controls_vehicle = rideable->controller == passenger_entity;
  }
  context.target_chunk_loaded = vehicle_position_chunk_loaded(world, move.position);
  context.swept_path_chunks_loaded =
      previous.has_accepted_move
          ? vehicle_path_chunks_loaded(world, previous.position, move.position)
          : context.target_chunk_loaded;

  const entity::EntityCollision* collision = world.find_entity_collision(vehicle_entity);
  if (collision == nullptr) {
    return context;
  }

  world::WorldCollisionQuery query(world, block_registry, true);
  const Aabb3f end = vehicle_bounds(move.position, collision->box);
  context.final_aabb_overlaps_solid =
      physics::overlaps_solid_blocks(query, end, physics::CollisionEpsilon);

  std::optional<Aabb3f> collision_safe_start;
  if (previous.has_accepted_move) {
    const Aabb3f start = vehicle_bounds(previous.position, collision->box);
    if (!physics::overlaps_solid_blocks(query, start, physics::CollisionEpsilon)) {
      const f32 elapsed =
          vehicle_validation_elapsed_seconds(move, previous, context.receive_time_seconds);
      const physics::MovementResolution previous_velocity_sweep =
          physics::resolve_movement(
              query,
              start,
              previous.velocity * elapsed,
              physics::CollisionEpsilon);
      context.previous_velocity_sweep_hit_solid =
          previous_velocity_sweep.hit_x ||
          previous_velocity_sweep.hit_y ||
          previous_velocity_sweep.hit_z;
      if (context.previous_velocity_sweep_hit_solid) {
        record_vehicle_crash_contact(
            context,
            previous_velocity_sweep,
            previous.velocity,
            true);
      }
      const physics::MovementResolution proposed_sweep =
          physics::resolve_movement(
              query,
              start,
              move.position - previous.position,
              physics::CollisionEpsilon);
      context.proposed_sweep_hit_solid =
          proposed_sweep.hit_x ||
          proposed_sweep.hit_y ||
          proposed_sweep.hit_z;
      if (context.proposed_sweep_hit_solid) {
        const AcceptedVehicleMovementState resolved_state =
            collision_resolved_vehicle_state(
                move,
                previous,
                proposed_sweep,
                tick_index,
                context.receive_time_seconds);
        const Aabb3f resolved_end =
            vehicle_bounds(resolved_state.position, collision->box);
        if (!physics::overlaps_solid_blocks(
                query,
                resolved_end,
                physics::CollisionEpsilon)) {
          context.has_collision_resolved_state = true;
          context.collision_resolved_state = resolved_state;
          record_vehicle_crash_contact(
              context,
              proposed_sweep,
              previous.velocity,
              true);
        }
      }
      collision_safe_start = start;
    }
  }

  if (context.final_aabb_overlaps_solid && collision_safe_start.has_value() &&
      !context.has_collision_resolved_state) {
    const physics::MovementResolution resolved =
        physics::resolve_movement(
            query,
            collision_safe_start.value(),
            move.position - previous.position);
    const AcceptedVehicleMovementState resolved_state =
        collision_resolved_vehicle_state(
            move,
            previous,
            resolved,
            tick_index,
            context.receive_time_seconds);
    const Aabb3f resolved_end =
        vehicle_bounds(resolved_state.position, collision->box);
    if (!physics::overlaps_solid_blocks(
            query,
            resolved_end,
            physics::CollisionEpsilon)) {
      context.has_collision_resolved_state = true;
      context.collision_resolved_state = resolved_state;
      record_vehicle_crash_contact(
          context,
          resolved,
          previous.velocity,
          true);
    }
  }
  context.has_ground_support =
      physics::has_support_below(
          query,
          end,
          std::max(physics::CollisionEpsilon * 2.0F, 0.05F));
  context.water_support_samples =
      vehicle_water_support_samples(world, block_registry, move.position, collision->box);
  context.water_occupancy =
      vehicle_water_occupancy(world, block_registry, move.position, collision->box);
  return context;
}

[[nodiscard]] AcceptedVehicleMovementState accepted_vehicle_state_from_world(
    const world::WorldRuntime& world,
    EntityID vehicle_entity,
    EntityNetID vehicle_net_id,
    TickIndex tick_index,
    TickRate tick_rate) {
  AcceptedVehicleMovementState state;
  if (const entity::EntityTransform* transform = world.find_entity_transform(vehicle_entity);
      transform != nullptr) {
    state.position = transform->position;
    state.rotation = transform->rotation;
  }
  if (const entity::EntityVelocity* velocity = world.find_entity_velocity(vehicle_entity);
      velocity != nullptr) {
    state.velocity = velocity->linear;
  }
  if (const entity::VehiclePhysicsComponent* physics =
          world.find_entity_vehicle_physics(vehicle_entity);
      physics != nullptr) {
    state.velocity = physics->linear_velocity;
    state.angular_velocity = physics->angular_velocity;
  }
  state.last_sequence = 0U;
  state.last_server_tick = tick_index;
  state.last_receive_time_seconds =
      static_cast<double>(tick_index) / static_cast<double>(std::max<TickRate>(tick_rate, 1U));
  state.has_accepted_move = true;
  (void)vehicle_net_id;
  return state;
}

[[nodiscard]] std::optional<Vec3f> fallback_rideable_dismount_position(
    const world::WorldRuntime& world,
    EntityID rideable_entity,
    const player::PlayerTuning& tuning) noexcept {
  const entity::EntityTransform* transform = world.find_entity_transform(rideable_entity);
  const entity::EntityCollision* collision = world.find_entity_collision(rideable_entity);
  if (transform == nullptr) {
    return std::nullopt;
  }
  const f32 rideable_height =
      collision == nullptr ? 0.0F : std::max(collision->box.y, 0.0F);
  const f32 vertical_clearance =
      rideable_height + std::max(tuning.body_height, 0.0F) +
      physics::CollisionEpsilon;
  return transform->position + Vec3f{0.0F, vertical_clearance, 0.0F};
}

[[nodiscard]] bool crash_severity_at_least(
    entity::CrashSeverity severity,
    entity::CrashSeverity minimum) noexcept {
  return static_cast<u8>(severity) >= static_cast<u8>(minimum);
}

[[nodiscard]] f32 crash_damage_for_severity(
    const entity::VehicleCrashDefinition& crash,
    entity::CrashSeverity severity) noexcept {
  switch (severity) {
    case entity::CrashSeverity::Bump:
      return crash.bump_damage;
    case entity::CrashSeverity::HardImpact:
      return crash.hard_impact_damage;
    case entity::CrashSeverity::Ejection:
      return crash.ejection_damage;
    case entity::CrashSeverity::Destruction:
      return crash.destruction_damage;
    case entity::CrashSeverity::None:
      break;
  }
  return 0.0F;
}

[[nodiscard]] bool crash_should_explode(
    const entity::VehicleCrashDefinition& crash,
    const VehicleCrashAssessment& assessment) noexcept {
  if (!crash.explode_on_destruction ||
      assessment.severity != entity::CrashSeverity::Destruction) {
    return false;
  }
  return crash.explosion_threshold <= 0.0F ||
         assessment.normal_impact_speed >= crash.explosion_threshold;
}

[[nodiscard]] VehicleCrashDebugInfo make_vehicle_crash_debug_info(
    EntityNetID vehicle_net_id,
    const entity::VehicleDefinition& definition,
    const protocol::ServerboundVehicleMove& move,
    const VehicleMovementValidationContext& context,
    const VehicleMovementValidationResult& validation,
    const VehicleCrashAssessment& assessment,
    const AcceptedVehicleMovementState& accepted) {
  VehicleCrashDebugInfo debug;
  debug.vehicle_id = vehicle_net_id;
  debug.definition = definition.name;
  debug.server_tick = context.server_tick;
  debug.sequence = move.sequence;
  debug.accepted_move = validation.accepted;
  debug.sent_correction = validation.should_send_correction;
  debug.validation_reason = validation.reason;
  debug.final_aabb_overlaps_solid = context.final_aabb_overlaps_solid;
  debug.proposed_sweep_hit_solid = context.proposed_sweep_hit_solid;
  debug.previous_velocity_sweep_hit_solid =
      context.previous_velocity_sweep_hit_solid;
  debug.had_collision_contact = context.has_collision_contact;
  debug.packet_velocity = move.velocity;
  debug.accepted_velocity = accepted.velocity;
  debug.severity = assessment.severity;
  debug.normal_impact_speed = assessment.normal_impact_speed;
  debug.direct_impact_dot = assessment.direct_impact_dot;
  if (context.has_collision_contact) {
    debug.contact_resolved_safe = context.collision_contact.resolved_safe;
    debug.contact_normal = context.collision_contact.normal;
    debug.pre_impact_velocity = context.collision_contact.pre_impact_velocity;
  }
  return debug;
}

[[nodiscard]] Vec3f vehicle_effect_position(
    const world::WorldRuntime& world,
    EntityID vehicle_entity) noexcept {
  const entity::EntityTransform* transform =
      world.find_entity_transform(vehicle_entity);
  if (transform == nullptr) {
    return {};
  }
  const entity::EntityCollision* collision =
      world.find_entity_collision(vehicle_entity);
  if (collision == nullptr) {
    return transform->position;
  }
  return transform->position + Vec3f{0.0F, collision->box.y * 0.5F, 0.0F};
}

[[nodiscard]] std::vector<EntityID> vehicle_occupants(
    const world::WorldRuntime& world,
    EntityID vehicle_entity) {
  std::vector<EntityID> occupants;
  const entity::RideableComponent* rideable =
      world.find_entity_rideable(vehicle_entity);
  if (rideable == nullptr) {
    return occupants;
  }
  occupants.reserve(rideable->seats.size());
  for (const entity::Seat& seat : rideable->seats) {
    if (seat.occupant.value != 0U) {
      occupants.push_back(seat.occupant);
    }
  }
  return occupants;
}

[[nodiscard]] std::optional<Vec3f> crash_ejection_position(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    EntityID vehicle_entity,
    const player::PlayerTuning& tuning) noexcept {
  std::optional<Vec3f> position =
      world.find_safe_dismount_position(
          vehicle_entity,
          block_registry,
          tuning);
  if (position.has_value()) {
    return position;
  }
  return fallback_rideable_dismount_position(
      world,
      vehicle_entity,
      tuning);
}

[[nodiscard]] Vec3f crash_ejection_velocity(
    const entity::VehicleCrashDefinition& crash,
    const VehicleCrashContact& contact) noexcept {
  Vec3f launch = contact.pre_impact_velocity * crash.ejection_velocity_scale;
  Vec3f horizontal{launch.x, 0.0F, launch.z};
  f32 horizontal_speed = length(horizontal);
  Vec3f direction = horizontal_speed > 0.001F
                       ? horizontal * (1.0F / horizontal_speed)
                       : normalize_or_zero(Vec3f{-contact.normal.x, 0.0F, -contact.normal.z});
  if (horizontal_speed > crash.ejection_max_horizontal_speed &&
      crash.ejection_max_horizontal_speed >= 0.0F) {
    horizontal_speed = crash.ejection_max_horizontal_speed;
  }
  if (horizontal_speed < crash.ejection_min_horizontal_speed) {
    horizontal_speed = crash.ejection_min_horizontal_speed;
  }
  horizontal = direction * horizontal_speed;
  launch.x = horizontal.x;
  launch.z = horizontal.z;
  launch.y = std::max(launch.y + crash.ejection_upward_velocity, crash.ejection_upward_velocity);
  if (crash.ejection_max_vertical_speed > 0.0F) {
    launch.y = std::clamp(
        launch.y,
        -crash.ejection_max_vertical_speed,
        crash.ejection_max_vertical_speed);
  }
  return launch;
}

[[nodiscard]] VehicleDamageStage vehicle_damage_stage(
    const entity::EntityHealth* health) noexcept {
  if (health == nullptr || !std::isfinite(health->max) || health->max <= 0.0F ||
      !std::isfinite(health->current)) {
    return VehicleDamageStage::Healthy;
  }
  if (health->current <= 0.0F) {
    return VehicleDamageStage::Destroyed;
  }
  const f32 ratio = health->current / health->max;
  if (ratio <= 0.25F) {
    return VehicleDamageStage::Critical;
  }
  if (ratio <= 0.5F) {
    return VehicleDamageStage::Damaged;
  }
  return VehicleDamageStage::Healthy;
}

[[nodiscard]] TickIndex vehicle_smoke_interval_ticks(
    VehicleDamageStage stage) noexcept {
  return stage == VehicleDamageStage::Critical ? 15U : 45U;
}

} // namespace

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload) {
  if (transport.state(connection_id) == net::ConnectionState::Disconnected) {
    return net::NetworkResult::success();
  }
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  return transport.send(
      {connection_id,
       reliability,
       std::move(packet),
       message_type,
       lane});
}

void log_send_failure_if_dedicated(
    ServerMode mode,
    net::ConnectionID connection_id,
    const net::NetworkResult& send_result) {
  if (send_result || mode != ServerMode::Dedicated) {
    return;
  }

  core::warn(
      "dedicated",
      "network send failed: connection={} reason='{}'",
      connection_id.value,
      send_result.error().message);
}

[[nodiscard]] std::string format_text_variable_number(f32 value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << value;
  std::string text = out.str();
  while (text.size() > 1U && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text;
}

plugin::PluginNetworkSendResult ServerRuntimeImpl::send_plugin_network_packet(
    const plugin::PluginNetworkSendRequest& request) {
  auto failure = [](
                     ArvResultCode code,
                     std::string message) -> plugin::PluginNetworkSendResult {
    return plugin::PluginNetworkSendResult::failure(
        {code, std::move(message)});
  };

  if (state_ != ServerRuntimeState::Running || !transport_) {
    return failure(ARV_RESULT_INVALID_STATE, "server network is not running");
  }
  if (request.packet == nullptr) {
    return failure(ARV_RESULT_NOT_FOUND, "packet handle not found");
  }

  const plugin::PluginNetworkPacket& packet = *request.packet;
  switch (packet.message_type) {
    case protocol::MessageType::ClientboundChatMessage:
    case protocol::MessageType::ClientboundKickPlayer:
      break;
    default:
      return failure(ARV_RESULT_UNSUPPORTED, "packet type is not exposed to plugins");
  }

  std::vector<u8> payload;
  bool payload_prepared{};
  auto prepare_payload = [&]() -> plugin::PluginNetworkSendResult {
    if (payload_prepared) {
      return plugin::PluginNetworkSendResult::success();
    }
    payload = packet.payload;
    if (packet.message_type == protocol::MessageType::ClientboundChatMessage) {
      auto chat = protocol::deserialize_clientbound_chat_message(payload);
      if (!chat) {
        return failure(ARV_RESULT_INVALID_ARGUMENT, "chat packet payload is invalid");
      }
      chat.value().message_id = next_chat_message_id_++;
      payload = protocol::serialize_clientbound_chat_message(chat.value());
    }
    payload_prepared = true;
    return plugin::PluginNetworkSendResult::success();
  };

  auto session_for_player = [this](PlayerID player_id) -> const ServerSession* {
    const ServerSession* match = nullptr;
    sessions_.for_each_session([&](const ServerSession& session) {
      if (session.player_id == player_id) {
        match = &session;
      }
    });
    return match;
  };

  auto validate_phase = [&](const ServerSession& session)
      -> plugin::PluginNetworkSendResult {
    if (session.closing ||
        session.connection_state == net::ConnectionState::Disconnected ||
        session.connection_state == net::ConnectionState::Disconnecting) {
      return failure(ARV_RESULT_INVALID_STATE, "target player connection is closing");
    }
    if (packet.message_type == protocol::MessageType::ClientboundChatMessage &&
        (!session.authenticated ||
         session.connection_state != net::ConnectionState::InGame)) {
      return failure(
          ARV_RESULT_INVALID_STATE,
          "chat packets require an in-game target player");
    }
    return plugin::PluginNetworkSendResult::success();
  };

  auto send_to_session = [&](const ServerSession& session)
      -> plugin::PluginNetworkSendResult {
    plugin::PluginNetworkSendResult phase = validate_phase(session);
    if (!phase) {
      return phase;
    }
    plugin::PluginNetworkSendResult prepared = prepare_payload();
    if (!prepared) {
      return prepared;
    }
    net::NetworkResult send_result = send_protocol_message(
        *transport_,
        session.connection_id,
        packet.message_type,
        packet.reliability,
        packet.lane,
        payload);
    log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
    if (!send_result) {
      return failure(ARV_RESULT_INTERNAL_ERROR, send_result.error().message);
    }
    return plugin::PluginNetworkSendResult::success();
  };

  switch (request.target_kind) {
    case plugin::PluginNetworkTargetKind::Player: {
      const ServerSession* session = session_for_player(request.player);
      if (session == nullptr) {
        return failure(ARV_RESULT_NOT_FOUND, "target player is not connected");
      }
      return send_to_session(*session);
    }
    case plugin::PluginNetworkTargetKind::Players:
      for (PlayerID player_id : request.players) {
        const ServerSession* session = session_for_player(player_id);
        if (session == nullptr) {
          return failure(ARV_RESULT_NOT_FOUND, "target player is not connected");
        }
        plugin::PluginNetworkSendResult result = send_to_session(*session);
        if (!result) {
          return result;
        }
      }
      return plugin::PluginNetworkSendResult::success();
    case plugin::PluginNetworkTargetKind::Broadcast: {
      plugin::PluginNetworkSendResult last_result =
          plugin::PluginNetworkSendResult::success();
      sessions_.for_each_session([&](const ServerSession& session) {
        if (!last_result) {
          return;
        }
        plugin::PluginNetworkSendResult phase = validate_phase(session);
        if (!phase) {
          return;
        }
        last_result = send_to_session(session);
      });
      return last_result;
    }
    case plugin::PluginNetworkTargetKind::World: {
      plugin::PluginNetworkSendResult last_result =
          plugin::PluginNetworkSendResult::success();
      sessions_.for_each_session([&](const ServerSession& session) {
        if (!last_result || session.world_id != request.world) {
          return;
        }
        plugin::PluginNetworkSendResult phase = validate_phase(session);
        if (!phase) {
          return;
        }
        last_result = send_to_session(session);
      });
      return last_result;
    }
  }
  return failure(ARV_RESULT_INVALID_ARGUMENT, "invalid network target kind");
}

plugin::PluginNetworkSendResult ServerRuntimeImpl::send_plugin_chat_message(
    const plugin::PluginChatMessageSendRequest& request) {
  auto failure = [](
                     ArvResultCode code,
                     std::string message) -> plugin::PluginNetworkSendResult {
    return plugin::PluginNetworkSendResult::failure(
        {code, std::move(message)});
  };

  if (state_ != ServerRuntimeState::Running || !transport_) {
    return failure(ARV_RESULT_INVALID_STATE, "server network is not running");
  }
  if (request.message == nullptr) {
    return failure(ARV_RESULT_NOT_FOUND, "chat message handle not found");
  }
  const std::string channel =
      request.channel.empty() ? std::string{protocol::ChatChannelSystem}
                              : request.channel;
  if (channel.size() > protocol::MaxChatChannelBytes) {
    return failure(ARV_RESULT_INVALID_ARGUMENT, "chat channel is too long");
  }

  auto session_for_player = [this](PlayerID player_id) -> const ServerSession* {
    const ServerSession* match = nullptr;
    sessions_.for_each_session([&](const ServerSession& session) {
      if (session.player_id == player_id) {
        match = &session;
      }
    });
    return match;
  };

  auto validate_phase = [&](const ServerSession& session)
      -> plugin::PluginNetworkSendResult {
    if (session.closing ||
        session.connection_state == net::ConnectionState::Disconnected ||
        session.connection_state == net::ConnectionState::Disconnecting) {
      return failure(ARV_RESULT_INVALID_STATE, "target player connection is closing");
    }
    if (!session.authenticated ||
        session.connection_state != net::ConnectionState::InGame) {
      return failure(
          ARV_RESULT_INVALID_STATE,
          "chat messages require an in-game target player");
    }
    return plugin::PluginNetworkSendResult::success();
  };

  auto send_to_session = [&](const ServerSession& session)
      -> plugin::PluginNetworkSendResult {
    plugin::PluginNetworkSendResult phase = validate_phase(session);
    if (!phase) {
      return phase;
    }
    const arv::text::TextVariableResolver resolver{
        &text_variables_,
        session.player_id,
        arv::text::TextVariableResolution::ResolvedOnly};
    arv::text::RichText rich = request.message->to_rich_text(resolver);
    const arv::text::RichTextValidation validation =
        arv::text::validate_rich_text(rich);
    if (!validation.valid) {
      return failure(ARV_RESULT_INVALID_ARGUMENT, validation.message);
    }
    const protocol::ClientboundChatMessage packet{
        next_chat_message_id_++,
        channel,
        request.kind,
        std::move(rich)};
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        session.connection_id,
        protocol::MessageType::ClientboundChatMessage,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_chat_message(packet));
    log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
    if (!send_result) {
      return failure(ARV_RESULT_INTERNAL_ERROR, send_result.error().message);
    }
    return plugin::PluginNetworkSendResult::success();
  };

  switch (request.target_kind) {
    case plugin::PluginNetworkTargetKind::Player: {
      const ServerSession* session = session_for_player(request.player);
      if (session == nullptr) {
        return failure(ARV_RESULT_NOT_FOUND, "target player is not connected");
      }
      return send_to_session(*session);
    }
    case plugin::PluginNetworkTargetKind::Players:
      for (PlayerID player_id : request.players) {
        const ServerSession* session = session_for_player(player_id);
        if (session == nullptr) {
          return failure(ARV_RESULT_NOT_FOUND, "target player is not connected");
        }
        plugin::PluginNetworkSendResult result = send_to_session(*session);
        if (!result) {
          return result;
        }
      }
      return plugin::PluginNetworkSendResult::success();
    case plugin::PluginNetworkTargetKind::Broadcast: {
      plugin::PluginNetworkSendResult last_result =
          plugin::PluginNetworkSendResult::success();
      sessions_.for_each_session([&](const ServerSession& session) {
        if (!last_result || !session.authenticated ||
            session.connection_state != net::ConnectionState::InGame ||
            session.closing) {
          return;
        }
        last_result = send_to_session(session);
      });
      return last_result;
    }
    case plugin::PluginNetworkTargetKind::World: {
      plugin::PluginNetworkSendResult last_result =
          plugin::PluginNetworkSendResult::success();
      sessions_.for_each_session([&](const ServerSession& session) {
        if (!last_result || session.world_id != request.world ||
            !session.authenticated ||
            session.connection_state != net::ConnectionState::InGame ||
            session.closing) {
          return;
        }
        last_result = send_to_session(session);
      });
      return last_result;
    }
  }
  return failure(ARV_RESULT_INVALID_ARGUMENT, "invalid network target kind");
}

void ServerRuntimeImpl::refresh_builtin_text_variables() {
  usize online_players = 0U;
  sessions_.for_each_session([&](const ServerSession& session) {
    if (session.authenticated && !session.closing) {
      ++online_players;
    }
  });
  text_variables_.set_global("online_player_count", std::to_string(online_players));
  text_variables_.set_global("tps", std::to_string(base_tps_));
  text_variables_.set_global("world_name", world_name_);

  sessions_.for_each_session([&](const ServerSession& session) {
    if (!session.authenticated || session.closing) {
      return;
    }
    const std::string display_name =
        session.display_name.empty() ? session.profile_name : session.display_name;
    text_variables_.set_player(session.player_id, "player_name", display_name);
    const std::optional<EntityID> entity_id =
        default_world_.entity_for_player(session.player_id);
    const entity::EntityHealth* health =
        entity_id.has_value() ? default_world_.find_entity_health(entity_id.value()) : nullptr;
    if (health == nullptr) {
      (void)text_variables_.clear_player_value(session.player_id, "player_health");
      (void)text_variables_.clear_player_value(session.player_id, "player_max_health");
      return;
    }
    text_variables_.set_player(
        session.player_id,
        "player_health",
        format_text_variable_number(health->current));
    text_variables_.set_player(
        session.player_id,
        "player_max_health",
        format_text_variable_number(health->max));
  });
}

void ServerRuntimeImpl::send_text_variable_updates() {
  if (!transport_) {
    return;
  }
  refresh_builtin_text_variables();

  protocol::ClientboundTextVariableUpdate globals;
  globals.reset = true;
  globals.entries.reserve(text_variables_.globals().size());
  for (const auto& [name, variable] : text_variables_.globals()) {
    if (variable.binding != arv::text::TextVariableBinding::Live) {
      continue;
    }
    globals.entries.push_back({
        protocol::TextVariableScope::Global,
        name,
        variable.value,
        protocol::TextVariableBinding::Live,
    });
  }

  sessions_.for_each_session([&](const ServerSession& recipient) {
    if (!recipient.authenticated || recipient.closing ||
        recipient.connection_state != net::ConnectionState::InGame) {
      return;
    }
    protocol::ClientboundTextVariableUpdate update = globals;
    if (const arv::text::TextVariableMap* player_values =
            text_variables_.player_values(recipient.player_id);
        player_values != nullptr) {
      update.entries.reserve(update.entries.size() + player_values->size());
      for (const auto& [name, variable] : *player_values) {
        if (variable.binding != arv::text::TextVariableBinding::Live) {
          continue;
        }
        update.entries.push_back({
            protocol::TextVariableScope::Player,
            name,
            variable.value,
            protocol::TextVariableBinding::Live,
        });
      }
    }
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        recipient.connection_id,
        protocol::MessageType::ClientboundTextVariableUpdate,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        protocol::serialize_clientbound_text_variable_update(update));
    log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  });
}

void ServerRuntimeImpl::expire_timed_out_skin_uploads() {
  for (auto& [session_id, transfer] : skin_transfers_) {
    (void)session_id;
    if (transfer.active_upload.has_value() &&
        skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
      clear_active_skin_upload(transfer, active_skin_uploads_global_);
    }
  }
}

void ServerRuntimeImpl::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      handle_connection_lost(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      if (mode_ == ServerMode::Dedicated) {
        core::warn(
            "dedicated",
            "network send failed: connection={} reason='{}'",
            event.connection_id.value,
            event.reason);
      }
      return;
  }
}

void ServerRuntimeImpl::handle_message(
    net::ConnectionID connection_id,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ServerboundHello: {
      auto client_hello = protocol::deserialize_serverbound_hello(decoded.value().payload);
      if (!client_hello) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_client_hello(connection_id, client_hello.value());
      return;
    }
    case protocol::MessageType::ServerboundAuthResponse: {
      auto response = protocol::deserialize_auth_response(decoded.value().payload);
      if (!response) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_auth_response(connection_id, response.value());
      return;
    }
    case protocol::MessageType::ServerboundKeepAlive: {
      auto keep_alive = protocol::deserialize_serverbound_keep_alive(decoded.value().payload);
      if (!keep_alive) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_keep_alive(connection_id, keep_alive.value());
      return;
    }
    case protocol::MessageType::ServerboundPlayerMove: {
      auto move = protocol::deserialize_serverbound_player_move(decoded.value().payload);
      if (!move) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_player_move(connection_id, move.value());
      return;
    }
    case protocol::MessageType::ServerboundVehicleMove: {
      auto move = protocol::deserialize_serverbound_vehicle_move(decoded.value().payload);
      if (!move) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_vehicle_move(connection_id, move.value());
      return;
    }
    case protocol::MessageType::ServerboundRideEntityRequest: {
      auto request = protocol::deserialize_serverbound_ride_entity_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_ride_entity_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundDismountRequest: {
      auto request = protocol::deserialize_serverbound_dismount_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_dismount_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundVehicleAction: {
      auto action = protocol::deserialize_serverbound_vehicle_action(decoded.value().payload);
      if (!action) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_vehicle_action(connection_id, action.value());
      return;
    }
    case protocol::MessageType::ServerboundChunkInterestUpdate: {
      auto interest = protocol::deserialize_serverbound_chunk_interest_update(decoded.value().payload);
      if (!interest) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_chunk_interest_update(connection_id, interest.value());
      return;
    }
    case protocol::MessageType::ServerboundInventoryActionRequest: {
      auto request = protocol::deserialize_serverbound_inventory_action_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_inventory_action_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundCraftRecipe: {
      auto request = protocol::deserialize_serverbound_craft_recipe(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_craft_recipe(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundOpenContainerBlock: {
      auto request = protocol::deserialize_serverbound_open_container_block(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_open_container_block(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundContainerAction: {
      auto action = protocol::deserialize_serverbound_container_action(decoded.value().payload);
      if (!action) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_container_action(connection_id, action.value());
      return;
    }
    case protocol::MessageType::ServerboundCloseContainer: {
      auto request = protocol::deserialize_serverbound_close_container(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_close_container(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundChatMessage: {
      auto chat = protocol::deserialize_serverbound_chat_message(decoded.value().payload);
      if (!chat) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_chat_message(connection_id, std::move(chat.value()));
      return;
    }
    case protocol::MessageType::ServerboundAttackEntity: {
      auto attack = protocol::deserialize_serverbound_attack_entity(decoded.value().payload);
      if (!attack) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_attack_entity(connection_id, attack.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinRequest: {
      auto request = protocol::deserialize_skin_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinRequestBatch: {
      auto batch = protocol::deserialize_skin_request_batch(decoded.value().payload);
      if (!batch) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_request_batch(connection_id, std::move(batch.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadBegin: {
      auto begin = protocol::deserialize_skin_upload_begin(decoded.value().payload);
      if (!begin) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_upload_begin(connection_id, begin.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadData: {
      auto data = protocol::deserialize_skin_upload_data(decoded.value().payload);
      if (!data) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_upload_data(connection_id, std::move(data.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadChunk: {
      auto chunk = protocol::deserialize_skin_upload_chunk(decoded.value().payload);
      if (!chunk) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_upload_chunk(connection_id, std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadEnd: {
      auto end = protocol::deserialize_skin_upload_end(decoded.value().payload);
      if (!end) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_skin_upload_end(connection_id, end.value());
      return;
    }
    case protocol::MessageType::ServerboundPackSyncStatus: {
      auto status = protocol::deserialize_serverbound_pack_sync_status(decoded.value().payload);
      if (!status) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_pack_sync_status(connection_id, std::move(status.value()));
      return;
    }
    case protocol::MessageType::ServerboundPackRequest: {
      auto request = protocol::deserialize_serverbound_pack_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_pack_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundPackReady: {
      auto ready = protocol::deserialize_serverbound_pack_ready(decoded.value().payload);
      if (!ready) {
        kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.invalid_protocol_packet", "Invalid protocol packet."));
        return;
      }
      handle_pack_ready(connection_id, std::move(ready.value()));
      return;
    }
    default:
      return;
  }
}

void ServerRuntimeImpl::handle_client_hello(
    net::ConnectionID connection_id,
    const protocol::ServerboundHello& client_hello) {
  protocol::HandshakePolicy policy = handshake_policy_;
  if (!client_hello.auth_begin.has_value()) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.auth_required", "authentication required"));
    return;
  }

  const protocol::ServerboundAuthBegin& auth_begin = client_hello.auth_begin.value();
  PlayerProfileOpenResult profile_result = profiles_.open_or_create(
      auth_begin.public_key,
      auth_begin.display_name,
      auth_begin.claimed_profile_id);
  if (profile_result.status == PlayerProfileOpenStatus::InvalidClaimedProfileID ||
      profile_result.status == PlayerProfileOpenStatus::PublicKeyMismatch ||
      profile_result.profile == nullptr) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.auth_failed", "authentication failed"));
    return;
  }

  SessionProfileIdentity profile_identity;
  profile_identity.profile_id = profile_result.profile->profile_id;
  profile_identity.display_name = profile_result.profile->display_name;
  profile_identity.public_key = profile_result.profile->public_key;
  profile_identity.has_public_key = true;

  if (ServerSession* existing = sessions_.find_by_connection(connection_id); existing != nullptr) {
    policy.assigned_session_id = existing->session_id;
    policy.assigned_player_id = existing->player_id;
    policy.initial_world_id = existing->world_id;
  } else {
    const ReservedSessionIdentity identity = sessions_.reserve_identity();
    policy.assigned_session_id = identity.session_id;
    policy.assigned_player_id = identity.player_id;
    policy.initial_world_id = DefaultWorldID;
  }

  protocol::ClientboundHello server_hello = protocol::make_clientbound_hello(client_hello, policy);
  if (server_hello.accepted && pack_sync_.enabled) {
    server_hello.required_followup_sync = true;
  }
  if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
    ServerSession& session = sessions_.create(
        connection_id,
        {server_hello.assigned_session_id, server_hello.assigned_player_id},
        server_hello.initial_world_id,
        std::move(profile_identity),
        mode_ == ServerMode::Integrated);
    if (mode_ == ServerMode::Integrated) {
      if (!complete_authenticated_session(session)) {
        return;
      }
    }
  }

  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_hello(server_hello));
  if (server_hello.accepted) {
    if (ServerSession* session = sessions_.find_by_connection(connection_id);
        session != nullptr) {
      if (mode_ == ServerMode::Integrated) {
        send_auth_result(*session, true);
        begin_authenticated_session_join(*session);
      } else if (!session->authenticated) {
        send_auth_challenge(*session);
      }
    }
  }
}

void ServerRuntimeImpl::handle_auth_response(
    net::ConnectionID connection_id,
    const protocol::ServerboundAuthResponse& response) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || session->authenticated || !session->challenge.has_value() ||
      !session->has_public_key) {
    return;
  }

  const PendingAuthChallenge challenge = session->challenge.value();
  session->challenge.reset();
  if (tick_index_ > challenge.expires_server_tick) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.auth_failed", "authentication failed"));
    return;
  }

  const std::vector<u8> payload_bytes = protocol::build_auth_signed_payload(
      {protocol::CurrentProtocolVersion,
       challenge.server_instance_id,
       session->session_id,
       session->profile_id,
       session->public_key,
       challenge.nonce,
       challenge.expires_server_tick});
  const auto* first = reinterpret_cast<const std::byte*>(payload_bytes.data());
  const bool verified = crypto::verify_ed25519(
      std::span<const std::byte>{first, payload_bytes.size()},
      response.signature,
      session->public_key);
  if (!verified) {
    kick_client(connection_id, arv::text::text_or_fallback(language_, "server.disconnect.auth_failed", "authentication failed"));
    return;
  }

  if (!complete_authenticated_session(*session)) {
    return;
  }
  send_auth_result(*session, true);
  begin_authenticated_session_join(*session);
}

void ServerRuntimeImpl::handle_keep_alive(
    net::ConnectionID connection_id,
    const protocol::ServerboundKeepAlive& keep_alive) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || session->closing) {
    return;
  }
  if (keep_alive.id != session->last_keep_alive_id) {
    return;
  }
  session->last_keep_alive_received_time = std::chrono::steady_clock::now();
}

void ServerRuntimeImpl::handle_player_move(
    net::ConnectionID connection_id,
    const protocol::ServerboundPlayerMove& move) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr || block_registry_ == nullptr) {
    return;
  }
  if (const std::optional<EntityID> entity_id =
          default_world_.entity_for_player(session->player_id);
      entity_id.has_value() &&
      default_world_.find_entity_attachment(entity_id.value()) != nullptr) {
    return;
  }

  refresh_player_permissions(*player_state);
  AcceptedPlayerMovementState& previous = accepted_player_movement_[session->player_id];
  const double receive_time_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(base_tps_, 1U));
  auto sync_previous_to_player_state = [&]() {
    previous.position = player_state->position;
    previous.velocity = player_state->velocity;
    previous.yaw_radians = player_state->yaw_radians;
    previous.pitch_radians = player_state->pitch_radians;
    previous.flags = player_move_flags_from_state(*player_state);
    previous.last_server_tick = tick_index_;
    previous.last_receive_time_seconds = receive_time_seconds;
    previous.has_accepted_move = true;
  };
  if (!previous.has_accepted_move) {
    sync_previous_to_player_state();
  } else if (length(player_state->position - previous.position) >
             movement_.base_position_epsilon_blocks) {
    sync_previous_to_player_state();
  }

  const player::PlayerCommand command = action_command_from_move(move);
  player::PlayerCommand& pending = pending_player_commands_[session->player_id];
  if (command.client_tick >= player_state->last_processed_input_tick &&
      command.client_tick >= pending.client_tick) {
    pending = command;
  }

  const MovementValidationContext context = make_movement_validation_context(
      default_world_,
      *block_registry_,
      *player_state,
      move,
      tick_index_,
      base_tps_,
      player_tuning_);
  const MovementValidationResult validation =
      validate_serverbound_player_move(move, previous, context, movement_);
  if (!validation.accepted) {
    if (validation.should_send_correction) {
      send_player_move_correction(*session, *player_state, move.sequence, validation.reason);
    }
    return;
  }

  const u32 consumed_knockback_suppression_ticks =
      knockback_suppression_ticks_to_consume(
          previous,
          tick_index_,
          player_state->knockback_control_suppression_ticks);
  previous = validation.accepted_state;
  player_state->position = validation.accepted_state.position;
  player_state->velocity = validation.accepted_state.velocity;
  player_state->yaw_radians = validation.accepted_state.yaw_radians;
  player_state->pitch_radians = validation.accepted_state.pitch_radians;
  player_state->grounded =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::OnGround);
  player_state->sneaking =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sneaking);
  player_state->swimming =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Swimming);
  player_state->movement_mode =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Flying)
          ? player::MovementMode::Flying
          : player::MovementMode::Walking;
  const u32 remaining_knockback_suppression_ticks =
      player_state->knockback_control_suppression_ticks;
  player_state->knockback_control_suppression_ticks =
      remaining_knockback_suppression_ticks > consumed_knockback_suppression_ticks
          ? remaining_knockback_suppression_ticks -
                consumed_knockback_suppression_ticks
          : 0U;
}

void ServerRuntimeImpl::handle_ride_entity_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundRideEntityRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const std::optional<EntityID> passenger_entity =
      default_world_.entity_for_player(session->player_id);
  const std::optional<EntityID> rideable_entity =
      default_world_.entity_for_network_id(request.rideable_id);
  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (!passenger_entity.has_value() || !rideable_entity.has_value() ||
      player_state == nullptr) {
    return;
  }
  const entity::EntityTransform* passenger_transform =
      default_world_.find_entity_transform(passenger_entity.value());
  const entity::EntityTransform* rideable_transform =
      default_world_.find_entity_transform(rideable_entity.value());
  if (passenger_transform == nullptr || rideable_transform == nullptr ||
      default_world_.find_entity_rideable(rideable_entity.value()) == nullptr) {
    return;
  }
  const f32 max_mount_distance =
      player_tuning_.reach_distance + player_tuning_.body_width + 2.0F;
  if (distance(passenger_transform->position, rideable_transform->position) >
      max_mount_distance) {
    return;
  }
  if (const auto cooldown = crash_remount_cooldowns_.find(session->player_id);
      cooldown != crash_remount_cooldowns_.end()) {
    if (tick_index_ < cooldown->second) {
      return;
    }
    crash_remount_cooldowns_.erase(cooldown);
  }

  std::optional<std::string_view> requested_seat;
  if (!request.seat_id.empty()) {
    requested_seat = request.seat_id;
  }
  if (!default_world_.mount_entity(
          rideable_entity.value(),
          passenger_entity.value(),
          requested_seat)) {
    return;
  }

  if (const entity::EntityTransform* mounted_transform =
          default_world_.find_entity_transform(passenger_entity.value());
      mounted_transform != nullptr) {
    player_state->position = mounted_transform->position;
    player_state->velocity = {};
  }

  const std::optional<std::string> seat_id =
      default_world_.mounted_seat(passenger_entity.value());
  const entity::RideableComponent* rideable =
      default_world_.find_entity_rideable(rideable_entity.value());
  const bool controls_vehicle =
      rideable != nullptr && rideable->controller == passenger_entity.value();
  if (const std::optional<EntityNetID> rideable_net =
          default_world_.network_id_for_entity(rideable_entity.value());
      rideable_net.has_value() &&
      accepted_vehicle_movement_.find(rideable_net.value()) ==
          accepted_vehicle_movement_.end()) {
    accepted_vehicle_movement_[rideable_net.value()] =
        accepted_vehicle_state_from_world(
            default_world_,
            rideable_entity.value(),
            rideable_net.value(),
            tick_index_,
            base_tps_);
  }
  broadcast_ride_started(
      passenger_entity.value(),
      rideable_entity.value(),
      seat_id.value_or(std::string{}),
      controls_vehicle);
}

void ServerRuntimeImpl::handle_dismount_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundDismountRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const std::optional<EntityID> passenger_entity =
      default_world_.entity_for_player(session->player_id);
  if (!passenger_entity.has_value()) {
    return;
  }
  const std::optional<EntityID> rideable_entity =
      default_world_.mounted_rideable(passenger_entity.value());
  if (!rideable_entity.has_value()) {
    return;
  }
  const std::optional<EntityID> requested_rideable =
      default_world_.entity_for_network_id(request.rideable_id);
  if (requested_rideable.has_value() && requested_rideable.value() != rideable_entity.value()) {
    return;
  }

  std::optional<Vec3f> dismount_position;
  if (block_registry_ != nullptr) {
    dismount_position =
        default_world_.find_safe_dismount_position(
            rideable_entity.value(),
            *block_registry_,
            player_tuning_);
  }
  if (!dismount_position.has_value()) {
    dismount_position =
        fallback_rideable_dismount_position(
            default_world_,
            rideable_entity.value(),
            player_tuning_);
  }
  if (!dismount_position.has_value()) {
    return;
  }

  if (!default_world_.dismount_entity(passenger_entity.value())) {
    return;
  }
  if (const std::optional<EntityNetID> rideable_net =
          default_world_.network_id_for_entity(rideable_entity.value());
      rideable_net.has_value()) {
    accepted_vehicle_movement_.erase(rideable_net.value());
  }
  (void)default_world_.set_entity_position(passenger_entity.value(), dismount_position.value());
  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state != nullptr) {
    player_state->position = dismount_position.value();
    player_state->velocity = {};
    player_state->grounded = false;
  }
  broadcast_ride_ended(
      passenger_entity.value(),
      rideable_entity.value(),
      dismount_position.value());
}

void ServerRuntimeImpl::handle_vehicle_action(
    net::ConnectionID connection_id,
    const protocol::ServerboundVehicleAction& action) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  (void)action;
}

void ServerRuntimeImpl::tick_vehicle_damage_effects() {
  for (auto it = vehicle_smoke_states_.begin();
       it != vehicle_smoke_states_.end();) {
    const std::optional<EntityID> entity =
        default_world_.entity_for_network_id(it->first);
    if (!entity.has_value() || !default_world_.entity_exists(entity.value()) ||
        default_world_.find_entity_vehicle_physics(entity.value()) == nullptr) {
      const auto stale = it++;
      vehicle_smoke_states_.erase(stale);
      continue;
    }
    const VehicleDamageStage stage =
        vehicle_damage_stage(default_world_.find_entity_health(entity.value()));
    if (stage == VehicleDamageStage::Healthy ||
        stage == VehicleDamageStage::Destroyed) {
      const auto stale = it++;
      vehicle_smoke_states_.erase(stale);
      continue;
    }
    ++it;
  }

  for (const EntityID entity : default_world_.entity_ids()) {
    if (default_world_.find_entity_vehicle_physics(entity) == nullptr) {
      continue;
    }
    const std::optional<EntityNetID> net_id =
        default_world_.network_id_for_entity(entity);
    if (!net_id.has_value()) {
      continue;
    }
    const VehicleDamageStage stage =
        vehicle_damage_stage(default_world_.find_entity_health(entity));
    if (stage == VehicleDamageStage::Healthy ||
        stage == VehicleDamageStage::Destroyed) {
      vehicle_smoke_states_.erase(net_id.value());
      continue;
    }

    VehicleSmokeState& state = vehicle_smoke_states_[net_id.value()];
    if (state.stage != stage) {
      state.stage = stage;
      state.next_smoke_tick = tick_index_;
    }
    if (tick_index_ < state.next_smoke_tick) {
      continue;
    }
    broadcast_vehicle_smoke_particles(
        vehicle_effect_position(default_world_, entity) + Vec3f{0.0F, 0.08F, 0.0F},
        stage,
        static_cast<u32>(
            net_id->value ^
            (static_cast<u64>(tick_index_) << 8U) ^
            (stage == VehicleDamageStage::Critical ? 0x9e3779b9ULL : 0x85ebca6bULL)));
    state.stage = stage;
    state.next_smoke_tick =
        tick_index_ + vehicle_smoke_interval_ticks(stage);
  }
}

bool ServerRuntimeImpl::destroy_vehicle_from_damage(
    EntityID vehicle_entity,
    Vec3f normal,
    u32 seed,
    bool broadcast_explosion) {
  if (!default_world_.entity_exists(vehicle_entity)) {
    return false;
  }
  const std::optional<EntityNetID> vehicle_net_id =
      default_world_.network_id_for_entity(vehicle_entity);
  const Vec3f effect_position =
      vehicle_effect_position(default_world_, vehicle_entity);

  if (block_registry_ != nullptr) {
    const std::vector<EntityID> occupants =
        vehicle_occupants(default_world_, vehicle_entity);
    const std::optional<Vec3f> ejection_position =
        crash_ejection_position(
            default_world_,
            *block_registry_,
            vehicle_entity,
            player_tuning_);
    if (ejection_position.has_value()) {
      bool dismounted_any = false;
      for (const EntityID occupant : occupants) {
        const std::optional<PlayerID> player_id =
            default_world_.player_for_entity(occupant);
        if (!default_world_.dismount_entity(occupant)) {
          continue;
        }
        dismounted_any = true;
        (void)default_world_.set_entity_position(
            occupant,
            ejection_position.value());
        if (entity::EntityVelocity* velocity =
                default_world_.find_entity_velocity(occupant);
            velocity != nullptr) {
          velocity->linear = {0.0F, 3.0F, 0.0F};
        }
        if (player_id.has_value()) {
          player::PlayerState* player_state =
              default_world_.find_player(player_id.value());
          if (player_state != nullptr) {
            player_state->position = ejection_position.value();
            player_state->velocity = {0.0F, 3.0F, 0.0F};
            player_state->grounded = false;
            player_state->knockback_control_suppression_ticks =
                std::max<u32>(player_state->knockback_control_suppression_ticks, 4U);
            sessions_.for_each_session([&](const ServerSession& occupant_session) {
              if (!occupant_session.authenticated ||
                  occupant_session.player_id != player_id.value()) {
                return;
              }
              send_player_velocity(occupant_session, *player_state);
            });
          }
        }
        broadcast_ride_ended(
            occupant,
            vehicle_entity,
            ejection_position.value());
      }
      if (dismounted_any) {
        if (vehicle_net_id.has_value()) {
          accepted_vehicle_movement_.erase(vehicle_net_id.value());
        }
        broadcast_entity_passengers(vehicle_entity);
      }
    }
  }

  if (broadcast_explosion) {
    world::ExplosionRequest explosion;
    explosion.world_id = default_world_.id();
    explosion.origin = effect_position;
    explosion.profile_id = "arvane:vehicle";
    explosion.source_entity = vehicle_entity;
    explosion.seed = seed;
    (void)execute_explosion_request(std::move(explosion));
  }
  if (!default_world_.destroy_entity(
          vehicle_entity,
          world::EntityDespawnReason::Removed)) {
    return false;
  }
  if (vehicle_net_id.has_value()) {
    accepted_vehicle_movement_.erase(vehicle_net_id.value());
    vehicle_crash_cooldowns_.erase(vehicle_net_id.value());
    vehicle_smoke_states_.erase(vehicle_net_id.value());
  }
  default_world_.flush_entity_despawns();
  broadcast_pending_ride_ended_events();
  return true;
}

void ServerRuntimeImpl::handle_vehicle_damage_result(
    EntityID vehicle_entity,
    const world::DamageResult& damage,
    Vec3f normal,
    u32 seed) {
  if (!damage.applied ||
      default_world_.find_entity_vehicle_physics(vehicle_entity) == nullptr) {
    return;
  }
  const std::optional<EntityNetID> net_id =
      default_world_.network_id_for_entity(vehicle_entity);
  if (damage.killed) {
    (void)destroy_vehicle_from_damage(
        vehicle_entity,
        normal,
        seed,
        true);
    return;
  }
  if (!net_id.has_value()) {
    return;
  }
  const VehicleDamageStage stage =
      vehicle_damage_stage(default_world_.find_entity_health(vehicle_entity));
  if (stage == VehicleDamageStage::Healthy ||
      stage == VehicleDamageStage::Destroyed) {
    vehicle_smoke_states_.erase(net_id.value());
    return;
  }
  VehicleSmokeState& state = vehicle_smoke_states_[net_id.value()];
  if (state.stage != stage) {
    state.stage = VehicleDamageStage::Healthy;
    state.next_smoke_tick = tick_index_;
  }
}

bool ServerRuntimeImpl::apply_vehicle_crash(
    EntityID vehicle_entity,
    EntityNetID vehicle_net_id,
    const entity::VehicleDefinition& definition,
    const VehicleCrashAssessment& assessment,
    const VehicleCrashContact& contact,
    AcceptedVehicleMovementState& accepted,
    VehicleCrashDebugInfo* debug) {
  if (assessment.severity == entity::CrashSeverity::None ||
      !definition.crash.enabled ||
      !contact.resolved_safe) {
    return false;
  }
  if (const auto cooldown = vehicle_crash_cooldowns_.find(vehicle_net_id);
      cooldown != vehicle_crash_cooldowns_.end()) {
    if (tick_index_ < cooldown->second) {
      if (debug != nullptr) {
        debug->cooldown_suppressed = true;
        debug->cooldown_until = cooldown->second;
      }
      return false;
    }
    vehicle_crash_cooldowns_.erase(cooldown);
  }

  const f32 damage =
      crash_damage_for_severity(definition.crash, assessment.severity);
  const bool has_non_bump_consequence =
      crash_severity_at_least(assessment.severity, entity::CrashSeverity::HardImpact);
  if (!has_non_bump_consequence && damage <= 0.0F) {
    return false;
  }
  if (debug != nullptr) {
    debug->consequence_applied = true;
    debug->damage_applied = damage;
  }
  vehicle_crash_cooldowns_[vehicle_net_id] =
      tick_index_ + std::max<TickIndex>(1U, definition.crash.event_cooldown_ticks);

  if (has_non_bump_consequence) {
    accepted.velocity = accepted.velocity * definition.crash.velocity_retention;
    accepted.angular_velocity =
        accepted.angular_velocity * definition.crash.velocity_retention;
    apply_vehicle_accepted_state_to_world(
        default_world_,
        vehicle_entity,
        definition,
        accepted);
  }

  world::DamageResult crash_damage;
  if (damage > 0.0F) {
    crash_damage = default_world_.apply_damage(
        {vehicle_entity,
         vehicle_entity,
         damage,
         world::DamageCause::Generic,
         false});
  }

  if (crash_severity_at_least(assessment.severity, entity::CrashSeverity::Ejection) &&
      block_registry_ != nullptr) {
    const std::vector<EntityID> occupants =
        vehicle_occupants(default_world_, vehicle_entity);
    const std::optional<Vec3f> ejection_position =
        crash_ejection_position(
            default_world_,
            *block_registry_,
            vehicle_entity,
            player_tuning_);
    if (ejection_position.has_value()) {
      const Vec3f launch =
          crash_ejection_velocity(definition.crash, contact);
      bool ejected_any = false;
      u32 ejected_count = 0U;
      for (const EntityID occupant : occupants) {
        const std::optional<PlayerID> player_id =
            default_world_.player_for_entity(occupant);
        if (!default_world_.dismount_entity(occupant)) {
          continue;
        }
        ejected_any = true;
        ++ejected_count;
        (void)default_world_.set_entity_position(occupant, ejection_position.value());
        if (entity::EntityVelocity* velocity =
                default_world_.find_entity_velocity(occupant);
            velocity != nullptr) {
          velocity->linear = launch;
        }
        if (player_id.has_value()) {
          player::PlayerState* player_state =
              default_world_.find_player(player_id.value());
          if (player_state != nullptr) {
            player_state->position = ejection_position.value();
            player_state->velocity = launch;
            player_state->grounded = false;
            player_state->knockback_control_suppression_ticks =
                std::max<u32>(player_state->knockback_control_suppression_ticks, 6U);
            if (definition.crash.remount_cooldown_ticks > 0U) {
              crash_remount_cooldowns_[player_id.value()] =
                  tick_index_ + definition.crash.remount_cooldown_ticks;
            }
            sessions_.for_each_session([&](const ServerSession& occupant_session) {
              if (!occupant_session.authenticated ||
                  occupant_session.player_id != player_id.value()) {
                return;
              }
              send_player_velocity(occupant_session, *player_state);
            });
          }
        }
        broadcast_ride_ended(
            occupant,
            vehicle_entity,
            ejection_position.value());
      }
      if (ejected_any) {
        accepted_vehicle_movement_.erase(vehicle_net_id);
      }
      if (debug != nullptr) {
        debug->ejected_occupants = ejected_count;
      }
      if (default_world_.entity_exists(vehicle_entity)) {
        broadcast_entity_passengers(vehicle_entity);
      }
    }
  }

  if (crash_damage.applied) {
    handle_vehicle_damage_result(
        vehicle_entity,
        crash_damage,
        contact.normal,
        static_cast<u32>(
            vehicle_net_id.value ^ static_cast<u64>(tick_index_) ^ 0x51ed2705ULL));
    if (!default_world_.entity_exists(vehicle_entity)) {
      if (debug != nullptr) {
        debug->explosion_broadcast = true;
        debug->vehicle_destroyed = true;
        debug->accepted_velocity = accepted.velocity;
      }
      return true;
    }
  }

  if (assessment.severity == entity::CrashSeverity::Destruction) {
    const bool explode = crash_should_explode(definition.crash, assessment);
    if (definition.crash.destroy_on_destruction &&
        destroy_vehicle_from_damage(
            vehicle_entity,
            contact.normal,
            static_cast<u32>(
                vehicle_net_id.value ^ static_cast<u64>(tick_index_)),
            explode)) {
      if (debug != nullptr) {
        debug->explosion_broadcast = explode;
        debug->vehicle_destroyed = true;
        debug->accepted_velocity = accepted.velocity;
      }
      return true;
    }
  }
  if (debug != nullptr) {
    debug->accepted_velocity = accepted.velocity;
  }
  return false;
}

void ServerRuntimeImpl::handle_vehicle_move(
    net::ConnectionID connection_id,
    const protocol::ServerboundVehicleMove& move) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || block_registry_ == nullptr) {
    return;
  }
  const std::optional<EntityID> vehicle_entity =
      default_world_.entity_for_network_id(move.vehicle_id);
  const std::optional<EntityID> passenger_entity =
      default_world_.entity_for_player(session->player_id);
  if (!vehicle_entity.has_value() || !passenger_entity.has_value()) {
    return;
  }

  const entity::VehiclePhysicsComponent* vehicle_physics =
      default_world_.find_entity_vehicle_physics(vehicle_entity.value());
  const entity::VehicleDefinition* definition =
      vehicle_physics != nullptr && vehicle_registry_ != nullptr
          ? vehicle_registry_->find(vehicle_physics->definition)
          : nullptr;
  if (vehicle_physics == nullptr || definition == nullptr) {
    return;
  }

  AcceptedVehicleMovementState& previous =
      accepted_vehicle_movement_[move.vehicle_id];
  if (!previous.has_accepted_move) {
    previous = accepted_vehicle_state_from_world(
        default_world_,
        vehicle_entity.value(),
        move.vehicle_id,
        tick_index_,
        base_tps_);
  }

  const VehicleMovementValidationContext context =
      make_vehicle_validation_context(
          default_world_,
          *block_registry_,
          passenger_entity.value(),
          vehicle_entity.value(),
          move,
          previous,
          tick_index_,
          base_tps_);
  const VehicleMovementValidationResult validation =
      validate_serverbound_vehicle_move(
          move,
          previous,
          context,
          definition->authority);
  if (!validation.accepted) {
    if (validation.reason == protocol::VehicleCorrectionReason::Collision) {
      previous = validation.accepted_state;
      apply_vehicle_accepted_state_to_world(
          default_world_,
          vehicle_entity.value(),
          *definition,
          previous);
      const VehicleCrashAssessment crash =
          context.has_collision_contact
              ? assess_vehicle_crash(definition->crash, context.collision_contact)
              : VehicleCrashAssessment{};
      VehicleCrashDebugInfo debug =
          make_vehicle_crash_debug_info(
              move.vehicle_id,
              *definition,
              move,
              context,
              validation,
              crash,
              previous);
      if (apply_vehicle_crash(
              vehicle_entity.value(),
              move.vehicle_id,
              *definition,
              crash,
              context.collision_contact,
              previous,
              &debug)) {
        last_vehicle_crash_debug_[move.vehicle_id] = std::move(debug);
        return;
      }
      last_vehicle_crash_debug_[move.vehicle_id] = std::move(debug);
      broadcast_vehicle_state(move.vehicle_id, previous);
    } else {
      last_vehicle_crash_debug_[move.vehicle_id] =
          make_vehicle_crash_debug_info(
              move.vehicle_id,
              *definition,
              move,
              context,
              validation,
              VehicleCrashAssessment{},
              previous);
    }
    if (validation.should_send_correction) {
      send_vehicle_correction(
          *session,
          move.vehicle_id,
          previous,
          move.sequence,
          validation.reason);
    }
    return;
  }

  previous = validation.accepted_state;
  apply_vehicle_accepted_state_to_world(
      default_world_,
      vehicle_entity.value(),
      *definition,
      previous);
  const VehicleCrashAssessment crash =
      context.has_collision_contact
          ? assess_vehicle_crash(definition->crash, context.collision_contact)
          : VehicleCrashAssessment{};
  VehicleCrashDebugInfo debug =
      make_vehicle_crash_debug_info(
          move.vehicle_id,
          *definition,
          move,
          context,
          validation,
          crash,
          previous);
  if (apply_vehicle_crash(
          vehicle_entity.value(),
          move.vehicle_id,
          *definition,
          crash,
          context.collision_contact,
          previous,
          &debug)) {
    last_vehicle_crash_debug_[move.vehicle_id] = std::move(debug);
    return;
  }
  last_vehicle_crash_debug_[move.vehicle_id] = std::move(debug);
  broadcast_vehicle_state(move.vehicle_id, previous);
}

void ServerRuntimeImpl::handle_chat_message(
    net::ConnectionID connection_id,
    const protocol::ServerboundChatMessage& message) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || session->closing) {
    return;
  }

  const std::string channel = message.channel.value_or("arvane:global");
  if (std::string_view{channel} != protocol::ChatChannelGlobal) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message(arv::text::text_or_fallback(language_, "server.chat.error.unknown_channel", "Unknown or unavailable chat channel.")));
    return;
  }

  auto normalized = validate_chat_message_text(message.message, chat_validation_, language_);
  if (!normalized) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message(normalized.error().message));
    return;
  }

  const Seconds now_seconds =
      static_cast<Seconds>(tick_index_) / static_cast<Seconds>(std::max<TickRate>(base_tps_, 1U));
  ChatRateLimitState& rate_limit = chat_rate_limits_[session->session_id];
  if (!consume_chat_rate_limit(rate_limit, chat_validation_, now_seconds)) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message(arv::text::text_or_fallback(language_, "server.chat.error.rate_limited", "You are sending messages too quickly.")));
    return;
  }

  std::string normalized_message = std::move(normalized.value());
  if (is_command_chat_message(normalized_message)) {
    std::optional<command::CommandSender> sender =
        command_sender_for_session(session->session_id);
    if (!sender.has_value()) {
      send_error_message(
          *session,
          chat_formatter_.format_error_message(arv::text::text_or_fallback(
              language_,
              "ui.chat.command.no_sender",
              "No player command sender is available.")));
      return;
    }

    const command::CommandResult result =
        execute_command(sender.value(), normalized_message);
    bool sent_message = false;
    for (const command::CommandMessage& message : result.messages) {
      if (message.text.empty()) {
        continue;
      }
      if (command_message_is_error(message.kind)) {
        send_error_message(
            *session,
            authored_chat_message(message.text));
      } else {
        send_system_message(
            *session,
            authored_chat_message(message.text));
      }
      sent_message = true;
    }
    if (!sent_message && result.status != command::CommandStatus::PermissionDenied) {
      const std::string fallback =
          fallback_command_result_message(language_, result.status);
      if (command_status_is_error(result.status)) {
        send_error_message(
            *session,
            authored_chat_message(fallback));
      } else {
        send_system_message(
            *session,
            authored_chat_message(fallback));
      }
    }
    return;
  }

  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message(arv::text::text_or_fallback(
            language_,
            "ui.chat.command.no_sender",
            "No player command sender is available.")));
    return;
  }
  refresh_player_permissions(*player_state);
  if (!player_state->resolved_permissions.has(ChatSendPermission)) {
    if (!command_permission_denied_message_.empty()) {
      send_error_message(
          *session,
          authored_chat_message(command_permission_denied_message_));
    }
    return;
  }

  arv::text::ChatFormatContext context;
  context.language = language_;
  context.sender_profile_id = session->profile_id.string();
  context.sender_name =
      session->display_name.empty() ? session->profile_name : session->display_name;
  context.channel = channel;
  context.raw_message = std::move(normalized_message);
  context.message_markup.color =
      player_state->resolved_permissions.has(ChatColorPermission);
  context.message_markup.click =
      player_state->resolved_permissions.has(ChatClickPermission);
  context.message_markup.hover =
      player_state->resolved_permissions.has(ChatHoverPermission);
  arv::text::RichText formatted = chat_formatter_.format_player_chat(context);
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(formatted);
  if (!validation.valid) {
    core::warn("chat", "refusing to broadcast invalid formatted chat: {}", validation.message);
    send_error_message(
        *session,
        chat_formatter_.format_error_message(arv::text::text_or_fallback(language_, "server.chat.error.format_failed", "Chat message could not be formatted.")));
    return;
  }

  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:global",
      protocol::ChatMessageKind::Chat,
      std::move(formatted)};
  const std::vector<u8> payload = protocol::serialize_clientbound_chat_message(packet);
  sessions_.for_each_session([&](const ServerSession& recipient) {
    if (!recipient.authenticated || recipient.closing) {
      return;
    }
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        recipient.connection_id,
        protocol::MessageType::ClientboundChatMessage,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
    log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  });
  core::info(
      "chat",
      "[{}] {}: {}",
      packet.channel,
      context.sender_name,
      context.raw_message);
}

void ServerRuntimeImpl::send_system_message(
    const ServerSession& recipient,
    arv::text::RichText message) {
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to send invalid system chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::System,
      std::move(message)};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      recipient.connection_id,
      protocol::MessageType::ClientboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_chat_message(packet));
  log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
}

bool ServerRuntimeImpl::send_private_message(
    const ServerSession& recipient,
    arv::text::RichText message) {
  if (recipient.closing ||
      recipient.connection_state == net::ConnectionState::Disconnected ||
      recipient.connection_state == net::ConnectionState::Disconnecting ||
      !recipient.authenticated) {
    return false;
  }
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to send invalid private chat: {}", validation.message);
    return false;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::PrivateMessage,
      std::move(message)};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      recipient.connection_id,
      protocol::MessageType::ClientboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_chat_message(packet));
  log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  return static_cast<bool>(send_result);
}

void ServerRuntimeImpl::send_error_message(
    const ServerSession& recipient,
    arv::text::RichText message) {
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to send invalid error chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::Error,
      std::move(message)};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      recipient.connection_id,
      protocol::MessageType::ClientboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_chat_message(packet));
  log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
}

void ServerRuntimeImpl::broadcast_system_message(arv::text::RichText message) {
  const arv::text::RichTextValidation validation = arv::text::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to broadcast invalid system chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::System,
      std::move(message)};
  const std::vector<u8> payload = protocol::serialize_clientbound_chat_message(packet);
  sessions_.for_each_session([&](const ServerSession& recipient) {
    if (!recipient.authenticated || recipient.closing) {
      return;
    }
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        recipient.connection_id,
        protocol::MessageType::ClientboundChatMessage,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
    log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  });
}

void ServerRuntimeImpl::handle_skin_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinRequestDenyReason reason) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(reason));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        reason};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (session == nullptr || !session->authenticated) {
    return;
  }
  if (!skins_.enabled) {
    send_deny(protocol::SkinRequestDenyReason::SkinsDisabled);
    return;
  }
  if (!protocol::is_valid_skin_hash(request.skin_hash)) {
    send_deny(protocol::SkinRequestDenyReason::InvalidHash);
    return;
  }

  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (!transfer.request_second_limiter.try_consume(1.0, now_seconds) ||
      !transfer.request_minute_limiter.try_consume(1.0, now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (transfer.in_flight_request_ids.find(request.request_id) !=
          transfer.in_flight_request_ids.end() ||
      transfer.in_flight_skin_hashes.find(request.skin_hash) !=
          transfer.in_flight_skin_hashes.end() ||
      transfer.in_flight_request_ids.size() >=
          skins_.downloads.max_in_flight_per_connection) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (skins_.downloads.require_advertised_hash &&
      transfer.advertised_skin_hashes.find(request.skin_hash) ==
          transfer.advertised_skin_hashes.end()) {
    send_deny(protocol::SkinRequestDenyReason::NotAdvertisedToClient);
    return;
  }
  auto descriptor_result = load_stored_skin_descriptor(server_save_root_, request.skin_hash);
  if (!descriptor_result) {
    send_deny(descriptor_result.error());
    return;
  }
  const protocol::SkinDescriptor descriptor = descriptor_result.value();
  const io::SkinResolutionConfig* resolution =
      descriptor.width == descriptor.height
          ? find_skin_resolution_rule(skins_, descriptor.width)
          : nullptr;
  if (resolution == nullptr || descriptor.width == 0U || descriptor.height == 0U) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }
  const std::filesystem::path path =
      server_skin_storage_path(server_save_root_, request.skin_hash);
  const std::optional<u64> file_size = regular_file_size(path);
  if (!file_size.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (file_size.value() == 0U ||
      file_size.value() > resolution->max_upload_bytes ||
      file_size.value() > protocol::MaxSkinImagePayloadBytes) {
    send_deny(protocol::SkinRequestDenyReason::TooLarge);
    return;
  }
  if (!transfer.download_byte_limiter.try_consume(
          static_cast<double>(file_size.value()),
          now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }

  std::optional<std::vector<u8>> bytes = io::try_read_binary_file(path);
  if (!bytes.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (bytes->size() != file_size.value()) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }

  transfer.in_flight_request_ids.insert(request.request_id);
  transfer.in_flight_skin_hashes.insert(request.skin_hash);
  auto finish_request = [&]() {
    transfer.in_flight_request_ids.erase(request.request_id);
    transfer.in_flight_skin_hashes.erase(request.skin_hash);
  };
  const bool chunked =
      bytes->size() >= skins_.uploads.chunk_threshold_bytes ||
      descriptor.width >= 512U ||
      descriptor.height >= 512U;
  if (!chunked) {
    protocol::ClientboundSkinData data;
    data.request_id = request.request_id;
    data.skin_hash = request.skin_hash;
    data.format = descriptor.format;
    data.width = descriptor.width;
    data.height = descriptor.height;
    data.compressed_image_bytes = std::move(*bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinData,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data(data));
    finish_request();
    return;
  }

  protocol::ClientboundSkinDataBeginChunked begin;
  begin.request_id = request.request_id;
  begin.skin_hash = request.skin_hash;
  begin.format = descriptor.format;
  begin.width = descriptor.width;
  begin.height = descriptor.height;
  begin.total_bytes = static_cast<u32>(bytes->size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinDataBeginChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_begin_chunked(begin));
  usize offset = 0;
  while (offset < bytes->size()) {
    const usize chunk_size = std::min<usize>(
        skins_.uploads.max_chunk_bytes,
        bytes->size() - offset);
    protocol::ClientboundSkinDataChunk chunk;
    chunk.request_id = request.request_id;
    chunk.chunk.insert(
        chunk.chunk.end(),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinDataChunk,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data_chunk(chunk));
    offset += chunk_size;
  }
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinDataEndChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_end_chunked({request.request_id}));
  finish_request();
}

void ServerRuntimeImpl::handle_skin_request_batch(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequestBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  auto send_deny = [&](const protocol::SkinRequestEntry& request) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(protocol::SkinRequestDenyReason::RateLimited));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        protocol::SkinRequestDenyReason::RateLimited};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (batch.requests.size() > skins_.downloads.max_batch_size) {
    for (const protocol::SkinRequestEntry& request : batch.requests) {
      send_deny(request);
    }
    return;
  }

  for (const protocol::SkinRequestEntry& entry : batch.requests) {
    handle_skin_request(connection_id, {entry.request_id, entry.skin_hash});
  }
}

void ServerRuntimeImpl::handle_skin_upload_begin(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadBegin& begin) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload denied: connection={} reason={} width={} height={} bytes={}",
        connection_id.value,
        skin_upload_deny_reason_name(reason),
        begin.width,
        begin.height,
        begin.total_upload_bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_deny({reason}));
  };
  if (session == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  if (!session->authenticated) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  if (transfer.active_upload.has_value() &&
      skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  const bool rate_limited =
      !transfer.upload_begin_limiter.try_consume(1.0, now_seconds) ||
      transfer.failed_decode_limiter.available(now_seconds) < 1.0;
  const auto accepted = validate_skin_upload_begin(
      skins_,
      player_state->resolved_permissions,
      session->authenticated,
      transfer.active_upload.has_value(),
      rate_limited,
      active_skin_uploads_global_ >= skins_.uploads.max_active_global,
      begin);
  if (!accepted) {
    send_deny(accepted.error());
    return;
  }

  const u32 upload_id = transfer.next_upload_id++;
  if (!begin.client_skin_hash_hint.empty()) {
    auto existing_descriptor =
        load_stored_skin_descriptor(server_save_root_, begin.client_skin_hash_hint);
    if (existing_descriptor &&
        existing_descriptor.value().width == begin.width &&
        existing_descriptor.value().height == begin.height) {
      protocol::SkinDescriptor descriptor = std::move(existing_descriptor.value());
      descriptor.slim_model = begin.slim_model;
      set_selected_skin_for_session(*session, descriptor.skin_hash, descriptor.slim_model);
      core::info(
          "skin",
          "skin upload complete: profile={} skin={} bytes=0",
          session->profile_id.string(),
          descriptor.skin_hash);
      (void)send_protocol_message(
          *transport_,
          connection_id,
          protocol::MessageType::ClientboundSkinUploadComplete,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Session,
          protocol::serialize_skin_upload_complete(
              {upload_id, std::move(descriptor)}));
      broadcast_player_profile_info(*session);
      return;
    }
  }

  if (!transfer.upload_byte_limiter.try_consume(
          static_cast<double>(begin.total_upload_bytes),
          now_seconds)) {
    send_deny(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload;
  upload.upload_id = upload_id;
  upload.begin = begin;
  upload.mode = accepted.value().upload_mode;
  upload.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  upload.started_tick = tick_index_;
  upload.last_packet_tick = tick_index_;
  upload.session_id = session->session_id;
  upload.bytes.reserve(begin.total_upload_bytes);
  transfer.active_upload = std::move(upload);
  ++active_skin_uploads_global_;

  protocol::ClientboundSkinUploadAccept packet;
  packet.upload_id = upload_id;
  packet.upload_mode = accepted.value().upload_mode;
  packet.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadAccept,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_accept(packet));
}

void ServerRuntimeImpl::handle_skin_upload_data(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadData& data) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        data.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({data.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != data.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Full) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (data.compressed_image_bytes.size() !=
          transfer.active_upload->begin.total_upload_bytes ||
      data.compressed_image_bytes.size() >
          transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      data.compressed_image_bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_for_session(
      *session,
      descriptor.value().skin_hash,
      descriptor.value().slim_model);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      data.compressed_image_bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({data.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_skin_upload_chunk(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadChunk& chunk) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        chunk.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({chunk.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != chunk.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (chunk.image_chunk_bytes.empty() ||
      chunk.image_chunk_bytes.size() > transfer.active_upload->max_chunk_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  if (transfer.active_upload->bytes.size() + chunk.image_chunk_bytes.size() >
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  transfer.active_upload->bytes.insert(
      transfer.active_upload->bytes.end(),
      chunk.image_chunk_bytes.begin(),
      chunk.image_chunk_bytes.end());
  transfer.active_upload->last_packet_tick = tick_index_;
}

void ServerRuntimeImpl::handle_skin_upload_end(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadEnd& end) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        end.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({end.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != end.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (transfer.active_upload->bytes.size() !=
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }
  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      upload.bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_for_session(
      *session,
      descriptor.value().skin_hash,
      descriptor.value().slim_model);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      upload.bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({end.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_chunk_interest_update(
    net::ConnectionID connection_id,
    const protocol::ServerboundChunkInterestUpdate& interest) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    return;
  }

  const ChunkSectionCoord authoritative_center = player_chunk_coord(*player_state);
  chunk_streamer_.update_player_interest(
      *session,
      authoritative_center,
      default_world_.height_settings(),
      interest);
  if (mode_ == ServerMode::Dedicated &&
      logged_chunk_interest_sessions_.insert(session->session_id).second) {
    core::info(
        "dedicated",
        "client '{}' requested chunk stream: session={} player={} center=({}, {}, {}) requested_view={} server_max_view={}",
        session->profile_name,
        session->session_id.value,
        session->player_id.value,
        authoritative_center.value.x,
        authoritative_center.value.y,
        authoritative_center.value.z,
        interest.client_view_distance,
        max_view_distance_);
  }
}

void ServerRuntimeImpl::handle_inventory_action_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundInventoryActionRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated ||
      request.inventory_id != protocol::PlayerInventoryID) {
    return;
  }

  switch (request.kind) {
    case protocol::InventoryActionKind::SlotClick:
      (void)apply_player_inventory_click(
          session->player_id,
          request.slot_index,
          request.button);
      return;
    case protocol::InventoryActionKind::ShiftTransfer:
      (void)shift_transfer_player_inventory_slot(
          session->player_id,
          request.slot_index);
      return;
    case protocol::InventoryActionKind::DropCursor:
      (void)drop_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::DropOneCursor:
      (void)drop_one_from_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::DropOneSlot:
      (void)drop_one_from_player_inventory_slot(
          session->player_id,
          request.slot_index);
      return;
    case protocol::InventoryActionKind::CloseMenu:
      (void)close_player_inventory_menu(session->player_id);
      return;
  }
}

} // namespace arv::server::detail
