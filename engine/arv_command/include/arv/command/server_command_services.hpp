#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/location.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::command {

struct OnlinePlayerCommandTarget {
  SessionID session_id{};
  PlayerID player_id{};
  WorldID world_id{};
  std::string display_name{};
  Vec3f position{};
  f32 yaw_radians{};
  f32 pitch_radians{};
};

struct EntityCommandTarget {
  EntityID entity_id{};
  std::optional<PlayerID> player_id{};
  std::optional<SessionID> session_id{};
  WorldID world_id{};
  std::string display_name{};
  Vec3f position{};
};

struct PlayerBlockSelection {
  std::optional<WorldID> world{};
  std::optional<BlockPos> a{};
  std::optional<BlockPos> b{};
};

struct WorldTimeCommandState {
  TickIndex world_tick{};
  u32 day_time_tick{};
  u32 day_length_ticks{};
  bool daylight_cycle{};
};

struct GameRuleCommandEntry {
  std::string name{};
  std::string type{};
  std::string value{};
  std::string description{};
};

struct GameRuleSetCommandResult {
  bool success{};
  std::string old_value{};
  std::string new_value{};
  std::string message{};
};

struct ItemCommandRecord {
  u32 item_id{};
  std::string name{};
  std::string display_name{};
  u16 max_stack_size{};
};

struct EntityTypeCommandRecord {
  u32 type_id{};
  std::string name{};
  std::string display_name{};
  bool spawnable{};
};

struct InventoryInsertCommandResult {
  bool success{true};
  std::string message{};
  u32 requested{};
  u32 inserted{};
  u32 leftover{};
};

struct InventoryClearCommandResult {
  bool success{true};
  std::string message{};
  u32 removed_items{};
  u32 removed_stacks{};
};

struct StatusEffectCommandState {
  std::string effect_id{};
  u32 remaining_ticks{};
  u32 amplifier{};
};

struct SaveAllCommandResult {
  bool success{};
  bool flushed{};
  std::string message{};
  u32 saved_chunks{};
  u32 saved_players{};
  u32 saved_entities{};
};

struct ExplosionCommandRequest {
  WorldID world_id{};
  Vec3f origin{};
  std::string profile_id{"arvane:tnt"};
  std::optional<EntityID> source_entity{};
  u64 seed{};
  std::optional<f32> power{};
  std::optional<f32> radius{};
  std::optional<bool> block_damage_enabled{};
  std::optional<bool> entity_damage_enabled{};
  std::optional<bool> drops_enabled{};
  std::optional<bool> fire_enabled{};
  std::optional<bool> future_sound_enabled{};
  std::optional<u32> max_destroyed_blocks{};
  std::optional<u32> max_rays{};
  std::optional<u32> max_drops{};
};

struct ExplosionCommandResult {
  bool success{};
  std::string message{};
  std::string profile_id{};
  Vec3f origin{};
  u32 destroyed_blocks{};
  u32 affected_entities{};
};

struct DebugCommandStats {
  TickIndex tick_index{};
  TickRate tick_rate{};
  usize entity_count{};
  usize loaded_chunk_count{};
  usize active_chunk_count{};
  usize pending_generation_jobs{};
  usize pending_lighting_jobs{};
  usize pending_fluid_jobs{};
};

using OnlinePlayerByIDFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(PlayerID)>;
using OnlinePlayerByNameFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(std::string_view)>;
using OnlinePlayersFn = FunctionRef<std::vector<OnlinePlayerCommandTarget>()>;
using ResolvePlayerTargetsFn =
    FunctionRef<std::vector<OnlinePlayerCommandTarget>(
        const CommandSender&,
        std::string_view)>;
using ResolveEntityTargetsFn =
    FunctionRef<std::vector<EntityCommandTarget>(
        const CommandSender&,
        std::string_view)>;
using TeleportPlayerFn = FunctionRef<bool(PlayerID, Vec3f)>;
using TeleportPlayerLocationFn = FunctionRef<bool(PlayerID, Location)>;
using SetPlayerGameModeFn = FunctionRef<bool(PlayerID, std::string_view)>;
using SetPlayerFlySpeedFn = FunctionRef<bool(PlayerID, f32)>;
using DefaultPlayerFlySpeedFn = FunctionRef<f32()>;
using MaxPlayerFlySpeedFn = FunctionRef<f32()>;
using WorldTimeQueryFn = FunctionRef<std::optional<WorldTimeCommandState>(WorldID)>;
using WorldTimeSetFn = FunctionRef<bool(WorldID, u32)>;
using WorldTimeAddFn = FunctionRef<bool(WorldID, u32)>;
using WorldWeatherQueryFn = FunctionRef<std::optional<std::string>(WorldID)>;
using WorldWeatherSetFn = FunctionRef<bool(WorldID, std::string_view)>;
using WorldDifficultyQueryFn = FunctionRef<std::optional<std::string>(WorldID)>;
using WorldDifficultySetFn = FunctionRef<bool(WorldID, std::string_view)>;
using GameRuleListFn = FunctionRef<std::vector<GameRuleCommandEntry>(WorldID)>;
using GameRuleGetFn =
    FunctionRef<std::optional<GameRuleCommandEntry>(WorldID, std::string_view)>;
using GameRuleSetFn =
    FunctionRef<GameRuleSetCommandResult(WorldID, std::string_view, std::string_view)>;
