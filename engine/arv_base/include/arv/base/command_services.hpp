#pragma once

#include "arv/command/server_command_services.hpp"

namespace arv::base {

using OnlinePlayerCommandTarget = command::OnlinePlayerCommandTarget;
using Location = arv::Location;
using OnlinePlayerByIDFn = command::OnlinePlayerByIDFn;
using OnlinePlayerByNameFn = command::OnlinePlayerByNameFn;
using TeleportPlayerFn = command::TeleportPlayerFn;
using TeleportPlayerLocationFn = command::TeleportPlayerLocationFn;
using SetPlayerGameModeFn = command::SetPlayerGameModeFn;
using SetPlayerFlySpeedFn = command::SetPlayerFlySpeedFn;
using DefaultPlayerFlySpeedFn = command::DefaultPlayerFlySpeedFn;
using MaxPlayerFlySpeedFn = command::MaxPlayerFlySpeedFn;
using ServerCommandServices = command::ServerCommandServices;

} // namespace arv::base
