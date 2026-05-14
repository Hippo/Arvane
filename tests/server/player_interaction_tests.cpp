#include "arv/client/client_runtime.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_state.hpp"
#include "arv/server/server_combat.hpp"
#include "arv/server/server_runtime.hpp"
#include "support/entity_animation.hpp"
#include "support/server_harness.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using arv::testsupport::complete_integrated_handshake;
using arv::testsupport::connected_player;
using arv::testsupport::find_active_animation;

struct CombatValidationFixture {
  arv::voxel::BlockRegistry block_registry{arv::testsupport::make_test_block_registry()};
  arv::player::ItemRegistry item_registry{arv::testsupport::make_test_item_registry(block_registry)};
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::server::CombatConfig config{};
  arv::server::PlayerCombatState combat_state{};
  arv::player::PlayerState* player{};

  CombatValidationFixture() {
    player = &world.ensure_player(arv::PlayerID{1});
    player->world_id = world.id();
    player->position = {0.0F, 0.0F, 0.0F};
    player->selected_hotbar_slot = 0;
    player->resolved_permissions.core.can_interact = true;
    const std::optional<arv::player::ItemID> pickaxe =
        item_registry.find_item("arvane:wood_pickaxe");
    EXPECT_TRUE(pickaxe.has_value());
    EXPECT_TRUE(player->inventory.set_slot(
        0,
        arv::player::ItemStack{pickaxe.value(), 1U}));
  }

  [[nodiscard]] arv::EntityID create_target(
      arv::Vec3f position,
      bool health = true) {
    arv::entity::EntityComponentSet components;
    components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
    if (health) {
      components.health = arv::entity::AuthoredHealthComponent{10.0F};
    }
    arv::world::EntityCreateInfo info;
    info.position = position;
    info.authored_components = components;
    return world.create_entity(std::move(info));
  }

  [[nodiscard]] arv::server::AttackResult attack(
      arv::EntityID target,
      arv::u32 sequence,
      arv::TickIndex tick) {
    const std::optional<arv::EntityNetID> target_net = world.network_id_for_entity(target);
    EXPECT_TRUE(target_net.has_value());
    return arv::server::validate_and_apply_entity_attack(
        world,
        *player,
        item_registry,
        config,
        combat_state,
        target_net.value(),
        sequence,
        tick,
        &block_registry);
  }
};

void prepare_test_player(
    arv::server::ServerRuntime& server,
    const arv::player::ItemRegistry& item_registry) {
  arv::player::PlayerState& player_state = connected_player(server);
  player_state.position = {0.5F, 0.0F, 0.5F};
  player_state.velocity = {};
  player_state.yaw_radians = std::numbers::pi_v<float> * 0.5F;
  player_state.pitch_radians = 0.0F;
  player_state.grounded = false;
  player_state.selected_hotbar_slot = 0;
  ASSERT_TRUE(player_state.inventory.set_slot(
      0,
      arv::player::ItemStack{
          item_registry.find_item("arvane:stone").value(),
          2}));
}

void set_test_player_held_item(
    arv::server::ServerRuntime& server,
    const arv::player::ItemRegistry& item_registry,
    std::string_view item_name,
    arv::u16 count = 1U) {
  arv::player::PlayerState& player_state = connected_player(server);
  player_state.position = {0.5F, 0.0F, 0.5F};
  player_state.velocity = {};
  player_state.yaw_radians = std::numbers::pi_v<float> * 0.5F;
  player_state.pitch_radians = 0.0F;
  player_state.grounded = false;
  player_state.selected_hotbar_slot = 0;
  ASSERT_TRUE(player_state.inventory.set_slot(
      0,
      arv::player::ItemStack{
          item_registry.find_item(item_name).value(),
      count}));
}

arv::server::FluidContainerInteractionConfig make_test_fluid_containers() {
  return {"arvane:bucket", "arvane:water_bucket"};
}

arv::server::BlockBreakBehaviorRegistry make_base_block_break_behaviors() {
  arv::server::BlockBreakBehaviorRegistry registry;
  arv::base::register_base_block_break_behaviors(registry);
  return registry;
}

