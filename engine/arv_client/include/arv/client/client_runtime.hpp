#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/chat_history.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/particles.hpp"
#include "arv/client/vehicle_debug.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/world_time.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::text {
class LanguageCatalog;
class TextVariableStore;
} // namespace arv::text

namespace arv::entity {
class VehicleDefinitionRegistry;
} // namespace arv::entity

namespace arv::client {

struct ReplicatedPassengerMirror {
  EntityNetID passenger_id{};
  std::string seat_id{};
  bool controls_vehicle{};
};

struct ReplicatedEntityMirror {
  EntityNetID entity_id{};
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f render_position{};
  Vec3f interpolation_start_position{};
  Quatf previous_rotation{};
  Quatf rotation{};
  Quatf render_rotation{};
  Quatf interpolation_start_rotation{};
  u64 last_server_tick{};
  u64 last_transform_change_server_tick{};
  Seconds interpolation_elapsed_seconds{};
  Seconds interpolation_duration_seconds{};
  std::optional<PlayerID> owning_player{};
  std::vector<protocol::EntityAnimationPlayback> animations{};
  Vec3f head_rotation_degrees{};
  bool has_health{};
  f32 health_current{};
  f32 health_max{};
  bool dead{};
  bool invulnerable{};
  std::optional<EntityNetID> parent_rideable{};
  std::string parent_seat_id{};
  bool controls_parent_vehicle{};
  std::vector<ReplicatedPassengerMirror> passengers{};
};

struct DroppedItemMirror {
  EntityNetID drop_id{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f render_position{};
  Vec3f interpolation_start_position{};
  player::ItemStack stack{};
  u64 last_server_tick{};
  u64 last_position_change_server_tick{};
  Seconds interpolation_elapsed_seconds{};
  Seconds interpolation_duration_seconds{};
};

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
  net::ConnectResult connect_integrated();
  net::ConnectResult connect(net::NetworkAddress address);
  void disconnect();
  [[nodiscard]] std::optional<std::string> take_disconnect_reason();
  [[nodiscard]] std::optional<std::string> disconnect_reason() const;
  void submit_input(InputFrame input);
  void set_chunk_view_distance(i32 view_distance) noexcept;
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
  [[nodiscard]] std::optional<protocol::ClientboundWorldTime> take_world_time_update();
  [[nodiscard]] const player::Inventory& inventory() const noexcept;
  [[nodiscard]] const std::optional<player::ItemStack>& inventory_cursor_stack() const noexcept;
  [[nodiscard]] u32 inventory_revision() const noexcept;
  [[nodiscard]] bool has_authoritative_inventory() const noexcept;
  net::NetworkResult request_inventory_slot_click(
      usize slot_index,
      player::InventoryClickButton button);
  net::NetworkResult request_inventory_drop_cursor(bool one_item);
  net::NetworkResult request_inventory_close();
  net::NetworkResult request_attack_entity(EntityNetID target_entity);
  net::NetworkResult request_ride_entity(EntityNetID rideable_id, std::string seat_id = {});
  net::NetworkResult request_dismount();
  net::NetworkResult send_vehicle_action(protocol::ServerboundVehicleAction action);
  net::NetworkResult send_vehicle_move(const protocol::ServerboundVehicleMove& move);
  [[nodiscard]] std::optional<VehicleDebugTelemetry> vehicle_debug_telemetry() const;
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

} // namespace arv::client
