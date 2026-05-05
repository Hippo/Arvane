#pragma once

#include "arv/client/local_player_controller.hpp"
#include "arv/player/permissions.hpp"

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
  void add_chat_message(std::string message);

  void handle_chat_input(const InputFrame& input, bool was_chat_open);

  [[nodiscard]] const std::string& chat_draft() const noexcept;
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept;

private:
  bool inventory_open_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
  std::string chat_draft_{};
  std::vector<std::string> chat_messages_{};
};

} // namespace arv::client
