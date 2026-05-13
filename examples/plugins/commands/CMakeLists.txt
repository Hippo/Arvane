add_library(commands_plugin SHARED src/commands.cpp)
target_link_libraries(commands_plugin PRIVATE Arvane::PluginSDK)
