cmake_minimum_required(VERSION 3.22)
project(PubgCheat CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows only
if(NOT WIN32)
    message(FATAL_ERROR "Windows only project")
endif()

add_subdirectory(Injector)
add_subdirectory(Cheat)
