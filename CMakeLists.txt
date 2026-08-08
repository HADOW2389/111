cmake_minimum_required(VERSION 3.20)
project(PubgCheat LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. DLL Target (cheat.dll)
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
# Ищем точный путь к ntifs.h во всех установленных Windows Kits
file(GLOB WDK_NTIFS_HEADER "C:/Program Files (x86)/Windows Kits/10/Include/10.*/km/ntifs.h")

if(WDK_NTIFS_HEADER)
    list(GET WDK_NTIFS_HEADER -1 FOUND_NTIFS)
    get_filename_component(WDK_KM_DIR "${FOUND_NTIFS}" DIRECTORY)
    get_filename_component(WDK_INC_VERSION_DIR "${WDK_KM_DIR}" DIRECTORY)
    get_filename_component(WDK_VERSION_NAME "${WDK_INC_VERSION_DIR}" NAME)

    set(WDK_LIB_DIR "C:/Program Files (x86)/Windows Kits/10/Lib/${WDK_VERSION_NAME}/km/x64")

    message(STATUS "Found WDK Include KM Dir: ${WDK_KM_DIR}")
    message(STATUS "Found WDK Lib KM Dir: ${WDK_LIB_DIR}")

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
        ${WDK_KM_DIR}
        ${WDK_INC_VERSION_DIR}/km/crt
        ${WDK_INC_VERSION_DIR}/shared
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
        /LIBPATH:${WDK_LIB_DIR}
    )

    target_link_libraries(BlindEye PRIVATE
        ntoskrnl.lib
        hal.lib
    )
else()
    message(WARNING "ntifs.h not found in Windows Kits! BlindEye.sys skipped.")
endif()