arv::voxel::BlockRegistry make_timed_break_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("arvane:stone");
  stone.properties.hardness = 0.2F;
  stone.tags.push_back("arvane:mineable/pickaxe");
  definitions.push_back(std::move(stone));
  arv::voxel::BlockDefinition dirt =
      arv::voxel::make_full_cube_definition("arvane:dirt");
  dirt.properties.hardness = 0.05F;
  dirt.properties.drops_with_wrong_tool = true;
  dirt.tags.push_back("arvane:mineable/shovel");
  definitions.push_back(std::move(dirt));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

arv::voxel::BlockRegistry make_stair_placement_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition stairs;
  stairs.name = "arvane:stone_stairs";
  stairs.render_category = arv::voxel::RenderCategory::Opaque;
  stairs.properties.solid = true;
  stairs.properties.collidable = true;
  stairs.properties.opaque = true;
  stairs.properties.blocks_light = true;
  stairs.properties.ao_occluder = true;
  stairs.states.push_back({"facing", {"north", "south", "east", "west"}});
  stairs.states.push_back({"half", {"bottom", "top"}});
  definitions.push_back(std::move(stairs));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

} // namespace

TEST(PlayerInteractionTests, EntityAttackWithinValidationRangeAppliesServerDamage) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.damage_applied);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::None);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 7.0F);
}

TEST(PlayerInteractionTests, EntityAttackBeyondValidationRangeIsRejected) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({7.0F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_FALSE(result.damage_applied);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::OutOfRange);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 10.0F);
}

TEST(PlayerInteractionTests, EntityAttackUsesGraceButHonorsMaxValidationRange) {
  CombatValidationFixture fixture;
  fixture.config.attack_validation_grace_blocks = 10.0F;
  fixture.config.max_attack_validation_range = 4.0F;
  const arv::EntityID target = fixture.create_target({5.0F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::OutOfRange);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 10.0F);
}

TEST(PlayerInteractionTests, EntityAttackCanRejectCurrentBlockObstruction) {
  CombatValidationFixture fixture;
  fixture.config.use_current_block_obstruction_check = true;
  (void)fixture.world.ensure_chunk({{0, 0, 0}});
  const std::optional<arv::voxel::BlockVariantID> stone =
      fixture.block_registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone.has_value());
  ASSERT_TRUE(fixture.world.set_block({{1, 1, 0}}, stone.value()));
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::BlockObstructed);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 10.0F);
}

TEST(PlayerInteractionTests, DuplicateAttackSequenceDoesNotApplyDamageTwice) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult first = fixture.attack(target, 7U, 10U);
  const arv::server::AttackResult duplicate = fixture.attack(target, 7U, 40U);

  EXPECT_TRUE(first.accepted);
  EXPECT_TRUE(first.damage_applied);
  EXPECT_FALSE(duplicate.accepted);
  EXPECT_EQ(duplicate.reject_reason, arv::server::AttackRejectReason::Duplicate);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 7.0F);
}

TEST(PlayerInteractionTests, EntityAttackCooldownPreventsFastRepeatedHits) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult first = fixture.attack(target, 1U, 10U);
  const arv::server::AttackResult too_soon = fixture.attack(target, 2U, 11U);

  EXPECT_TRUE(first.accepted);
  EXPECT_FALSE(too_soon.accepted);
  EXPECT_EQ(too_soon.reject_reason, arv::server::AttackRejectReason::Cooldown);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 7.0F);
}

TEST(PlayerInteractionTests, EntityAttackRejectsMissingTarget) {
  CombatValidationFixture fixture;

  const arv::server::AttackResult result =
      arv::server::validate_and_apply_entity_attack(
          fixture.world,
          *fixture.player,
          fixture.item_registry,
          fixture.config,
          fixture.combat_state,
          arv::EntityNetID{9999U},
          1U,
          10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::MissingTarget);
}

TEST(PlayerInteractionTests, EntityAttackRejectsTargetsWithoutHealth) {
  CombatValidationFixture fixture;
  const arv::EntityID target =
      fixture.create_target({2.8F, 0.0F, 0.0F}, false);

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::TargetNotDamageable);
}

TEST(PlayerInteractionTests, EntityAttackRejectsAlreadyDeadTargets) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});
  arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  health->current = 0.0F;

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::TargetDead);
}

