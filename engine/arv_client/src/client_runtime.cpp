#include "arv/client/client_runtime.hpp"

#include "arv/lang/language.hpp"

#include "client_frame_builder.hpp"
#include "client_runtime_internal.hpp"

#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] std::string tr(
    const lang::LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

} // namespace

struct ClientRuntime::Impl {
  explicit Impl(std::unique_ptr<net::ITransport> transport) : connection(std::move(transport)),
                                                              sender(connection),
                                                              identity(make_ephemeral_local_identity()) {
  }

  [[nodiscard]] detail::ClientRuntimeContext context(
      const physics::ICollisionQuery* collision_query,
      Seconds tick_seconds) noexcept {
    return {
        connection,
        sender,
        identity,
        player_controller,
        chunk_interest,
        ui,
        chunks,
        inventory,
        input,
        entities,
        dropped_items,
        skins,
        language,
        collision_query,
        tick_seconds};
  }

  void configure_language(const lang::LanguageCatalog* new_language) noexcept {
    language = new_language;
    connection.configure_language(new_language);
    ui.set_language(new_language);
  }

  void fixed_tick(TickIndex tick_index, const physics::ICollisionQuery* collision_query) {
    if (!connection.transport()) {
      return;
    }

    const bool was_in_game = connection.state() == net::ConnectionState::InGame;
    const Seconds tick_seconds = tick_duration_seconds(player_controller.server_tick_rate());

    detail::ClientRuntimeContext runtime = context(collision_query, tick_seconds);
    network.process_events(runtime);
    if (connection.tick_timeouts()) {
      clear_multiplayer_runtime();
    }

    if (was_in_game && connection.state() == net::ConnectionState::InGame) {
      input.send_fixed_tick_input(
          tick_index,
          player_controller,
          sender,
          collision_query,
          tick_seconds);
    }

    if (connection.state() == net::ConnectionState::InGame) {
      chunk_interest.set_world_id(player_controller.camera().world_id());
      chunk_interest.update_player_position(player_controller.player_position());
      if (chunk_interest.consume_interest_changed()) {
        (void)sender.send_chunk_interest_update(chunk_interest.build_interest_update());
      }
    }
  }

  void clear_multiplayer_runtime() {
    entities.clear();
    dropped_items.clear();
    chunks.clear();
    inventory.reset();
    player_controller.leave_world();
    chunk_interest.set_world_id(WorldID{});
    ui.close_ui();
  }

  detail::ClientConnectionRuntime connection;
  detail::ClientProtocolSender sender;
  LocalPlayerIdentity identity{};
  detail::ClientNetworkRouter network{};
  const lang::LanguageCatalog* language{};
  detail::ClientInputRuntime input{};
  LocalPlayerController player_controller{};
  ClientChunkInterest chunk_interest{};
  ClientUiState ui{};
  ClientChunkInbox chunks{};
  ClientInventoryRuntime inventory{};
  ClientEntityMirrorStore entities{};
  ClientDroppedItemMirrorStore dropped_items{};
  detail::ClientSkinRuntime skins{};
  u32 next_attack_sequence{1};
  std::vector<ClientAttackFeedback> attack_feedback{};
};

ClientRuntime::ClientRuntime(std::unique_ptr<net::ITransport> transport) : impl_(std::make_unique<Impl>(std::move(transport))) {
}

ClientRuntime::~ClientRuntime() = default;

ClientRuntime::ClientRuntime(ClientRuntime&&) noexcept = default;

ClientRuntime& ClientRuntime::operator=(ClientRuntime&&) noexcept = default;

void ClientRuntime::set_player_tuning(player::PlayerTuning tuning) noexcept {
  impl_->player_controller.set_player_tuning(tuning);
}

void ClientRuntime::set_movement_send_rate(f32 send_rate_hz) noexcept {
  impl_->input.set_movement_send_rate(send_rate_hz);
}

void ClientRuntime::set_local_identity(LocalPlayerIdentity identity) {
  impl_->identity = std::move(identity);
}

void ClientRuntime::configure_language(const lang::LanguageCatalog* language) noexcept {
  impl_->configure_language(language);
}

void ClientRuntime::configure_networking(ClientNetworkSettings settings) noexcept {
  impl_->connection.configure_timeouts(
      settings.server_keep_alive_timeout_seconds,
      settings.connection_timeout_seconds,
      settings.server_keep_alive_timeout_enabled);
}

