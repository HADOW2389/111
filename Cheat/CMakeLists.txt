cmake_minimum_required(VERSION 3.22)
project(Cheat CXX)

# ── ImGui sources (кладём в Cheat/imgui/) ───────────────────────────────────
set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_impl_win32.cpp
    imgui/imgui_impl_dx11.cpp
)

add_library(Cheat SHARED
    dllmain.cpp
    features/esp.h
    features/aimbot.h
    sdk/memory.h
    sdk/offsets.h
    sdk/types.h
    ${IMGUI_SOURCES}
)

target_include_directories(Cheat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_compile_definitions(Cheat PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(Cheat PRIVATE
    d3d11
    dxgi
    user32
    kernel32
)

if(MSVC)
    target_compile_options(Cheat PRIVATE
        /W3
        /O2
        /EHsc
        /MT
        /GS-          # отключаем stack canary (чуть меньше детектов)
    )
    set_target_properties(Cheat PROPERTIES
        OUTPUT_NAME "cheat"
        SUFFIX ".dll"
    )
endif()