TEST(PlayerInteractionTests, EntityAttackAcceptsButDoesNotDamageInvulnerableTargets) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});
  arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  health->invulnerable = true;

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_FALSE(result.damage_applied);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::None);
  EXPECT_FLOAT_EQ(health->current, 10.0F);
}

TEST(PlayerInteractionTests, EntityAttackRejectsWhenGamemodeDisallowsInteraction) {
  CombatValidationFixture fixture;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});
  fixture.player->resolved_permissions.core.can_interact = false;

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reject_reason, arv::server::AttackRejectReason::GamemodeDisallowsAttack);
}

TEST(PlayerInteractionTests, EntityAttackUsesDefaultFistProfileForEmptyHand) {
  CombatValidationFixture fixture;
  ASSERT_TRUE(fixture.player->inventory.clear_slot(0));
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.damage_applied);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 9.0F);
}

TEST(PlayerInteractionTests, EntityAttackUsesDefaultFistProfileForHeldItemsWithoutAttackData) {
  CombatValidationFixture fixture;
  const std::optional<arv::player::ItemID> stone =
      fixture.item_registry.find_item("arvane:stone");
  ASSERT_TRUE(stone.has_value());
  ASSERT_TRUE(fixture.player->inventory.set_slot(
      0,
      arv::player::ItemStack{stone.value(), 1U}));
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.damage_applied);
  const arv::entity::EntityHealth* health = fixture.world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 9.0F);
}

TEST(PlayerInteractionTests, EntityAttackAppliesKnockbackOnDamage) {
  CombatValidationFixture fixture;
  fixture.config.base_horizontal_knockback = 5.0F;
  fixture.config.base_vertical_knockback = 1.5F;
  fixture.config.airborne_vertical_knockback = 1.5F;
  fixture.config.vertical_knockback_cap = 2.0F;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.damage_applied);
  EXPECT_TRUE(result.knockback_applied);
  const arv::entity::EntityVelocity* velocity =
      fixture.world.find_entity_velocity(target);
  ASSERT_NE(velocity, nullptr);
  EXPECT_GT(velocity->linear.x, 0.0F);
  EXPECT_FLOAT_EQ(velocity->linear.y, 1.5F);
}

TEST(PlayerInteractionTests, EntityAttackScalesKnockbackResistance) {
  CombatValidationFixture fixture;
  fixture.config.base_horizontal_knockback = 6.0F;
  fixture.config.base_vertical_knockback = 2.0F;
  fixture.config.airborne_vertical_knockback = 2.0F;
  fixture.config.vertical_knockback_cap = 3.0F;
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  components.health = arv::entity::AuthoredHealthComponent{10.0F};
  components.knockback_resistance =
      arv::entity::AuthoredKnockbackResistanceComponent{0.5F};
  arv::world::EntityCreateInfo info;
  info.position = {2.8F, 0.0F, 0.0F};
  info.authored_components = components;
  const arv::EntityID target = fixture.world.create_entity(std::move(info));

  const arv::server::AttackResult result = fixture.attack(target, 1U, 10U);

  ASSERT_TRUE(result.accepted);
  ASSERT_TRUE(result.damage_applied);
  const arv::entity::EntityVelocity* velocity =
      fixture.world.find_entity_velocity(target);
  ASSERT_NE(velocity, nullptr);
  EXPECT_FLOAT_EQ(velocity->linear.x, 3.0F);
  EXPECT_FLOAT_EQ(velocity->linear.y, 1.0F);
}

