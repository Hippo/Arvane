set(ARV_LANG_HEADERS
  include/arv/lang/language.hpp
)

add_library(arv_lang STATIC
  src/language.cpp
)
target_include_directories(arv_lang PUBLIC include)
target_link_libraries(arv_lang PUBLIC arv_core)
target_link_libraries(arv_lang PRIVATE yaml-cpp)
