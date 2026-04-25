#pragma once

#include "arv/core/id.hpp"
#include "arv/core/types.hpp"

namespace arv {

struct SessionTag {};
struct PlayerTag {};
struct WorldTag {};

using SessionID = ID<SessionTag, u64>;
using PlayerID = ID<PlayerTag, u64>;
using WorldID = ID<WorldTag, u32>;

} // namespace arv
