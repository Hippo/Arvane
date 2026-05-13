add_library(scheduler_plugin SHARED src/scheduler.cpp)
target_link_libraries(scheduler_plugin PRIVATE Arvane::PluginSDK)
