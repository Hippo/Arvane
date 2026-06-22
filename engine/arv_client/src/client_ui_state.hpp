#pragma once

#include "arv/client/chat_history.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/client/player_menu.hpp"
#include "arv/core/text_edit.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/text_variables.hpp"
#include "arv/text/variables.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::client {

class ClientUiState {
public:
  void set_tab_menu_visible(bool visible) noexcept;
  [[nodiscard]] bool tab_menu_visible() const noexcept;

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
  [[nodiscard]] bool chat_open() const noexcept;

  void toggle_inventory_if_allowed(const player::ResolvedPermissions& permissions);
  void open_inventory() noexcept;
  void set_player_menu_tab(PlayerMenuTab tab) noexcept;
  void set_player_menu_settings_section(PlayerMenuSettingsSection section) noexcept;
  void scroll_player_menu_settings(f32 wheel_y, usize row_count, f32 row_height, f32 viewport_height) noexcept;
  void set_player_menu_active_settings_slider(std::string action_id);
  void clear_player_menu_active_settings_slider() noexcept;
  void set_player_menu_key_bind_capture(PlayerMenuKeyBindCapture capture);
  void clear_player_menu_key_bind_capture() noexcept;
  void clear_player_menu_search() noexcept;
  void select_crafting_category(std::string category_id);
  void select_crafting_recipe(std::string recipe_id);
  void open_chat();
  void open_chat_with_draft(std::string draft);
  void close_chat() noexcept;
  void close_ui() noexcept;
  void set_language(const arv::text::LanguageCatalog* language) noexcept;
  void set_max_chat_history_messages(usize max_messages);
  void clear_text_variables();
  void apply_text_variable_update(
      protocol::ClientboundTextVariableUpdate update,
      std::optional<PlayerID> local_player_id);
  void add_chat_message(std::string message);
  void add_chat_message(protocol::ClientboundChatMessage message);
  void suggest_chat_command(std::string command);
  [[nodiscard]] std::optional<std::string> commit_chat_draft_to_history();
  void request_url_confirmation(std::string url);
  void cancel_url_confirmation() noexcept;
  void acknowledge_url_open_unavailable();
  [[nodiscard]] const std::optional<std::string>& pending_url_confirmation() const noexcept;
  [[nodiscard]] std::optional<std::string> consume_pending_clipboard_text();

  [[nodiscard]] std::optional<std::string> handle_chat_input(
      const InputFrame& input,
      bool was_chat_open);
  void handle_player_menu_input(const InputFrame& input, bool was_inventory_open);

  [[nodiscard]] const std::string& chat_draft() const noexcept;
  [[nodiscard]] usize chat_draft_cursor() const noexcept;
  [[nodiscard]] usize chat_draft_selection_start() const noexcept;
  [[nodiscard]] usize chat_draft_selection_end() const noexcept;
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept;
  [[nodiscard]] std::span<const ChatHistoryEntry> chat_history() const noexcept;
  [[nodiscard]] const arv::text::TextVariableStore& text_variables() const noexcept;

private:
  void reset_chat_history_navigation() noexcept;
  void recall_previous_chat_history();
  void recall_next_chat_history();
  void assign_chat_draft(std::string draft);

  bool inventory_open_{};
  PlayerMenuTab player_menu_tab_{PlayerMenuTab::Inventory};
  PlayerMenuSettingsSection player_menu_settings_section_{PlayerMenuSettingsSection::Overview};
  f32 player_menu_settings_scroll_y_{};
  std::string player_menu_active_settings_slider_{};
  std::optional<PlayerMenuKeyBindCapture> player_menu_key_bind_capture_{};
  arv::core::TextEditBuffer player_menu_search_{};
  std::string selected_crafting_category_id_{};
  std::string selected_crafting_recipe_id_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
  arv::core::TextEditBuffer chat_draft_{};
  std::vector<std::string> submitted_chat_history_{};
  std::optional<usize> chat_history_cursor_{};
  std::string uncommitted_chat_draft_{};
  std::vector<std::string> chat_messages_{};
  std::vector<ChatHistoryEntry> chat_history_{};
  std::optional<std::string> pending_url_confirmation_{};
  std::optional<std::string> pending_clipboard_text_{};
  arv::text::TextVariableStore text_variables_{};
  const arv::text::LanguageCatalog* language_{};
  usize max_chat_history_messages_{DefaultMaxChatHistoryMessages};
  u64 next_received_chat_sequence_{1U};
};

} // namespace arv::client