void ClientRuntime::configure_chat(ClientChatSettings settings) {
  impl_->ui.set_max_chat_history_messages(settings.max_history_messages);
}

const LocalPlayerIdentity& ClientRuntime::local_identity() const noexcept {
  return impl_->identity;
}

net::ConnectResult ClientRuntime::connect_integrated() {
  return impl_->connection.connect_integrated(impl_->sender, impl_->identity);
}

net::ConnectResult ClientRuntime::connect(net::NetworkAddress address) {
  return impl_->connection.connect_remote(std::move(address));
}

void ClientRuntime::disconnect() {
  impl_->connection.disconnect_local(
      tr(impl_->language, "ui.connection.disconnected", "Disconnected."));
  impl_->clear_multiplayer_runtime();
}

std::optional<std::string> ClientRuntime::take_disconnect_reason() {
  return impl_->connection.take_disconnect_reason();
}

std::optional<std::string> ClientRuntime::disconnect_reason() const {
  return impl_->connection.disconnect_reason();
}

void ClientRuntime::submit_input(InputFrame input) {
  impl_->input.submit_input(
      std::move(input),
      impl_->ui,
      impl_->player_controller,
      impl_->sender,
      impl_->connection.state());
}

void ClientRuntime::set_chunk_view_distance(i32 view_distance) noexcept {
  impl_->chunk_interest.set_view_distance(view_distance);
}

void ClientRuntime::fixed_tick(TickIndex tick_index) {
  impl_->fixed_tick(tick_index, nullptr);
}

void ClientRuntime::fixed_tick(
    TickIndex tick_index,
    const physics::ICollisionQuery* collision_query) {
  impl_->fixed_tick(tick_index, collision_query);
}

void ClientRuntime::advance_entity_interpolation(Seconds delta_seconds) noexcept {
  impl_->entities.advance_interpolation(delta_seconds);
  impl_->dropped_items.advance_interpolation(delta_seconds);
}

render::FramePacket ClientRuntime::build_frame_packet(
    f32 interpolation_alpha,
    u32 viewport_width,
    u32 viewport_height) const {
  return build_client_frame_packet(
      impl_->player_controller.camera(),
      interpolation_alpha,
      viewport_width,
      viewport_height);
}

net::ConnectionState ClientRuntime::connection_state() const noexcept {
  return impl_->connection.state();
}

const CameraShell& ClientRuntime::camera() const noexcept {
  return impl_->player_controller.camera();
}

CameraShell& ClientRuntime::camera() noexcept {
  return impl_->player_controller.camera();
}

const Vec3f& ClientRuntime::player_position() const noexcept {
  return impl_->player_controller.player_position();
}

u8 ClientRuntime::selected_hotbar_slot() const noexcept {
  return impl_->player_controller.selected_hotbar_slot();
}

const std::string& ClientRuntime::game_mode_id() const noexcept {
  return impl_->player_controller.game_mode_id();
}

const player::ResolvedPermissions& ClientRuntime::resolved_permissions() const noexcept {
  return impl_->player_controller.resolved_permissions();
}

std::optional<PlayerID> ClientRuntime::local_player_id() const noexcept {
  return impl_->player_controller.player_id();
}

std::optional<EntityNetID> ClientRuntime::local_entity_id() const noexcept {
  return impl_->player_controller.local_entity_id();
}

const protocol::BlockBreakProgress& ClientRuntime::block_break_progress() const noexcept {
  return impl_->player_controller.block_break_progress();
}

TickIndex ClientRuntime::world_initial_tick() const noexcept {
  return impl_->player_controller.world_initial_tick();
}

TickRate ClientRuntime::server_tick_rate() const noexcept {
  return impl_->player_controller.server_tick_rate();
}

bool ClientRuntime::inventory_open() const noexcept {
  return impl_->ui.inventory_open();
}

bool ClientRuntime::chat_open() const noexcept {
  return impl_->ui.chat_open();
}

const std::string& ClientRuntime::chat_draft() const noexcept {
  return impl_->ui.chat_draft();
}

std::span<const std::string> ClientRuntime::chat_messages() const noexcept {
  return impl_->ui.chat_messages();
}

std::span<const ChatHistoryEntry> ClientRuntime::chat_history() const noexcept {
  return impl_->ui.chat_history();
}

void ClientRuntime::close_chat() noexcept {
  impl_->ui.close_chat();
}

void ClientRuntime::add_chat_message(std::string message) {
  impl_->ui.add_chat_message(std::move(message));
}

