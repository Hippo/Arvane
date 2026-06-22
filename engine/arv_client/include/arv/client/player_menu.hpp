#pragma once

#include "arv/core/types.hpp"

#include <string>

namespace arv::client {

enum class PlayerMenuTab : u8 {
  Inventory,
  Crafting,
  Settings,
  ExitGame,
};

enum class PlayerMenuSettingsSection : u8 {
  Overview,
  Render,
  KeyBinds,
  Controls,
  Interface,
};

struct PlayerMenuKeyBindCapture {
  std::string action_name{};
  u8 slot{};
};

} // namespace arv::client