using EntityTypeByNameFn =
    FunctionRef<std::optional<EntityTypeCommandRecord>(std::string_view)>;
using SpawnEntityFn =
    FunctionRef<std::optional<EntityCommandTarget>(WorldID, std::string_view, Vec3f)>;
using KillEntityFn = FunctionRef<bool(EntityID)>;
using ItemByNameFn = FunctionRef<std::optional<ItemCommandRecord>(std::string_view)>;
using GivePlayerItemFn =
    FunctionRef<InventoryInsertCommandResult(
        PlayerID,
        u32,
        u16,
        std::optional<std::string>)>;
using ClearPlayerInventoryFn =
    FunctionRef<InventoryClearCommandResult(PlayerID, std::optional<u32>)>;
using ApplyStatusEffectFn =
    FunctionRef<bool(EntityID, std::string_view, u32, u32)>;
using ClearStatusEffectsFn =
    FunctionRef<u32(EntityID, std::optional<std::string_view>)>;
using ListStatusEffectsFn =
    FunctionRef<std::vector<StatusEffectCommandState>(EntityID)>;
using SaveAllFn = FunctionRef<SaveAllCommandResult(bool)>;
using CreateExplosionFn =
    FunctionRef<ExplosionCommandResult(const ExplosionCommandRequest&)>;
using BroadcastSystemMessageFn = FunctionRef<bool(std::string_view)>;
using SendPrivateMessageFn = FunctionRef<bool(SessionID, std::string_view)>;
using KickPlayerFn = FunctionRef<bool(PlayerID, std::string_view)>;
using DebugStatsFn = FunctionRef<DebugCommandStats(WorldID)>;
using PlayerSelectionByIDFn =
    FunctionRef<std::optional<PlayerBlockSelection>(PlayerID)>;
using SetPlayerSelectionFn = FunctionRef<bool(PlayerID, const PlayerBlockSelection&)>;
using WorldBlockReadFn = FunctionRef<std::optional<u32>(WorldID, BlockPos)>;
using WorldBlockWriteFn = FunctionRef<bool(WorldID, BlockPos, u32)>;
using BlockVariantNameFn = FunctionRef<std::optional<std::string>(u32)>;
using BlockVariantByNameFn = FunctionRef<std::optional<u32>(std::string_view)>;
using AirBlockVariantFn = FunctionRef<std::optional<u32>()>;
using BlueprintListFn = FunctionRef<std::vector<std::string>(WorldID)>;
using BlueprintLoadFn = FunctionRef<std::optional<std::string>(WorldID, std::string_view)>;
using BlueprintSaveFn = FunctionRef<bool(WorldID, std::string_view, std::string_view)>;
using WorldSpawnReadFn = FunctionRef<std::optional<Location>(WorldID)>;
using WorldSpawnWriteFn = FunctionRef<bool(WorldID, Location)>;

struct ServerCommandServices {
  OnlinePlayerByIDFn online_player_by_id{};
  OnlinePlayerByNameFn online_player_by_name{};
  OnlinePlayersFn online_players{};
  ResolvePlayerTargetsFn resolve_player_targets{};
  ResolveEntityTargetsFn resolve_entity_targets{};
  TeleportPlayerFn teleport_player{};
  TeleportPlayerLocationFn teleport_player_location{};
  SetPlayerGameModeFn set_player_game_mode{};
  SetPlayerFlySpeedFn set_player_fly_speed{};
  DefaultPlayerFlySpeedFn default_player_fly_speed{};
  MaxPlayerFlySpeedFn max_player_fly_speed{};
  WorldTimeQueryFn query_world_time{};
  WorldTimeSetFn set_world_time{};
  WorldTimeAddFn add_world_time{};
  WorldWeatherQueryFn query_world_weather{};
  WorldWeatherSetFn set_world_weather{};
  WorldDifficultyQueryFn query_world_difficulty{};
  WorldDifficultySetFn set_world_difficulty{};
  GameRuleListFn list_game_rules{};
  GameRuleGetFn get_game_rule{};
  GameRuleSetFn set_game_rule{};
  EntityTypeByNameFn entity_type_by_name{};
  SpawnEntityFn spawn_entity{};
  KillEntityFn kill_entity{};
  ItemByNameFn item_by_name{};
  GivePlayerItemFn give_player_item{};
  ClearPlayerInventoryFn clear_player_inventory{};
  ApplyStatusEffectFn apply_status_effect{};
  ClearStatusEffectsFn clear_status_effects{};
  ListStatusEffectsFn list_status_effects{};
  SaveAllFn save_all{};
  CreateExplosionFn create_explosion{};
  BroadcastSystemMessageFn broadcast_system_message{};
  SendPrivateMessageFn send_private_message{};
  KickPlayerFn kick_player{};
  DebugStatsFn debug_stats{};
  PlayerSelectionByIDFn player_selection_by_id{};
  SetPlayerSelectionFn set_player_selection{};
  WorldBlockReadFn read_block{};
  WorldBlockWriteFn write_block{};
  BlockVariantNameFn block_variant_name{};
  BlockVariantByNameFn block_variant_by_name{};
  AirBlockVariantFn air_block_variant{};
  BlueprintListFn list_blueprints{};
  BlueprintLoadFn load_blueprint{};
  BlueprintSaveFn save_blueprint{};
  WorldSpawnReadFn read_world_spawn{};
  WorldSpawnWriteFn write_world_spawn{};
};

} // namespace arv::command
