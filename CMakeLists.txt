cmake_minimum_required(VERSION 3.20)
project(PubgCheat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cheat SHARED
    Cheat/main.cpp
    Cheat/UI.cpp
    Cheat/Drawing.cpp
    Cheat/ImGui/imgui.cpp
    Cheat/ImGui/imgui_draw.cpp
    Cheat/ImGui/imgui_tables.cpp
    Cheat/ImGui/imgui_widgets.cpp
    Cheat/ImGui/imgui_impl_dx11.cpp
    Cheat/ImGui/imgui_impl_win32.cpp
)

target_include_directories(cheat PRIVATE
    ${CMAKE_SOURCE_DIR}/Cheat
    ${CMAKE_SOURCE_DIR}/Cheat/ImGui
)

target_compile_definitions(cheat PRIVATE
    _WINDLL
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(cheat PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
)

if(MSVC)
    target_compile_options(cheat PRIVATE /W3 /O2 /EHsc /MT /GS-)
endif()
