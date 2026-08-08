cmake_minimum_required(VERSION 3.20)
project(PubgCheat LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. DLL Target
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

# 2. Kernel Driver Target (BlindEye.sys)
set(WDK_PATH "C:/Program Files (x86)/Windows Kits/10")
file(GLOB WDK_INC_VERSIONS "${WDK_PATH}/Include/10.*")
list(GET WDK_INC_VERSIONS -1 LATEST_WDK_INC)

file(GLOB WDK_LIB_VERSIONS "${WDK_PATH}/Lib/10.*")
list(GET WDK_LIB_VERSIONS -1 LATEST_WDK_LIB)

if(EXISTS "${LATEST_WDK_INC}")
    add_library(BlindEye SHARED
        BlindEye/DriverEntry.cpp
        BlindEye/DriverUtil.cpp
        BlindEye/Hooks.cpp
        BlindEye/Memory.cpp
    )

    set_target_properties(BlindEye PROPERTIES
        OUTPUT_NAME "BlindEye"
        SUFFIX ".sys"
    )

    target_include_directories(BlindEye PRIVATE
        ${LATEST_WDK_INC}/km
        ${LATEST_WDK_INC}/km/crt
        ${LATEST_WDK_INC}/shared
        ${CMAKE_SOURCE_DIR}/BlindEye
    )

    target_compile_definitions(BlindEye PRIVATE
        _AMD64_
        _KERNEL_MODE
        NDIS_MINIPORT_DRIVER
        NTDDI_VERSION=0x0A000000
    )

    target_compile_options(BlindEye PRIVATE
        /kernel
        /GS-
        /Gy
        /GR-
        /EHs-c-
        /W3
        /O2
    )

    target_link_options(BlindEye PRIVATE
        /DRIVER
        /ENTRY:DriverEntry
        /SUBSYSTEM:NATIVE
        /LIBPATH:${LATEST_WDK_LIB}/km/x64
    )

    target_link_libraries(BlindEye PRIVATE
        ntoskrnl.lib
        hal.lib
    )
endif()
