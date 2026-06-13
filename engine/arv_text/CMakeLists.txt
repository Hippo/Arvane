set(ARV_TEXT_HEADERS
  include/arv/text/chat_message.hpp
  include/arv/text/chat_format.hpp
  include/arv/text/language.hpp
  include/arv/text/rich_text.hpp
  include/arv/text/variables.hpp
)

add_library(arv_text STATIC
  src/chat_message.cpp
  src/chat_format.cpp
  src/language.cpp
  src/rich_text.cpp
  src/variables.cpp
)
target_include_directories(arv_text PUBLIC include)
target_link_libraries(arv_text PUBLIC arv_core)
if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  target_link_libraries(arv_text PUBLIC arv_io_data)
else()
  target_link_libraries(arv_text PUBLIC arv_io)
endif()
