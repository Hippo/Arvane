#pragma once

#include <string_view>

namespace arv::base::ids {

inline constexpr std::string_view PackID = "arvane_base";
inline constexpr std::string_view ModuleName = "arv_base";
inline constexpr std::string_view Namespace = "arvane";

inline constexpr std::string_view SurvivalGameModeID = "survival";
inline constexpr std::string_view CreativeGameModeID = "creative";
inline constexpr std::string_view SpectatorGameModeID = "spectator";
inline constexpr std::string_view LobbyGameModeID = "lobby";
inline constexpr std::string_view NoClipGameModeID = "noclip";

inline constexpr std::string_view PlayerEntityTypeName = "arvane:player";
inline constexpr std::string_view DroppedItemEntityTypeName = "arvane:dropped_item";

inline constexpr std::string_view IdleGoalID = "arvane:idle";
inline constexpr std::string_view WanderGoalID = "arvane:wander";
inline constexpr std::string_view AvoidFluidGoalID = "arvane:avoid_fluid";
inline constexpr std::string_view FloatInFluidGoalID = "arvane:float_in_fluid";
inline constexpr std::string_view SwimInFluidGoalID = "arvane:swim_in_fluid";
inline constexpr std::string_view NearestTargetGoalID = "arvane:nearest_target";
inline constexpr std::string_view MoveToTargetGoalID = "arvane:move_to_target";
inline constexpr std::string_view LookAtTargetGoalID = "arvane:look_at_target";
inline constexpr std::string_view MeleeAttackGoalID = "arvane:melee_attack";
inline constexpr std::string_view FleeFromTargetGoalID = "arvane:flee_from_target";
inline constexpr std::string_view ReturnHomeGoalID = "arvane:return_home";
inline constexpr std::string_view MineableTagsBlockBreakBehaviorID =
    "arvane:block_break/mineable_tags";
inline constexpr std::string_view FluidBlockBehaviorID =
    "arvane:block_behavior/fluid";
inline constexpr std::string_view BlockItemBehaviorID =
    "arvane:item_behavior/block";
inline constexpr std::string_view ToolItemBehaviorID =
    "arvane:item_behavior/tool";
inline constexpr std::string_view EmptyFluidContainerItemBehaviorID =
    "arvane:item_behavior/empty_fluid_container";
inline constexpr std::string_view FilledFluidContainerItemBehaviorID =
    "arvane:item_behavior/filled_fluid_container";

inline constexpr std::string_view AirBlockID = "arvane:air";
inline constexpr std::string_view StoneBlockID = "arvane:stone";
inline constexpr std::string_view DirtBlockID = "arvane:dirt";
inline constexpr std::string_view GrassBlockID = "arvane:grass_block";
inline constexpr std::string_view GlassBlockID = "arvane:glass";
inline constexpr std::string_view GlowstoneBlockID = "arvane:glowstone";
inline constexpr std::string_view WaterBlockID = "arvane:water";

inline constexpr std::string_view BucketItemID = "arvane:bucket";
inline constexpr std::string_view WaterBucketItemID = "arvane:water_bucket";
inline constexpr std::string_view CoalItemID = "arvane:coal";
inline constexpr std::string_view WoodPickaxeItemID = "arvane:wood_pickaxe";

inline constexpr std::string_view OverworldWorldgenDataID = "arvane:worldgen/dimension/overworld";

inline constexpr std::string_view PlayerModelAssetID = "arvane:model/entity/player";
inline constexpr std::string_view PlayerAnimationAssetID = "arvane:animation/player";

inline constexpr std::string_view TeleportPermissionNode = "arvane.command.teleport";
inline constexpr std::string_view MsgPermissionNode = "arvane.command.msg";
inline constexpr std::string_view SpawnPermissionNode = "arvane.command.spawn";
inline constexpr std::string_view SetSpawnPermissionNode = "arvane.command.setspawn";
inline constexpr std::string_view SelectionPermissionNode = "arvane.command.selection";
inline constexpr std::string_view BlueprintPermissionNode = "arvane.command.blueprint";
inline constexpr std::string_view FlySpeedPermissionNode = "arvane.commands.flyspeed";

} // namespace arv::base::ids