TEST(PlayerInteractionTests, EntityAttackAppliesPlayerVelocityWithoutTeleporting) {
  CombatValidationFixture fixture;
  fixture.config.base_horizontal_knockback = 5.0F;
  fixture.config.base_vertical_knockback = 1.5F;
  fixture.config.airborne_vertical_knockback = 1.5F;
  fixture.config.vertical_knockback_cap = 2.0F;
  arv::player::PlayerState& target_player =
      fixture.world.ensure_player(arv::PlayerID{2});
  target_player.position = {2.8F, 0.0F, 0.0F};
  target_player.velocity = {};
  target_player.grounded = true;
  const std::optional<arv::EntityID> target =
      fixture.world.entity_for_player(arv::PlayerID{2});
  ASSERT_TRUE(target.has_value());

  const arv::server::AttackResult result = fixture.attack(target.value(), 1U, 10U);

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.damage_applied);
  EXPECT_TRUE(result.knockback_applied);
  ASSERT_TRUE(result.knockback_player_id.has_value());
  EXPECT_EQ(result.knockback_player_id.value(), (arv::PlayerID{2}));
  EXPECT_EQ(result.knockback_velocity, (arv::Vec3f{5.0F, 1.5F, 0.0F}));
  EXPECT_EQ(result.knockback_control_suppression_ticks, 2U);
  EXPECT_EQ(target_player.position, (arv::Vec3f{2.8F, 0.0F, 0.0F}));
  EXPECT_FLOAT_EQ(target_player.velocity.x, 5.0F);
  EXPECT_FLOAT_EQ(target_player.velocity.y, 1.5F);
  EXPECT_EQ(target_player.knockback_control_suppression_ticks, 2U);
  const arv::entity::EntityVelocity* velocity =
      fixture.world.find_entity_velocity(target.value());
  ASSERT_NE(velocity, nullptr);
  EXPECT_FLOAT_EQ(velocity->linear.x, 5.0F);
  EXPECT_FLOAT_EQ(velocity->linear.y, 1.5F);
  const arv::entity::EntityHealth* health =
      fixture.world.find_entity_health(target.value());
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 17.0F);
}

TEST(PlayerInteractionTests, SprintKnockbackBonusRequiresResetAfterHit) {
  CombatValidationFixture fixture;
  fixture.config.existing_horizontal_retention = 0.5F;
  fixture.config.base_horizontal_knockback = 4.0F;
  fixture.config.sprint_horizontal_bonus = 2.0F;
  fixture.config.base_vertical_knockback = 1.0F;
  fixture.config.airborne_vertical_knockback = 1.0F;
  fixture.config.vertical_knockback_cap = 2.0F;
  fixture.combat_state.was_sprinting_forward_last_tick = true;
  fixture.combat_state.sprint_knockback_ready = true;
  const arv::EntityID target = fixture.create_target({2.8F, 0.0F, 0.0F});

  const arv::server::AttackResult first = fixture.attack(target, 1U, 10U);

  ASSERT_TRUE(first.accepted);
  ASSERT_TRUE(first.damage_applied);
  const arv::entity::EntityVelocity* velocity =
      fixture.world.find_entity_velocity(target);
  ASSERT_NE(velocity, nullptr);
  EXPECT_FLOAT_EQ(velocity->linear.x, 6.0F);
  EXPECT_FALSE(fixture.combat_state.sprint_knockback_ready);

  const arv::server::AttackResult second = fixture.attack(target, 2U, 100U);

  ASSERT_TRUE(second.accepted);
  ASSERT_TRUE(second.damage_applied);
  EXPECT_FLOAT_EQ(velocity->linear.x, 7.0F);
}

TEST(PlayerInteractionTests, PrimaryButtonSwingsRightHandAndSecondarySwingsLeftHand) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const std::optional<arv::EntityID> player_entity =
      server.voxel_world().entity_for_player(player_state.player_id);
  ASSERT_TRUE(player_entity.has_value());

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();
  const arv::entity::EntityAnimationState* animation =
      server.voxel_world().find_entity_animation_state(player_entity.value());
  ASSERT_NE(animation, nullptr);
  EXPECT_NE(find_active_animation(*animation, "hit_right"), nullptr);
  EXPECT_EQ(find_active_animation(*animation, "hit_left"), nullptr);

  ASSERT_TRUE(server.voxel_world().set_entity_animation(player_entity.value(), ""));
  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();
  animation = server.voxel_world().find_entity_animation_state(player_entity.value());
  ASSERT_NE(animation, nullptr);
  EXPECT_NE(find_active_animation(*animation, "hit_left"), nullptr);
  EXPECT_EQ(find_active_animation(*animation, "hit_right"), nullptr);
}

TEST(PlayerInteractionTests, SecondaryPlaceFlowsThroughServerAuthorityAndConsumesHeldStack) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{2, 1, 0}}), stone);
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(player_state.inventory.slot(0)->count, 1U);
}

TEST(PlayerInteractionTests, StairPlacementResolvesFacingAndHalfState) {
  const arv::voxel::BlockRegistry block_registry = make_stair_placement_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID east_top =
      block_registry.find_variant("arvane:stone_stairs[facing=east,half=top]").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  set_test_player_held_item(server, item_registry, "arvane:stone_stairs");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{2, 1, 0}}), east_top);
}

