#pragma once

#include "arv/client/chat_history.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/chat.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::client {

class ClientUiState {
public:
  void set_tab_menu_visible(bool visible) noexcept;
  [[nodiscard]] bool tab_menu_visible() const noexcept;

  [[nodiscard]] bool inventory_open() const noexcept;
  [[nodiscard]] bool chat_open() const noexcept;

  void toggle_inventory_if_allowed(const player::ResolvedPermissions& permissions);
  void open_chat();
  void open_chat_with_draft(std::string draft);
  void close_chat() noexcept;
  void close_ui() noexcept;
  void set_max_chat_history_messages(usize max_messages);
  void add_chat_message(std::string message);
  void add_chat_message(protocol::ClientboundChatMessage message);
  void suggest_chat_command(std::string command);
  void request_url_confirmation(std::string url);
  void cancel_url_confirmation() noexcept;
  void acknowledge_url_open_unavailable();
  [[nodiscard]] const std::optional<std::string>& pending_url_confirmation() const noexcept;

  [[nodiscard]] std::optional<std::string> handle_chat_input(
      const InputFrame& input,
      bool was_chat_open);

  [[nodiscard]] const std::string& chat_draft() const noexcept;
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept;
  [[nodiscard]] std::span<const ChatHistoryEntry> chat_history() const noexcept;

private:
  bool inventory_open_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
  std::string chat_draft_{};
  std::vector<std::string> chat_messages_{};
  std::vector<ChatHistoryEntry> chat_history_{};
  std::optional<std::string> pending_url_confirmation_{};
  usize max_chat_history_messages_{DefaultMaxChatHistoryMessages};
  u64 next_received_chat_sequence_{1U};
};

} // namespace arv::client
