option(ARVANE_BUILD_FIRST_PARTY_PLUGINS "Build first-party external plugins" ON)

if(ARVANE_BUILD_FIRST_PARTY_PLUGINS)
  add_subdirectory(arv_mason)
endif()
