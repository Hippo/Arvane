set(ARV_WORLD_HEADERS
  include/arv/world/fluid_solver.hpp
  include/arv/world/entity_goal_utils.hpp
  include/arv/world/entity_goal_registry.hpp
  include/arv/world/regions.hpp
  include/arv/world/world_lighting.hpp
  include/arv/world/world_generation.hpp
  include/arv/world/world_runtime.hpp
  include/arv/world/world_queries.hpp
  include/arv/world/world_time.hpp
)

add_library(arv_world STATIC
  src/entity_goal_utils.cpp
  src/entity_goal_registry.cpp
  src/fluid_solver.cpp
  src/regions.cpp
  src/world_lighting.cpp
  src/world_generation.cpp
  src/world_time.cpp
  src/world_runtime_internal.cpp
  src/world_runtime.cpp
  src/world_chunk_store.cpp
  src/world_chunk_snapshots.cpp
  src/world_block_mutation.cpp
  src/world_dirty_tracking.cpp
  src/world_tick_queues.cpp
  src/world_fluid_runtime.cpp
  src/world_lighting_runtime.cpp
  src/world_entity_store.cpp
  src/world_entity_components.cpp
  src/world_entity_damage.cpp
  src/world_entity_simulation.cpp
  src/world_player_store.cpp
  src/world_region_runtime.cpp
)
target_include_directories(arv_world PUBLIC include)
target_link_libraries(arv_world PUBLIC arv_core arv_entity arv_physics arv_player arv_voxel EnTT::EnTT)
target_link_libraries(arv_world PRIVATE arv_io FastNoise2)
