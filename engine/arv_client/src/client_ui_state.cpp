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

void erase_last_utf8_codepoint(std::string& text) {
  if (text.empty()) {
    return;
  }
  usize index = text.size() - 1U;
  while (index > 0U &&
         (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U) {
    --index;
  }
  text.erase(index);
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
  }
}

void ClientUiState::open_chat() {
  chat_open_ = true;
  inventory_open_ = false;
}

void ClientUiState::open_chat_with_draft(std::string draft) {
  chat_open_ = true;
  inventory_open_ = false;
  chat_draft_ = std::move(draft);
}

void ClientUiState::close_chat() noexcept {
  chat_open_ = false;
  chat_draft_.clear();
  pending_url_confirmation_.reset();
}

void ClientUiState::close_ui() noexcept {
  inventory_open_ = false;
  chat_open_ = false;
  chat_draft_.clear();
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

std::optional<std::string> ClientUiState::handle_chat_input(
    const InputFrame& input,
    bool was_chat_open) {
  if (!chat_open_) {
    return std::nullopt;
  }

  if (was_chat_open && input.text_backspace_pressed) {
    erase_last_utf8_codepoint(chat_draft_);
  }
  if (was_chat_open && !input.text_input.empty()) {
    const usize remaining =
        chat_draft_.size() >= MaxChatDraftBytes ? 0U : MaxChatDraftBytes - chat_draft_.size();
    chat_draft_.append(input.text_input.substr(0U, remaining));
  }
  if (was_chat_open && input.text_submit_pressed) {
    std::optional<std::string> submitted;
    std::string trimmed = trim_ascii_copy(chat_draft_);
    if (!trimmed.empty()) {
      submitted = std::move(trimmed);
    }
    chat_draft_.clear();
    chat_open_ = false;
    return submitted;
  }
  return std::nullopt;
}

const std::string& ClientUiState::chat_draft() const noexcept {
  return chat_draft_;
}

std::span<const std::string> ClientUiState::chat_messages() const noexcept {
  return chat_messages_;
}

std::span<const ChatHistoryEntry> ClientUiState::chat_history() const noexcept {
  return chat_history_;
}

} // namespace arv::client
