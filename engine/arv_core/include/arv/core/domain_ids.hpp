#pragma once

#include "arv/core/id.hpp"
#include "arv/core/types.hpp"

namespace arv {

struct SessionTag {};
struct PlayerTag {};
struct WorldTag {};
struct EntityTag {};
struct EntityNetTag {};
struct EntityTypeTag {};
struct InventoryTag {};

using SessionID = ID<SessionTag, u64>;
using PlayerID = ID<PlayerTag, u64>;
using WorldID = ID<WorldTag, u32>;
using EntityID = ID<EntityTag, u64>;
using EntityNetID = ID<EntityNetTag, u32>;
using EntityTypeID = ID<EntityTypeTag, u32>;
using InventoryID = ID<InventoryTag, u32>;

} // namespace arv
