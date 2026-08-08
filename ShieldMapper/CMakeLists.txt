cmake_minimum_required(VERSION 3.20)
project(ShieldMapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ShieldMapper
    main.cpp
    shield_driver.cpp
    mapper.cpp
)

target_link_libraries(ShieldMapper PRIVATE
    psapi
    ntdll
)

target_compile_definitions(ShieldMapper PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
