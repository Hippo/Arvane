#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/chat_history.hpp"
#include "arv/client/client_chunk_stream_packets.hpp"
#include "arv/client/dropped_item_mirror.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/particles.hpp"
#include "arv/client/player_menu.hpp"
#include "arv/client/replicated_entity_mirror.hpp"
#include "arv/client/vehicle_debug.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/crafting.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/protocol/world_time.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {
class LanguageCatalog;
class TextVariableStore;
} // namespace arv::text

namespace arv::entity {
class VehicleDefinitionRegistry;
} // namespace arv::entity

namespace arv::client {

struct ClientAttackFeedback {
  EntityNetID target_entity{};
  Vec3f target_position{};
  u32 sequence{};
};

struct ClientNetworkSettings {
  u32 server_keep_alive_timeout_seconds{30U};
  u32 connection_timeout_seconds{10U};
  bool server_keep_alive_timeout_enabled{true};
};

struct ClientChatSettings {
  usize max_history_messages{DefaultMaxChatHistoryMessages};
};

enum class ClientWorldTransitionPhase : u8 {
  None,
  LeavingWorld,
  EnteringWorld,
};

struct ClientWorldTransitionStatus {
  bool active{};
  ClientWorldTransitionPhase phase{ClientWorldTransitionPhase::None};
  WorldID world_id{};
  f32 fade_alpha{};
  std::string message{};
};

struct RemoteClientFixedTickResult {
  bool was_in_game{};
  bool entered_game{};
  TickIndex submitted_tick{};
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
  void configure_language(const arv::text::LanguageCatalog* language) noexcept;
  void configure_networking(ClientNetworkSettings settings) noexcept;
  void configure_chat(ClientChatSettings settings);
  [[nodiscard]] const LocalPlayerIdentity& local_identity() const noexcept;
  [[nodiscard]] const std::string& world_environment_sky() const noexcept;
  [[nodiscard]] bool space_environment() const noexcept;
  [[nodiscard]] std::span<const protocol::ClientboundSpaceBodyView> space_bodies() const noexcept;
  [[nodiscard]] const ClientWorldTransitionStatus& world_transition_status()
      const noexcept;
  net::ConnectResult connect_integrated();
  net::ConnectResult connect(net::NetworkAddress address);
  void disconnect();
  [[nodiscard]] std::optional<std::string> take_disconnect_reason();
  [[nodiscard]] std::optional<std::string> disconnect_reason() const;
  void submit_input(InputFrame input);
  void set_chunk_view_distance(i32 view_distance) noexcept;
  void poll_network();
  void fixed_tick(TickIndex tick_index);
  void fixed_tick(TickIndex tick_index, const physics::ICollisionQuery* collision_query);
  void advance_entity_interpolation(Seconds delta_seconds) noexcept;
  void configure_particles(const ParticleRegistry* registry) noexcept;
  [[nodiscard]] u32 spawn_particles(const ParticleBurstDesc& desc);
  void advance_particles(Seconds delta_seconds) noexcept;
  [[nodiscard]] ParticleStats particle_stats() const noexcept;
  void set_movement_send_rate(f32 send_rate_hz) noexcept;
  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const;
  [[nodiscard]] net::ConnectionState connection_state() const noexcept;
  [[nodiscard]] const CameraShell& camera() const noexcept;
  CameraShell& camera() noexcept;
  [[nodiscard]] const Vec3f& player_position() const noexcept;
  [[nodiscard]] const Vec3f& player_velocity() const noexcept;
  [[nodiscard]] bool player_grounded() const noexcept;
  [[nodiscard]] u8 selected_hotbar_slot() const noexcept;
  [[nodiscard]] const std::string& game_mode_id() const noexcept;
  [[nodiscard]] const player::ResolvedPermissions& resolved_permissions() const noexcept;
  [[nodiscard]] std::optional<PlayerID> local_player_id() const noexcept;
  [[nodiscard]] std::optional<EntityNetID> local_entity_id() const noexcept;
  [[nodiscard]] const protocol::BlockBreakProgress& block_break_progress() const noexcept;
  [[nodiscard]] TickIndex world_initial_tick() const noexcept;
  [[nodiscard]] TickRate server_tick_rate() const noexcept;
  void configure_vehicles(const entity::VehicleDefinitionRegistry* registry) noexcept;
  [[nodiscard]] bool inventory_open() const noexcept;
  [[nodiscard]] PlayerMenuTab player_menu_tab() const noexcept;
  [[nodiscard]] PlayerMenuSettingsSection player_menu_settings_section() const noexcept;
  [[nodiscard]] f32 player_menu_settings_scroll_y() const noexcept;
  [[nodiscard]] const std::string& player_menu_active_settings_slider() const noexcept;
  [[nodiscard]] const std::optional<PlayerMenuKeyBindCapture>&
  player_menu_key_bind_capture() const noexcept;
  [[nodiscard]] const std::string& player_menu_search() const noexcept;
  [[nodiscard]] usize player_menu_search_cursor() const noexcept;
  [[nodiscard]] usize player_menu_search_selection_start() const noexcept;
  [[nodiscard]] usize player_menu_search_selection_end() const noexcept;
  [[nodiscard]] const std::string& selected_crafting_category_id() const noexcept;
  [[nodiscard]] const std::string& selected_crafting_recipe_id() const noexcept;
  void set_player_menu_tab(PlayerMenuTab tab) noexcept;
  void set_player_menu_settings_section(PlayerMenuSettingsSection section) noexcept;
  void scroll_player_menu_settings(
      f32 wheel_y,
      usize row_count,
      f32 row_height,
      f32 viewport_height) noexcept;
  void set_player_menu_active_settings_slider(std::string action_id);
  void clear_player_menu_active_settings_slider() noexcept;
  void set_player_menu_key_bind_capture(PlayerMenuKeyBindCapture capture);
  void clear_player_menu_key_bind_capture() noexcept;
  void clear_player_menu_search() noexcept;
  void select_crafting_category(std::string category_id);
  void select_crafting_recipe(std::string recipe_id);
  [[nodiscard]] bool chat_open() const noexcept;
  [[nodiscard]] const std::string& chat_draft() const noexcept;
  [[nodiscard]] usize chat_draft_cursor() const noexcept;
  [[nodiscard]] usize chat_draft_selection_start() const noexcept;
  [[nodiscard]] usize chat_draft_selection_end() const noexcept;
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept;
  [[nodiscard]] std::span<const ChatHistoryEntry> chat_history() const noexcept;
  [[nodiscard]] const arv::text::TextVariableStore& text_variables() const noexcept;
  void close_chat() noexcept;
  void add_chat_message(std::string message);
  void add_chat_message(protocol::ClientboundChatMessage message);
  void suggest_chat_command(std::string command);
  net::NetworkResult send_chat_message(
      std::string message,
      std::optional<std::string> channel = std::nullopt);
  [[nodiscard]] std::optional<std::string> commit_chat_draft_to_history();
  void request_chat_url_confirmation(std::string url);
  void cancel_chat_url_confirmation() noexcept;
  void acknowledge_chat_url_open_unavailable();
  [[nodiscard]] const std::optional<std::string>& pending_chat_url_confirmation() const noexcept;
  [[nodiscard]] std::optional<std::string> consume_pending_clipboard_text();
  [[nodiscard]] bool tab_menu_visible() const noexcept;
  [[nodiscard]] std::vector<protocol::ClientboundChunkSectionData> drain_received_chunk_section_data();
  [[nodiscard]] std::vector<protocol::ClientboundChunkSectionUnload> drain_received_chunk_section_unloads();
  [[nodiscard]] std::vector<protocol::ClientboundBlockUpdateBatch> drain_received_block_updates();
  [[nodiscard]] ClientChunkStreamPackets drain_chunk_stream_packets();
  [[nodiscard]] std::optional<protocol::ClientboundWorldTime> take_world_time_update();
  [[nodiscard]] const player::Inventory& inventory() const noexcept;
  [[nodiscard]] const std::optional<player::ItemStack>& inventory_cursor_stack() const noexcept;
  [[nodiscard]] u32 inventory_revision() const noexcept;
  [[nodiscard]] bool has_authoritative_inventory() const noexcept;
  [[nodiscard]] std::span<const std::string> unlocked_recipe_ids() const noexcept;
  [[nodiscard]] bool recipe_unlocked(std::string_view recipe_id) const noexcept;
  [[nodiscard]] std::optional<protocol::ClientboundCraftRecipeResult>
  take_craft_recipe_result();
  [[nodiscard]] bool container_open() const noexcept;
  [[nodiscard]] const std::string& active_container_type() const noexcept;
  [[nodiscard]] const std::string& active_container_title() const noexcept;
  [[nodiscard]] const std::string& active_container_layout() const noexcept;
  [[nodiscard]] const player::Inventory& active_container_inventory() const noexcept;
  [[nodiscard]] std::optional<protocol::ContainerBlockSource>
  active_container_block_source() const noexcept;
  net::NetworkResult request_open_container_block(WorldID world_id, BlockPos block);
  net::NetworkResult request_container_slot_click(
      player::ContainerSlotSource source,
      usize slot_index,
      player::InventoryClickButton button);
  net::NetworkResult request_container_shift_transfer(
      player::ContainerSlotSource source,
      usize slot_index);
  net::NetworkResult request_inventory_slot_click(
      usize slot_index,
      player::InventoryClickButton button);
  net::NetworkResult request_inventory_shift_transfer(usize slot_index);
  net::NetworkResult request_inventory_drop_cursor(bool one_item);
  net::NetworkResult request_inventory_close();
  net::NetworkResult request_craft_recipe(std::string recipe_id, u16 count = 1);
  net::NetworkResult request_attack_entity(EntityNetID target_entity);
  net::NetworkResult request_ride_entity(EntityNetID rideable_id, std::string seat_id = {});
  net::NetworkResult request_dismount();
  net::NetworkResult send_vehicle_action(protocol::ServerboundVehicleAction action);
  net::NetworkResult send_vehicle_move(const protocol::ServerboundVehicleMove& move);
  [[nodiscard]] std::optional<VehicleDebugTelemetry> vehicle_debug_telemetry() const;
  [[nodiscard]] std::optional<EntityNetID> mounted_vehicle_id() const noexcept;
  [[nodiscard]] std::string_view mounted_seat_id() const noexcept;
  [[nodiscard]] bool mounted_controls_vehicle() const noexcept;
  [[nodiscard]] std::vector<ClientAttackFeedback> drain_attack_feedback();
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
  [[nodiscard]] std::span<const std::filesystem::path> multiplayer_pack_cache_paths() const noexcept;
  [[nodiscard]] const std::string& multiplayer_registry_snapshot_json() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] RemoteClientFixedTickResult advance_remote_client_fixed_tick(
    ClientRuntime& client,
    TickIndex& client_tick,
    const physics::ICollisionQuery* collision_query = nullptr);

} // namespace arv::client
