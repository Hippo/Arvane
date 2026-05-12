#pragma once

#include "arv/command/server_command_services.hpp"

namespace arv::base {

using OnlinePlayerCommandTarget = command::OnlinePlayerCommandTarget;
using OnlinePlayerByIDFn = command::OnlinePlayerByIDFn;
using OnlinePlayerByNameFn = command::OnlinePlayerByNameFn;
using TeleportPlayerFn = command::TeleportPlayerFn;
using ServerCommandServices = command::ServerCommandServices;

} // namespace arv::base