TEST(PlayerInteractionTests, PrimaryBreakRemovesTargetBlockThroughServerAuthority) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, PrimaryBreakAccumulatesProgressBeforeRemovingHardBlock) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.hand_speed_multiplier = 1.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true, .primary_held = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), dirt);

  client.fixed_tick(server.tick_index());
  EXPECT_TRUE(client.block_break_progress().active);
  EXPECT_EQ(client.block_break_progress().block, (arv::BlockPos{{3, 1, 0}}));
  EXPECT_GT(client.block_break_progress().progress, 0.0F);
  server.tick();

  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, MatchingToolBreaksMineableBlocksFasterThanHand) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  arv::server::BlockBreakBehaviorRegistry break_behaviors =
      make_base_block_break_behaviors();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 1.0F;
  start_info.block_breaking.hand_speed_multiplier = 0.2F;
  start_info.block_breaking.wrong_tool_speed_multiplier = 0.2F;
  start_info.block_breaking.behavior_id =
      std::string{arv::base::ids::MineableTagsBlockBreakBehaviorID};
  start_info.block_break_behaviors = &break_behaviors;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true, .primary_held = true});
  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }
  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), stone);

  set_test_player_held_item(server, item_registry, "arvane:wood_pickaxe");
  for (int tick = 0; tick < 4; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
  }

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
}

TEST(PlayerInteractionTests, BlockDropsRequireCorrectToolWhenConfiguredByMineableTags) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  arv::server::BlockBreakBehaviorRegistry break_behaviors =
      make_base_block_break_behaviors();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  start_info.block_breaking.behavior_id =
      std::string{arv::base::ids::MineableTagsBlockBreakBehaviorID};
  start_info.block_break_behaviors = &break_behaviors;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
  EXPECT_TRUE(server.dropped_item_stacks().empty());
}

TEST(PlayerInteractionTests, GenericBlockBreakingIgnoresBaseMineableTagsWithoutBehavior) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), arv::voxel::AirBlockVariantID);
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(
      server.dropped_item_stacks().front().stack.item,
      item_registry.find_item("arvane:stone").value());
}

TEST(PlayerInteractionTests, BlockDropsSpawnWhenBrokenWithMatchingToolOrNoRequiredTool) {
  const arv::voxel::BlockRegistry block_registry = make_timed_break_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  arv::server::BlockBreakBehaviorRegistry break_behaviors =
      make_base_block_break_behaviors();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  start_info.block_breaking.behavior_id =
      std::string{arv::base::ids::MineableTagsBlockBreakBehaviorID};
  start_info.block_break_behaviors = &break_behaviors;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, stone));
  set_test_player_held_item(server, item_registry, "arvane:wood_pickaxe");
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(
      server.dropped_item_stacks().front().stack.item,
      item_registry.find_item("arvane:stone").value());

  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  const std::vector<arv::server::DroppedItemStack> drops = server.dropped_item_stacks();
  ASSERT_EQ(drops.size(), 2U);
  const arv::player::ItemID dirt_item = item_registry.find_item("arvane:dirt").value();
  EXPECT_NE(
      std::find_if(
          drops.begin(),
          drops.end(),
          [dirt_item](const arv::server::DroppedItemStack& drop) {
            return drop.stack.item == dirt_item;
          }),
      drops.end());
}

TEST(PlayerInteractionTests, BlockDropOverridesCanDropAnotherItemOrNothing) {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:dirt"));
  arv::voxel::BlockDefinition grass =
      arv::voxel::make_full_cube_definition("arvane:grass_block");
  grass.drops.kind = arv::voxel::BlockDropKind::Item;
  grass.drops.item_name = "arvane:dirt";
  definitions.push_back(std::move(grass));
  arv::voxel::BlockDefinition glass =
      arv::voxel::make_full_cube_definition(
          "arvane:glass",
          arv::voxel::RenderCategory::Transparent);
  glass.drops.kind = arv::voxel::BlockDropKind::None;
  definitions.push_back(std::move(glass));

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(definitions));
  ASSERT_TRUE(block_registry_result);
  const arv::voxel::BlockRegistry block_registry =
      std::move(block_registry_result.value());
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID grass_variant =
      block_registry.find_variant("arvane:grass_block").value();
  const arv::voxel::BlockVariantID glass_variant =
      block_registry.find_variant("arvane:glass").value();
  const arv::player::ItemID dirt_item =
      item_registry.find_item("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.block_breaking.base_seconds_per_hardness = 0.0F;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, grass_variant));
  prepare_test_player(server, item_registry);
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(server.dropped_item_stacks().front().stack.item, dirt_item);

  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, glass_variant));
  prepare_test_player(server, item_registry);
  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.dropped_item_stacks().size(), 1U);
}

