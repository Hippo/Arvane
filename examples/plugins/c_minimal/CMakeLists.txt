add_library(c_minimal_plugin SHARED src/c_minimal.c)
target_link_libraries(c_minimal_plugin PRIVATE Arvane::SDK)
