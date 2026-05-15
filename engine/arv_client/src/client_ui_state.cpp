#include "client_ui_state.hpp"

#include "arv/core/string.hpp"
#include "arv/text/language.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <utility>

namespace arv::client {
namespace {

constexpr usize MaxChatDraftBytes = 256U;
constexpr usize MaxSubmittedChatHistory = 100U;

[[nodiscard]] arv::text::TextVariableBinding text_binding_from_protocol(
    protocol::TextVariableBinding binding) noexcept {
  switch (binding) {
    case protocol::TextVariableBinding::Resolved:
      return arv::text::TextVariableBinding::Resolved;
    case protocol::TextVariableBinding::Live:
      return arv::text::TextVariableBinding::Live;
  }
  return arv::text::TextVariableBinding::Resolved;
}

template<typename T>
void trim_vector_to_limit(std::vector<T>& values, usize limit) {
  if (values.size() <= limit) {
    return;
  }
  const usize remove_count = values.size() - limit;
  values.erase(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(remove_count));
}

} // namespace

void ClientUiState::set_tab_menu_visible(bool visible) noexcept {
  tab_menu_visible_ = visible;
}

bool ClientUiState::tab_menu_visible() const noexcept {
  return tab_menu_visible_;
}

bool ClientUiState::inventory_open() const noexcept {
  return inventory_open_;
}

bool ClientUiState::chat_open() const noexcept {
  return chat_open_;
}

void ClientUiState::toggle_inventory_if_allowed(
    const player::ResolvedPermissions& permissions) {
  if (!permissions.core.can_open_inventory) {
    return;
  }
  inventory_open_ = !inventory_open_;
  if (inventory_open_) {
    chat_open_ = false;
    reset_chat_history_navigation();
  }
}

void ClientUiState::open_chat() {
  chat_open_ = true;
  inventory_open_ = false;
  reset_chat_history_navigation();
}

void ClientUiState::open_chat_with_draft(std::string draft) {
  chat_open_ = true;
  inventory_open_ = false;
  assign_chat_draft(std::move(draft));
  reset_chat_history_navigation();
}

void ClientUiState::close_chat() noexcept {
  chat_open_ = false;
  chat_draft_.value.clear();
  chat_draft_.cursor = 0U;
  chat_draft_.selection_anchor = 0U;
  reset_chat_history_navigation();
  pending_url_confirmation_.reset();
}

void ClientUiState::close_ui() noexcept {
  inventory_open_ = false;
  chat_open_ = false;
  chat_draft_.value.clear();
  chat_draft_.cursor = 0U;
  chat_draft_.selection_anchor = 0U;
  reset_chat_history_navigation();
  pending_url_confirmation_.reset();
}

void ClientUiState::set_language(const arv::text::LanguageCatalog* language) noexcept {
  language_ = language;
}

void ClientUiState::set_max_chat_history_messages(usize max_messages) {
  max_chat_history_messages_ = std::max<usize>(1U, max_messages);
  trim_vector_to_limit(chat_messages_, max_chat_history_messages_);
  trim_vector_to_limit(chat_history_, max_chat_history_messages_);
}

void ClientUiState::clear_text_variables() {
  text_variables_.clear();
}

void ClientUiState::apply_text_variable_update(
    protocol::ClientboundTextVariableUpdate update,
    std::optional<PlayerID> local_player_id) {
  if (update.reset) {
    text_variables_.clear();
  }
  for (protocol::TextVariableUpdateEntry& entry : update.entries) {
    if (entry.scope == protocol::TextVariableScope::Global) {
      if (entry.value.has_value()) {
        text_variables_.set_global(
            std::move(entry.name),
            std::move(entry.value.value()),
            text_binding_from_protocol(entry.binding));
      } else {
        (void)text_variables_.clear_global(entry.name);
      }
      continue;
    }
    if (!local_player_id.has_value()) {
      continue;
    }
    if (entry.value.has_value()) {
      text_variables_.set_player(
          local_player_id.value(),
          std::move(entry.name),
          std::move(entry.value.value()),
          text_binding_from_protocol(entry.binding));
    } else {
      (void)text_variables_.clear_player_value(local_player_id.value(), entry.name);
    }
  }
}

void ClientUiState::add_chat_message(std::string message) {
  if (message.empty()) {
    return;
  }
  protocol::ClientboundChatMessage packet;
  packet.message_id = 0U;
  packet.channel = "arvane:system";
  packet.kind = protocol::ChatMessageKind::System;
  packet.message = arv::text::plain_rich_text(message);
  add_chat_message(std::move(packet));
}

void ClientUiState::add_chat_message(protocol::ClientboundChatMessage message) {
  const std::string plain = arv::text::to_plain_text(message.message);
  if (plain.empty()) {
    return;
  }
  chat_messages_.push_back(plain);
  trim_vector_to_limit(chat_messages_, max_chat_history_messages_);
  chat_history_.push_back(
      {message.message_id,
       std::move(message.channel),
       message.kind,
       std::move(message.message),
       next_received_chat_sequence_++,
       std::chrono::steady_clock::now(),
       message.message_id == 0U});
  trim_vector_to_limit(chat_history_, max_chat_history_messages_);
}

void ClientUiState::suggest_chat_command(std::string command) {
  open_chat_with_draft(std::move(command));
}

std::optional<std::string> ClientUiState::commit_chat_draft_to_history() {
  std::string trimmed = trim_ascii_copy(chat_draft_.value);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  submitted_chat_history_.push_back(trimmed);
  trim_vector_to_limit(submitted_chat_history_, MaxSubmittedChatHistory);
  reset_chat_history_navigation();
  return trimmed;
}

void ClientUiState::request_url_confirmation(std::string url) {
  pending_url_confirmation_ = std::move(url);
  open_chat();
}

void ClientUiState::cancel_url_confirmation() noexcept {
  pending_url_confirmation_.reset();
}

void ClientUiState::acknowledge_url_open_unavailable() {
  pending_url_confirmation_.reset();
  add_chat_message(arv::text::text_or_fallback(
      language_,
      "ui.chat.open_url_unavailable",
      "Opening external URLs is not implemented in this build."));
}

const std::optional<std::string>& ClientUiState::pending_url_confirmation() const noexcept {
  return pending_url_confirmation_;
}

std::optional<std::string> ClientUiState::consume_pending_clipboard_text() {
  std::optional<std::string> text = std::move(pending_clipboard_text_);
  pending_clipboard_text_.reset();
  return text;
}

std::optional<std::string> ClientUiState::handle_chat_input(
    const InputFrame& input,
    bool was_chat_open) {
  if (!chat_open_) {
    return std::nullopt;
  }

  if (was_chat_open) {
    for (const arv::core::TextEditInput& edit_input : input.text_edit_events) {
      if (edit_input.command == arv::core::TextEditCommand::MoveUp) {
        recall_previous_chat_history();
        continue;
      }
      if (edit_input.command == arv::core::TextEditCommand::MoveDown) {
        recall_next_chat_history();
        continue;
      }

      const arv::core::TextEditResult result =
          chat_draft_.handle_edit(edit_input, MaxChatDraftBytes);
      if (result.clipboard_text.has_value()) {
        pending_clipboard_text_ = result.clipboard_text.value();
      }
      if (result.text_changed) {
        reset_chat_history_navigation();
      }
    }
  }
  if (was_chat_open && input.text_backspace_pressed) {
    if (chat_draft_.backspace()) {
      reset_chat_history_navigation();
    }
  }
  if (was_chat_open && !input.text_input.empty()) {
    if (chat_draft_.insert(input.text_input, MaxChatDraftBytes)) {
      reset_chat_history_navigation();
    }
  }
  if (was_chat_open && input.text_submit_pressed) {
    std::optional<std::string> submitted = commit_chat_draft_to_history();
    chat_draft_.value.clear();
    chat_draft_.cursor = 0U;
    chat_draft_.selection_anchor = 0U;
    chat_open_ = false;
    return submitted;
  }
  return std::nullopt;
}

const std::string& ClientUiState::chat_draft() const noexcept {
  return chat_draft_.value;
}

usize ClientUiState::chat_draft_cursor() const noexcept {
  return chat_draft_.cursor;
}

usize ClientUiState::chat_draft_selection_start() const noexcept {
  return chat_draft_.selection_start();
}

usize ClientUiState::chat_draft_selection_end() const noexcept {
  return chat_draft_.selection_end();
}

std::span<const std::string> ClientUiState::chat_messages() const noexcept {
  return chat_messages_;
}

std::span<const ChatHistoryEntry> ClientUiState::chat_history() const noexcept {
  return chat_history_;
}

const arv::text::TextVariableStore& ClientUiState::text_variables() const noexcept {
  return text_variables_;
}

void ClientUiState::reset_chat_history_navigation() noexcept {
  chat_history_cursor_.reset();
  uncommitted_chat_draft_.clear();
}

void ClientUiState::recall_previous_chat_history() {
  if (submitted_chat_history_.empty()) {
    return;
  }
  if (!chat_history_cursor_.has_value()) {
    uncommitted_chat_draft_ = chat_draft_.value;
    chat_history_cursor_ = submitted_chat_history_.size();
  }
  if (chat_history_cursor_.value() == 0U) {
    assign_chat_draft(submitted_chat_history_.front());
    return;
  }
  --chat_history_cursor_.value();
  assign_chat_draft(submitted_chat_history_[chat_history_cursor_.value()]);
}

void ClientUiState::recall_next_chat_history() {
  if (!chat_history_cursor_.has_value()) {
    return;
  }
  if (chat_history_cursor_.value() + 1U < submitted_chat_history_.size()) {
    ++chat_history_cursor_.value();
    assign_chat_draft(submitted_chat_history_[chat_history_cursor_.value()]);
    return;
  }
  assign_chat_draft(std::move(uncommitted_chat_draft_));
  reset_chat_history_navigation();
}

void ClientUiState::assign_chat_draft(std::string draft) {
  chat_draft_ = arv::core::TextEditBuffer{std::move(draft)};
}

} // namespace arv::client
