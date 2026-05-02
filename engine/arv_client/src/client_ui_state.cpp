#include "client_ui_state.hpp"

namespace arv::client {
namespace {

constexpr usize MaxChatDraftBytes = 256U;
constexpr usize MaxChatMessages = 64U;

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

void ClientUiState::close_ui() noexcept {
  inventory_open_ = false;
  chat_open_ = false;
  chat_draft_.clear();
}

void ClientUiState::handle_chat_input(const InputFrame& input, bool was_chat_open) {
  if (!chat_open_) {
    return;
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
    if (!chat_draft_.empty()) {
      chat_messages_.emplace_back(chat_draft_);
      if (chat_messages_.size() > MaxChatMessages) {
        chat_messages_.erase(chat_messages_.begin());
      }
      chat_draft_.clear();
    }
    chat_open_ = false;
  }
}

const std::string& ClientUiState::chat_draft() const noexcept {
  return chat_draft_;
}

std::span<const std::string> ClientUiState::chat_messages() const noexcept {
  return chat_messages_;
}

} // namespace arv::client
