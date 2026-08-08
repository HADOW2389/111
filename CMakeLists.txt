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
file(GLOB WDK_NTIFS_HEADER "C:/Program Files (x86)/Windows Kits/10/Include/10.*/km/ntifs.h")

if(WDK_NTIFS_HEADER)
    list(GET WDK_NTIFS_HEADER -1 FOUND_NTIFS)
    get_filename_component(WDK_KM_DIR "${FOUND_NTIFS}" DIRECTORY)
    get_filename_component(WDK_INC_VERSION_DIR "${WDK_KM_DIR}" DIRECTORY)
    get_filename_component(WDK_VERSION_NAME "${WDK_INC_VERSION_DIR}" NAME)

    set(WDK_LIB_DIR "C:/Program Files (x86)/Windows Kits/10/Lib/${WDK_VERSION_NAME}/km/x64")

    add_library(BlindEye SHARED
        BlindEye/DriverEntry.cpp
        BlindEye/DriverUtil.cpp
        BlindEye/Hooks.cpp
        BlindEye/Memory.cpp
    )

    set_target_properties(BlindEye PROPERTIES
        OUTPUT_NAME "BlindEye"
        SUFFIX ".sys"
        LINK_FLAGS "/MANIFEST:NO /NODEFAULTLIB"
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
        /MANIFEST:NO
        /NODEFAULTLIB
        /LIBPATH:${WDK_LIB_DIR}
    )

    target_link_libraries(BlindEye PRIVATE
        ntoskrnl.lib
        hal.lib
        BufferOverflowK.lib
    )
endif()

# 3. KDMapper Target (kdmapper.exe)
if(EXISTS "${CMAKE_SOURCE_DIR}/kdmapper/kdmapper")
    add_executable(kdmapper
        kdmapper/kdmapper/main.cpp
        kdmapper/kdmapper/kdmapper.cpp
        kdmapper/kdmapper/intel_driver.cpp
        kdmapper/kdmapper/portable_executable.cpp
        kdmapper/kdmapper/service.cpp
        kdmapper/kdmapper/utils.cpp
        kdmapper/kdmapper/KDSymbolsHandler.cpp
    )

    target_include_directories(kdmapper PRIVATE
        ${CMAKE_SOURCE_DIR}/kdmapper/kdmapper
        ${CMAKE_SOURCE_DIR}/kdmapper/kdmapper/include
    )

    target_compile_definitions(kdmapper PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )

    if(MSVC)
        target_compile_options(kdmapper PRIVATE /W3 /O2 /EHsc /MT /std:c++17)
    endif()
endif()

# 4. ShieldMapper Target (ShieldMapper.exe)
add_executable(ShieldMapper
    ShieldMapper/main.cpp
    ShieldMapper/shield_driver.cpp
    ShieldMapper/mapper.cpp
)

target_include_directories(ShieldMapper PRIVATE
    ${CMAKE_SOURCE_DIR}/ShieldMapper
)

target_compile_definitions(ShieldMapper PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ShieldMapper PRIVATE
    psapi
    ntdll
)

if(MSVC)
    target_compile_options(ShieldMapper PRIVATE /W3 /O2 /EHsc /MT /std:c++17)
endif()
