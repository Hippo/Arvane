set(ARV_TEXT_HEADERS
  include/arv/text/chat_format.hpp
  include/arv/text/language.hpp
  include/arv/text/rich_text.hpp
)

add_library(arv_text STATIC
  src/chat_format.cpp
  src/language.cpp
  src/rich_text.cpp
)
target_include_directories(arv_text PUBLIC include)
target_link_libraries(arv_text PUBLIC arv_core)
target_link_libraries(arv_text PRIVATE arv_io)
