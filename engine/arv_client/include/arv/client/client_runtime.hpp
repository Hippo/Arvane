#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/chunk_interest.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/world_join.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace arv::client {

struct ReplicatedEntityMirror {
  EntityNetID entity_id{};
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf previous_rotation{};
  Quatf rotation{};
  u64 last_server_tick{};
  std::optional<PlayerID> owning_player{};
};

struct DroppedItemMirror {
  EntityNetID drop_id{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  player::ItemStack stack{};
  u64 last_server_tick{};
};

class ClientRuntime {
public:
  explicit ClientRuntime(std::unique_ptr<net::ITransport> transport);
  void set_player_tuning(player::PlayerTuning tuning) noexcept;
  net::ConnectResult connect_integrated();
  net::ConnectResult connect(net::NetworkAddress address);
  void submit_input(InputFrame input);
  void set_chunk_view_distance(i32 view_distance) noexcept;
  void fixed_tick(TickIndex tick_index);
  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const;
  [[nodiscard]] net::ConnectionState connection_state() const noexcept;
  [[nodiscard]] const CameraShell& camera() const noexcept;
  CameraShell& camera() noexcept;
  [[nodiscard]] u8 selected_hotbar_slot() const noexcept;
  [[nodiscard]] const std::string& game_mode_id() const noexcept;
  [[nodiscard]] const player::ResolvedPermissions& resolved_permissions() const noexcept;
  [[nodiscard]] const protocol::BlockBreakProgress& block_break_progress() const noexcept;
  [[nodiscard]] bool inventory_open() const noexcept {
    return inventory_open_;
  }
  [[nodiscard]] bool chat_open() const noexcept {
    return chat_open_;
  }
  [[nodiscard]] const std::string& chat_draft() const noexcept {
    return chat_draft_;
  }
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept {
    return chat_messages_;
  }
  [[nodiscard]] bool tab_menu_visible() const noexcept {
    return tab_menu_visible_;
  }
  [[nodiscard]] std::vector<protocol::ChunkData> drain_received_chunk_data();
  [[nodiscard]] std::vector<protocol::ChunkUnload> drain_received_chunk_unloads();
  [[nodiscard]] std::vector<protocol::BlockUpdateBatch> drain_received_block_updates();
  [[nodiscard]] const player::Inventory& inventory() const noexcept {
    return inventory_;
  }
  [[nodiscard]] const std::optional<player::ItemStack>& inventory_cursor_stack() const noexcept {
    return inventory_cursor_stack_;
  }
  [[nodiscard]] u32 inventory_revision() const noexcept {
    return inventory_revision_;
  }
  [[nodiscard]] bool has_authoritative_inventory() const noexcept {
    return has_authoritative_inventory_;
  }
  net::NetworkResult request_inventory_slot_click(
      usize slot_index,
      player::InventoryClickButton button);
  net::NetworkResult request_inventory_drop_cursor(bool one_item);
  net::NetworkResult request_inventory_close();
  [[nodiscard]] usize replicated_entity_count() const noexcept {
    return replicated_entities_.size();
  }
  [[nodiscard]] const ReplicatedEntityMirror* find_replicated_entity(
      EntityNetID entity_id) const noexcept;
  [[nodiscard]] std::vector<ReplicatedEntityMirror> replicated_entity_mirrors() const;
  [[nodiscard]] usize dropped_item_count() const noexcept {
    return dropped_items_.size();
  }
  [[nodiscard]] const DroppedItemMirror* find_dropped_item(
      EntityNetID drop_id) const noexcept;
  [[nodiscard]] std::vector<DroppedItemMirror> dropped_item_mirrors() const;

private:
  net::NetworkResult send_client_hello();
  net::NetworkResult send_chunk_interest_update();
  net::NetworkResult send_inventory_action(protocol::InventoryActionRequest request);
  void handle_network_event(net::NetworkEvent event);
  void handle_message(const std::vector<u8>& packet);
  net::NetworkResult send_input_batch(TickIndex tick_index);
  void apply_world_join_result(const protocol::WorldJoinResult& result);
  void apply_world_leave(const protocol::WorldLeave& leave);
  void apply_entity_spawn(protocol::EntitySpawn spawn);
  void apply_entity_despawn(const protocol::EntityDespawn& despawn);
  void apply_entity_snapshot(const protocol::EntitySnapshot& snapshot);
  void apply_dropped_item_snapshot(const protocol::DroppedItemSnapshot& snapshot);
  void apply_inventory_update(protocol::InventoryUpdate update);
  void handle_chat_input(const InputFrame& input, bool was_chat_open);
  void close_ui() noexcept;

  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  LocalPlayerController player_controller_{};
  ClientChunkInterest chunk_interest_{};
  std::vector<net::NetworkEvent> inbound_events_;
  std::vector<protocol::ChunkData> received_chunk_data_;
  std::vector<protocol::ChunkUnload> received_chunk_unloads_;
  std::vector<protocol::BlockUpdateBatch> received_block_updates_;
  HashMap<EntityNetID, ReplicatedEntityMirror> replicated_entities_;
  HashMap<EntityNetID, DroppedItemMirror> dropped_items_;
  player::Inventory inventory_{player::DefaultPlayerInventorySlotCount};
  std::optional<player::ItemStack> inventory_cursor_stack_{};
  u32 inventory_revision_{};
  u32 next_inventory_action_id_{1};
  bool has_authoritative_inventory_{};
  bool inventory_open_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
  std::string chat_draft_{};
  std::vector<std::string> chat_messages_{};
};

} // namespace arv::client
