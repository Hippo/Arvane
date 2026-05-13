function(arvane_plugin_example target source)
  add_library(${target} SHARED ${source})
  target_link_libraries(${target} PRIVATE Arvane::SDK)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
endfunction()

arvane_plugin_example(arv_example_c_minimal_plugin c_minimal/src/c_minimal.c)
arvane_plugin_example(arv_example_cpp_minimal_plugin cpp_minimal/src/cpp_minimal.cpp)
arvane_plugin_example(arv_example_service_provider_plugin service_provider/src/service_provider.cpp)
arvane_plugin_example(arv_example_service_consumer_plugin service_consumer/src/service_consumer.cpp)
arvane_plugin_example(arv_example_commands_plugin commands/src/commands.cpp)
arvane_plugin_example(arv_example_scheduler_plugin scheduler/src/scheduler.cpp)
