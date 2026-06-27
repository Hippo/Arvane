#pragma once

#include "arv/client/chunk_interest.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/particles.hpp"
#include "arv/core/time.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/crafting.hpp"
#include "arv/protocol/world_time.hpp"

#include "client_chunk_inbox.hpp"
#include "client_connection.hpp"
#include "client_dropped_item_mirror.hpp"
#include "client_entity_mirror.hpp"
#include "client_input_runtime.hpp"
#include "client_inventory_runtime.hpp"
#include "client_network_messages.hpp"
#include "client_protocol_send.hpp"
#include "client_skin_runtime.hpp"
#include "client_ui_state.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <optional>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::entity {
class VehicleDefinitionRegistry;
} // namespace arv::entity

namespace arv::client::detail {

struct ClientPackSyncDownload {
  protocol::PackBundleDescriptor descriptor{};
  std::vector<u8> bytes{};
  HashSet<u32> received_chunks{};
  u64 received_bytes{};
  u32 chunk_count{};
};

struct ClientPackSyncState {
  void clear() {
    expected_registry_hash.clear();
    registry_snapshot_payload.clear();
    required_bundle_hashes.clear();
    loaded_bundle_hashes.clear();
    loaded_bundle_paths.clear();
    downloads.clear();
  }

  std::string expected_registry_hash{};
  std::string registry_snapshot_payload{};
  HashSet<std::string> required_bundle_hashes{};
  std::vector<std::string> loaded_bundle_hashes{};
  std::vector<std::filesystem::path> loaded_bundle_paths{};
  HashMap<std::string, ClientPackSyncDownload> downloads{};
};

struct ClientRecipeBookRuntime {
  void clear() {
    unlocked_recipe_ids.clear();
    last_craft_result.reset();
  }

  [[nodiscard]] bool is_unlocked(std::string_view recipe_id) const noexcept {
    return std::find(
               unlocked_recipe_ids.begin(),
               unlocked_recipe_ids.end(),
               recipe_id) != unlocked_recipe_ids.end();
  }

  void apply_snapshot(protocol::ClientboundRecipeBookSnapshot snapshot) {
    unlocked_recipe_ids = std::move(snapshot.unlocked_recipe_ids);
    std::sort(unlocked_recipe_ids.begin(), unlocked_recipe_ids.end());
    unlocked_recipe_ids.erase(
        std::unique(unlocked_recipe_ids.begin(), unlocked_recipe_ids.end()),
        unlocked_recipe_ids.end());
  }

  void apply_unlocks(protocol::ClientboundRecipeUnlocks unlocks) {
    unlocked_recipe_ids.insert(
        unlocked_recipe_ids.end(),
        std::make_move_iterator(unlocks.unlocked_recipe_ids.begin()),
        std::make_move_iterator(unlocks.unlocked_recipe_ids.end()));
    std::sort(unlocked_recipe_ids.begin(), unlocked_recipe_ids.end());
    unlocked_recipe_ids.erase(
        std::unique(unlocked_recipe_ids.begin(), unlocked_recipe_ids.end()),
        unlocked_recipe_ids.end());
  }

  std::vector<std::string> unlocked_recipe_ids{};
  std::optional<protocol::ClientboundCraftRecipeResult> last_craft_result{};
};

struct ClientRuntimeContext {
  ClientConnectionRuntime& connection;
  ClientProtocolSender& sender;
  LocalPlayerIdentity& identity;

  LocalPlayerController& player_controller;
  ClientChunkInterest& chunk_interest;

  ClientUiState& ui;
  ClientChunkInbox& chunks;
  ClientInventoryRuntime& inventory;
  ClientInputRuntime& input;
  ClientEntityMirrorStore& entities;
  ClientDroppedItemMirrorStore& dropped_items;
  ClientSkinRuntime& skins;
  ParticleSystem& particles;
  ClientPackSyncState& pack_sync;
  ClientRecipeBookRuntime& recipe_book;
  std::optional<protocol::ClientboundWorldTime>& world_time_update;
  std::string& world_environment_sky;
  bool& space_environment;
  std::vector<protocol::ClientboundSpaceBodyView>& space_bodies;
  ClientWorldTransitionStatus& world_transition_status;
  Seconds& world_transition_fade_remaining_seconds;
  const arv::text::LanguageCatalog* language{};
  const arv::entity::VehicleDefinitionRegistry* vehicle_registry{};
  const physics::ICollisionQuery* collision_query{};
  Seconds tick_seconds{};
};

} // namespace arv::client::detail
