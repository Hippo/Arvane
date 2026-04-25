cmake_minimum_required(VERSION 3.19)
project(imgui)

set(IMGUI_HEADERS imconfig.h imgui.h imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h imgui_stdlib.h)
set(IMGUI_SOURCES imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_stdlib.cpp)
add_library(imgui STATIC ${IMGUI_HEADERS} ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})