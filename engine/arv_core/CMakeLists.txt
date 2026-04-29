set(ARV_CORE_HEADERS
  include/arv/core/coordinates.hpp
  include/arv/core/domain_ids.hpp
  include/arv/core/expected.hpp
  include/arv/core/fixed_timestep.hpp
  include/arv/core/id.hpp
  include/arv/core/log.hpp
  include/arv/core/math.hpp
  include/arv/core/thread_roles.hpp
  include/arv/core/time.hpp
  include/arv/core/types.hpp
)

add_library(arv_core STATIC
  src/log.cpp
  src/thread_roles.cpp
)
target_include_directories(arv_core PUBLIC include)
target_link_libraries(arv_core PUBLIC unordered_dense spdlog::spdlog)