TEST(PlayerInteractionTests, BucketPicksUpSourceWaterAndBecomesWaterBucket) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerChunkStreamingConfig streaming_config;
  streaming_config.simulation_distance = 4;
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.player_tuning = arv::player::default_player_tuning();
  start_info.fluid_definition = water;
  start_info.chunk_streaming = streaming_config;
  start_info.fluid_containers = make_test_fluid_containers();
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().place_fluid_source({{3, 1, 0}}, water));
  set_test_player_held_item(server, item_registry, "arvane:bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{3, 1, 0}}).empty());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:water_bucket").value());
}

TEST(PlayerInteractionTests, WaterBucketPlacesSourceWaterAndBecomesBucket) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.fluid_containers = make_test_fluid_containers();
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:bucket").value());
}

TEST(PlayerInteractionTests, WaterBucketReplacesPartialWaterWithSource) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.fluid_containers = make_test_fluid_containers();
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  ASSERT_TRUE(server.voxel_world().set_fluid(
      {{2, 1, 0}},
      arv::voxel::make_flowing_fluid(water, 4U),
      water));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(
      player_state.inventory.slot(0)->item,
      item_registry.find_item("arvane:bucket").value());
}

TEST(PlayerInteractionTests, PlacedWaterFlowsThroughActiveServerSimulation) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.fluid_containers = make_test_fluid_containers();
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  set_test_player_held_item(server, item_registry, "arvane:water_bucket");

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();
  ASSERT_TRUE(server.voxel_world().get_fluid({{2, 1, 0}}).source());

  bool flowed = false;
  for (arv::u32 tick = 0; tick < 300U; ++tick) {
    client.fixed_tick(server.tick_index());
    server.tick();
    const arv::voxel::FluidCell below = server.voxel_world().get_fluid({{2, 0, 0}});
    if (below.type == water.type && below.falling()) {
      flowed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  EXPECT_TRUE(flowed);
}

TEST(PlayerInteractionTests, RegionDenyPreventsWorldModification) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{3, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  arv::world::Region spawn;
  spawn.name = "spawn";
  spawn.world = arv::server::DefaultWorldID;
  spawn.priority = 100;
  spawn.shape.min = {-16.0F, -4.0F, -16.0F};
  spawn.shape.max = {16.0F, 16.0F, 16.0F};
  spawn.core.can_modify_world = arv::player::TriState::Deny;
  ASSERT_NE(server.voxel_world().regions().add_region(std::move(spawn)).value, 0U);

  client.submit_input(arv::client::InputFrame{.primary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{3, 1, 0}}), dirt);
}

TEST(PlayerInteractionTests, PlacementOutsideConfiguredReachIsRejected) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID dirt = block_registry.find_variant("arvane:dirt").value();
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  (void)server.voxel_world().ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(server.voxel_world().set_block({{10, 1, 0}}, dirt));
  prepare_test_player(server, item_registry);

  client.submit_input(arv::client::InputFrame{.secondary_pressed = true});
  client.fixed_tick(server.tick_index());
  server.tick();

  EXPECT_EQ(server.voxel_world().get_block({{9, 1, 0}}), arv::voxel::AirBlockVariantID);
  const arv::player::PlayerState& player_state = connected_player(server);
  ASSERT_TRUE(player_state.inventory.slot(0).has_value());
  EXPECT_EQ(player_state.inventory.slot(0)->count, 2U);
}

