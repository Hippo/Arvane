add_library(service_consumer_plugin SHARED src/service_consumer.cpp)
target_link_libraries(service_consumer_plugin PRIVATE Arvane::SDK)
