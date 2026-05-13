add_library(cpp_minimal_plugin SHARED src/cpp_minimal.cpp)
target_link_libraries(cpp_minimal_plugin PRIVATE Arvane::PluginSDK)
