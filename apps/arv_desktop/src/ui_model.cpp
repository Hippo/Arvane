#include "ui_model.hpp"

#include "asset_helpers.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace arv::desktop {
namespace {

[[nodiscard]] client::UiImageData missing_icon_for(
    const asset::TextureAtlas& item_atlas,
    u32 item_atlas_width,
    u32 item_atlas_height) {
  const asset::TextureAtlasEntry* missing_entry =
      item_atlas.find("arvane:texture/missing");
  return missing_entry == nullptr
             ? client::UiImageData{}
             : image_data_from_atlas_entry(
                   *missing_entry,
                   item_atlas_width,
                   item_atlas_height,
                   render::UiTextureSource::ItemAtlas);
}

[[nodiscard]] client::UiItemStackView stack_view_for_ui(
    const player::ItemStack& stack,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas,
    client::UiImageData missing_icon) {
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  const u32 block_atlas_width =
      block_atlas.atlas.pages.empty() ? 1U : block_atlas.atlas.pages.front().image.width;
  const u32 block_atlas_height =
      block_atlas.atlas.pages.empty() ? 1U : block_atlas.atlas.pages.front().image.height;

  client::UiItemStackView view;
  const player::ItemRecord* item = item_registry.item(stack.item);
  view.item_name = item == nullptr ? std::string{player::MissingItemName} : item->name;
  view.display_name = item == nullptr ? view.item_name : item->display_name;
  view.count = stack.count;
  view.texture_icon = missing_icon;
  view.render_type = item == nullptr ? player::ItemRenderType::Texture : item->render_type;
  if (item != nullptr && item->render_type == player::ItemRenderType::Texture) {
    const asset::TextureAtlasEntry* entry = item_atlas.find(item->texture_asset);
    if (entry != nullptr) {
      view.texture_icon = image_data_from_atlas_entry(
          *entry,
          item_atlas_width,
          item_atlas_height,
          render::UiTextureSource::ItemAtlas);
    }
  } else if (item != nullptr && item->render_type == player::ItemRenderType::Block &&
             item->render_block_variant.has_value()) {
    for (const voxel::BlockFace face : voxel::AllBlockFaces) {
      const voxel::BlockFaceRenderData* face_data =
          block_atlas.render_data.face(item->render_block_variant.value(), face);
      if (face_data == nullptr) {
        continue;
      }
      client::UiImageData face_image;
      face_image.valid = true;
      face_image.source = render::UiTextureSource::TerrainAtlas;
      face_image.page = face_data->page;
      face_image.uvs = normalized_uvs(face_data->uvs, block_atlas_width, block_atlas_height);
      view.block_faces[voxel::face_index(face)] = face_image;
    }
  }
  return view;
}

} // namespace

std::vector<client::UiItemStackView> inventory_slots_for_ui(
    const player::Inventory& inventory,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas) {
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  const client::UiImageData missing_icon =
      missing_icon_for(item_atlas, item_atlas_width, item_atlas_height);

  std::vector<client::UiItemStackView> slots;
  slots.reserve(inventory.slot_count());
  for (usize index = 0; index < inventory.slot_count(); ++index) {
    client::UiItemStackView view;
    const std::optional<player::ItemStack>& stack = inventory.slot(index);
    if (stack.has_value()) {
      view = stack_view_for_ui(*stack, item_registry, item_atlas, block_atlas, missing_icon);
    }
    slots.push_back(std::move(view));
  }
  return slots;
}

std::optional<client::UiItemStackView> item_stack_for_ui(
    const std::optional<player::ItemStack>& stack,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas) {
  if (!stack.has_value()) {
    return std::nullopt;
  }
  const u32 item_atlas_width =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.width;
  const u32 item_atlas_height =
      item_atlas.pages.empty() ? 1U : item_atlas.pages.front().image.height;
  return stack_view_for_ui(
      stack.value(),
      item_registry,
      item_atlas,
      block_atlas,
      missing_icon_for(item_atlas, item_atlas_width, item_atlas_height));
}

std::vector<client::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server) {
  std::vector<client::PlayerListEntryView> players;
  for (const SessionID session_id : server.sessions().session_ids()) {
    const server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }
    players.push_back({session->profile_name, std::nullopt});
  }
  return players;
}

} // namespace arv::desktop
