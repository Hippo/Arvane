#pragma once

#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/crafting/crafting.hpp"
#include "arv/crafting/smelting.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/player/container.hpp"
#include "arv/player/item.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/text/language.hpp"
#include "arv/ui/core/ui.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/space_definition.hpp"
#include "arv/world/world_generation.hpp"

#include <string>
#include <string_view>

namespace arv::desktop {

struct DesktopLoadedAssets {
  voxel::BlockRegistry block_registry{};
  u64 block_registry_hash{};
  player::ItemRegistry item_registry{};
  u64 item_registry_hash{};
  crafting::CraftingRecipeRegistry crafting_registry{};
  crafting::SmeltingRuleRegistry smelting_registry{};
  player::ContainerTypeRegistry container_types{};
  player::ContainerUiLayoutRegistry container_layouts{};
  client::ItemAnimationLibrary item_animation_library{};
  entity::EntityRegistry entity_registry{};
  entity::VehicleDefinitionRegistry vehicle_registry{};
  arv::text::LanguageCatalog language{};
  asset::BlockTextureAtlas block_atlas{};
  render::FluidRenderData fluid_render_data{};
  asset::TextureAtlas item_atlas{};
  ui::UiImageData crosshair_image{};
  client::EntityRenderAssets entity_render_assets{};
  client::ParticleAssets particle_assets{};
  asset::FontSystem font_system{};
  ui::UiFontStacks ui_font_stacks{};
  world::WorldGenerationConfig default_generation_config{};
  world::SpaceDefinitionRegistry space_definitions{};
};

struct DesktopAssetLoadError {
  std::string category{};
  std::string message{};
};

using DesktopLoadedAssetsResult = Expected<DesktopLoadedAssets, DesktopAssetLoadError>;
using DesktopAssetValidationResult = Expected<void, DesktopAssetLoadError>;

struct DesktopAssetLoadOptions {
  bool validate_behavior_ids{true};
  bool load_worldgen{true};
};

[[nodiscard]] DesktopLoadedAssetsResult load_desktop_assets(
    asset::ResourceManager& resources,
    render::RenderCommandQueue& renderer,
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const ui::UiTheme& theme,
    const ui::UiDisplayMetrics& ui_metrics,
    const voxel::BlockBehaviorRegistry& block_behaviors,
    const player::ItemBehaviorRegistry& item_behaviors,
    const world::EntityGoalRegistry& entity_goals,
    DesktopAssetLoadOptions options = {});

[[nodiscard]] DesktopAssetValidationResult validate_desktop_assets_against_registry_snapshot(
    std::string_view snapshot_json,
    const DesktopLoadedAssets& assets);

} // namespace arv::desktop