TEST(PlayerInteractionTests, InventorySlotClicksMutateAuthoritativeServerInventory) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  ASSERT_TRUE(player_state.inventory.set_slot(0, arv::player::ItemStack{stone_item, 8}));
  ASSERT_TRUE(player_state.inventory.clear_slot(1));

  const auto pick = server.apply_player_inventory_click(
      player_state.player_id,
      0,
      arv::player::InventoryClickButton::Primary);
  EXPECT_EQ(pick.kind, arv::player::InventoryOperationResultKind::Success);
  EXPECT_FALSE(player_state.inventory.slot(0).has_value());
  ASSERT_TRUE(player_state.inventory_cursor_stack.has_value());
  EXPECT_EQ(player_state.inventory_cursor_stack->count, 8U);

  const auto place = server.apply_player_inventory_click(
      player_state.player_id,
      1,
      arv::player::InventoryClickButton::Primary);
  EXPECT_EQ(place.kind, arv::player::InventoryOperationResultKind::Success);
  EXPECT_FALSE(player_state.inventory_cursor_stack.has_value());
  ASSERT_TRUE(player_state.inventory.slot(1).has_value());
  EXPECT_EQ(player_state.inventory.slot(1)->item, stone_item);
  EXPECT_EQ(player_state.inventory.slot(1)->count, 8U);
}

TEST(PlayerInteractionTests, DroppingInventoryCursorRecordsServerDroppedStack) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  player_state.inventory_cursor_stack = arv::player::ItemStack{stone_item, 3};

  const std::optional<arv::player::ItemStack> dropped =
      server.drop_player_inventory_cursor(player_state.player_id);

  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->item, stone_item);
  EXPECT_EQ(dropped->count, 3U);
  EXPECT_FALSE(player_state.inventory_cursor_stack.has_value());
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(server.dropped_item_stacks().front().world_id, player_state.world_id);
  EXPECT_EQ(server.dropped_item_stacks().front().position, player_state.position);
  EXPECT_EQ(server.dropped_item_stacks().front().stack, dropped.value());
}

TEST(PlayerInteractionTests, DroppingOneFromInventoryCursorLeavesRemainderCarried) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  ASSERT_TRUE(server.start({
      arv::server::ServerMode::Integrated,
      arv::DefaultBaseTps,
      {"in-process", 0},
      &block_registry,
      &item_registry}));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  player_state.inventory_cursor_stack = arv::player::ItemStack{stone_item, 3};

  const std::optional<arv::player::ItemStack> dropped =
      server.drop_one_from_player_inventory_cursor(player_state.player_id);

  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->item, stone_item);
  EXPECT_EQ(dropped->count, 1U);
  ASSERT_TRUE(player_state.inventory_cursor_stack.has_value());
  EXPECT_EQ(player_state.inventory_cursor_stack->item, stone_item);
  EXPECT_EQ(player_state.inventory_cursor_stack->count, 2U);
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);
  EXPECT_EQ(server.dropped_item_stacks().front().world_id, player_state.world_id);
  EXPECT_EQ(server.dropped_item_stacks().front().position, player_state.position);
  EXPECT_EQ(server.dropped_item_stacks().front().stack, dropped.value());
}

TEST(PlayerInteractionTests, NearbyDroppedItemsArePickedUpAndRemoved) {
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  auto transports = arv::net::make_in_process_transport_pair();

  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.base_tps = arv::DefaultBaseTps;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &block_registry;
  start_info.item_registry = &item_registry;
  start_info.dropped_items.pickup_delay_seconds = 0.0F;
  ASSERT_TRUE(server.start(start_info));
  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  complete_integrated_handshake(server, client);

  arv::player::PlayerState& player_state = connected_player(server);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  player_state.position = {4.0F, 5.0F, 6.0F};
  for (arv::usize slot = 0; slot < player_state.inventory.slot_count(); ++slot) {
    ASSERT_TRUE(player_state.inventory.clear_slot(slot));
  }

  (void)server.voxel_world().create_dropped_item(
      arv::player::ItemStack{stone_item, 3},
      player_state.position,
      {},
      0.0F);
  ASSERT_EQ(server.dropped_item_stacks().size(), 1U);

  server.tick();

  EXPECT_TRUE(server.dropped_item_stacks().empty());
  const arv::player::PlayerState& updated_player_state = connected_player(server);
  ASSERT_TRUE(updated_player_state.inventory.slot(0).has_value());
  EXPECT_EQ(updated_player_state.inventory.slot(0)->item, stone_item);
  EXPECT_EQ(updated_player_state.inventory.slot(0)->count, 3U);
}