void ClientRuntime::suggest_chat_command(std::string command) {
  impl_->ui.suggest_chat_command(std::move(command));
}

void ClientRuntime::request_chat_url_confirmation(std::string url) {
  impl_->ui.request_url_confirmation(std::move(url));
}

void ClientRuntime::cancel_chat_url_confirmation() noexcept {
  impl_->ui.cancel_url_confirmation();
}

void ClientRuntime::acknowledge_chat_url_open_unavailable() {
  impl_->ui.acknowledge_url_open_unavailable();
}

const std::optional<std::string>& ClientRuntime::pending_chat_url_confirmation() const noexcept {
  return impl_->ui.pending_url_confirmation();
}

bool ClientRuntime::tab_menu_visible() const noexcept {
  return impl_->ui.tab_menu_visible();
}

std::vector<protocol::ClientboundChunkSectionData> ClientRuntime::drain_received_chunk_section_data() {
  return impl_->chunks.drain_chunk_section_data();
}

std::vector<protocol::ClientboundChunkSectionUnload> ClientRuntime::drain_received_chunk_section_unloads() {
  return impl_->chunks.drain_chunk_section_unloads();
}

std::vector<protocol::ClientboundBlockUpdateBatch> ClientRuntime::drain_received_block_updates() {
  return impl_->chunks.drain_block_updates();
}

const player::Inventory& ClientRuntime::inventory() const noexcept {
  return impl_->inventory.inventory();
}

const std::optional<player::ItemStack>& ClientRuntime::inventory_cursor_stack() const noexcept {
  return impl_->inventory.cursor_stack();
}

u32 ClientRuntime::inventory_revision() const noexcept {
  return impl_->inventory.revision();
}

bool ClientRuntime::has_authoritative_inventory() const noexcept {
  return impl_->inventory.has_authoritative_inventory();
}

net::NetworkResult ClientRuntime::request_inventory_slot_click(
    usize slot_index,
    player::InventoryClickButton button) {
  return impl_->inventory.request_slot_click(
      impl_->sender,
      slot_index,
      button,
      impl_->connection.state());
}

net::NetworkResult ClientRuntime::request_inventory_drop_cursor(bool one_item) {
  return impl_->inventory.request_drop_cursor(
      impl_->sender,
      one_item,
      impl_->connection.state());
}

net::NetworkResult ClientRuntime::request_inventory_close() {
  return impl_->inventory.request_close(
      impl_->sender,
      impl_->connection.state());
}

net::NetworkResult ClientRuntime::request_attack_entity(EntityNetID target_entity) {
  if (impl_->connection.state() != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }

  const u32 sequence = impl_->next_attack_sequence++;
  if (const ReplicatedEntityMirror* mirror = impl_->entities.find(target_entity);
      mirror != nullptr) {
    impl_->attack_feedback.push_back({target_entity, mirror->position, sequence});
  }
  return impl_->sender.send_attack_entity({target_entity, sequence});
}

std::vector<ClientAttackFeedback> ClientRuntime::drain_attack_feedback() {
  std::vector<ClientAttackFeedback> drained = std::move(impl_->attack_feedback);
  impl_->attack_feedback.clear();
  return drained;
}

usize ClientRuntime::replicated_entity_count() const noexcept {
  return impl_->entities.count();
}

const ReplicatedEntityMirror* ClientRuntime::find_replicated_entity(
    EntityNetID entity_id) const noexcept {
  return impl_->entities.find(entity_id);
}

std::vector<ReplicatedEntityMirror> ClientRuntime::replicated_entity_mirrors() const {
  return impl_->entities.mirrors();
}

usize ClientRuntime::dropped_item_count() const noexcept {
  return impl_->dropped_items.count();
}

const DroppedItemMirror* ClientRuntime::find_dropped_item(
    EntityNetID drop_id) const noexcept {
  return impl_->dropped_items.find(drop_id);
}

std::vector<DroppedItemMirror> ClientRuntime::dropped_item_mirrors() const {
  return impl_->dropped_items.mirrors();
}

const protocol::PlayerProfileInfo* ClientRuntime::profile_info_for_player(
    PlayerID player_id) const noexcept {
  return impl_->skins.profile_for_player(player_id);
}

std::vector<protocol::PlayerProfileInfo> ClientRuntime::player_profile_infos() const {
  return impl_->skins.profiles();
}

} // namespace arv::client
