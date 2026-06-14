#include "arv/client/block_outline_target.hpp"

#include "arv/client/chunk_collision_query.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace {

void ensure_loaded_chunk(arv::world::WorldRuntime& world, arv::ChunkSectionCoord coord) {
  (void)world.ensure_chunk(coord);
}

} // namespace

TEST(ClientBlockOutlineTargetTests, FindsSolidRaycastTarget) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{3, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  const std::optional<arv::client::BlockOutlineTarget> target =
      arv::client::find_block_outline_target(
          query,
          registry,
          {0.5F, 1.62F, 0.5F},
          {1.0F, 0.0F, 0.0F},
          5.0F);

  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->block, (arv::BlockPos{{3, 1, 0}}));
  EXPECT_EQ(target->variant, stone);
}

TEST(ClientBlockOutlineTargetTests, IgnoresMissingChunkAirFallback) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const std::array<arv::protocol::ClientboundChunkSectionData, 0> sections{};
  const arv::client::ChunkSectionCollisionQuery query(
      std::span<const arv::protocol::ClientboundChunkSectionData>{sections},
      registry);

  const std::optional<arv::client::BlockOutlineTarget> target =
      arv::client::find_block_outline_target(
          query,
          registry,
          {0.5F, 1.5F, 0.5F},
          {1.0F, 0.0F, 0.0F},
          5.0F);

  EXPECT_FALSE(target.has_value());
}
