#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/skins.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
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
  std::string animation_name{};
  Seconds animation_time_seconds{};
  Vec3f head_rotation_degrees{};
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
  ~ClientRuntime();

  ClientRuntime(ClientRuntime&&) noexcept;
  ClientRuntime& operator=(ClientRuntime&&) noexcept;

  ClientRuntime(const ClientRuntime&) = delete;
  ClientRuntime& operator=(const ClientRuntime&) = delete;

  void set_player_tuning(player::PlayerTuning tuning) noexcept;
  void set_local_identity(LocalPlayerIdentity identity);
  [[nodiscard]] const LocalPlayerIdentity& local_identity() const noexcept;
  net::ConnectResult connect_integrated();
  net::ConnectResult connect(net::NetworkAddress address);
  void submit_input(InputFrame input);
  void set_chunk_view_distance(i32 view_distance) noexcept;
  void fixed_tick(TickIndex tick_index);
  void fixed_tick(TickIndex tick_index, const physics::ICollisionQuery* prediction_query);
  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const;
  [[nodiscard]] net::ConnectionState connection_state() const noexcept;
  [[nodiscard]] const CameraShell& camera() const noexcept;
  CameraShell& camera() noexcept;
  [[nodiscard]] const Vec3f& player_position() const noexcept;
  [[nodiscard]] u8 selected_hotbar_slot() const noexcept;
  [[nodiscard]] const std::string& game_mode_id() const noexcept;
  [[nodiscard]] const player::ResolvedPermissions& resolved_permissions() const noexcept;
  [[nodiscard]] std::optional<PlayerID> local_player_id() const noexcept;
  [[nodiscard]] std::optional<EntityNetID> local_entity_id() const noexcept;
  [[nodiscard]] const protocol::BlockBreakProgress& block_break_progress() const noexcept;
  [[nodiscard]] TickIndex world_initial_tick() const noexcept;
  [[nodiscard]] TickRate server_tick_rate() const noexcept;
  [[nodiscard]] PlayerPredictionDebugStats prediction_debug_stats() const noexcept;
  [[nodiscard]] bool inventory_open() const noexcept;
  [[nodiscard]] bool chat_open() const noexcept;
  [[nodiscard]] const std::string& chat_draft() const noexcept;
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept;
  void close_chat() noexcept;
  void add_chat_message(std::string message);
  [[nodiscard]] bool tab_menu_visible() const noexcept;
  [[nodiscard]] std::vector<protocol::ChunkSectionData> drain_received_chunk_section_data();
  [[nodiscard]] std::vector<protocol::ChunkSectionUnload> drain_received_chunk_section_unloads();
  [[nodiscard]] std::vector<protocol::BlockUpdateBatch> drain_received_block_updates();
  [[nodiscard]] const player::Inventory& inventory() const noexcept;
  [[nodiscard]] const std::optional<player::ItemStack>& inventory_cursor_stack() const noexcept;
  [[nodiscard]] u32 inventory_revision() const noexcept;
  [[nodiscard]] bool has_authoritative_inventory() const noexcept;
  net::NetworkResult request_inventory_slot_click(
      usize slot_index,
      player::InventoryClickButton button);
  net::NetworkResult request_inventory_drop_cursor(bool one_item);
  net::NetworkResult request_inventory_close();
  [[nodiscard]] usize replicated_entity_count() const noexcept;
  [[nodiscard]] const ReplicatedEntityMirror* find_replicated_entity(
      EntityNetID entity_id) const noexcept;
  [[nodiscard]] std::vector<ReplicatedEntityMirror> replicated_entity_mirrors() const;
  [[nodiscard]] usize dropped_item_count() const noexcept;
  [[nodiscard]] const DroppedItemMirror* find_dropped_item(
      EntityNetID drop_id) const noexcept;
  [[nodiscard]] std::vector<DroppedItemMirror> dropped_item_mirrors() const;
  [[nodiscard]] const protocol::PlayerProfileInfo* profile_info_for_player(
      PlayerID player_id) const noexcept;
  [[nodiscard]] std::vector<protocol::PlayerProfileInfo> player_profile_infos() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace arv::client
