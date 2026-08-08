cmake_minimum_required(VERSION 3.22)
project(Injector CXX)

add_executable(Injector WIN32
    main.cpp
)

target_compile_definitions(Injector PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(Injector PRIVATE
    comdlg32
    user32
    gdi32
    kernel32
)

# MSVC specific
if(MSVC)
    target_compile_options(Injector PRIVATE
        /W3
        /O2
        /EHsc
        /MT           # static runtime
    )
    # Убираем console window
    set_target_properties(Injector PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()
