set(ARV_DEDICATED_SOURCES
  src/main.cpp
)

add_executable(arv_dedicated ${ARV_DEDICATED_SOURCES})
target_link_libraries(arv_dedicated PRIVATE
  arv_core
  arv_io
  arv_net
  arv_server
)
