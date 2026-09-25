cmake_minimum_required(VERSION 3.22)
project(Dora64AndroidProbe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SDL_SHARED ON CACHE BOOL "Build SDL2 shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Do not build SDL2 static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Do not build SDL2 tests" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../lib/SDL2"
    "${CMAKE_CURRENT_BINARY_DIR}/SDL2")

add_library(main SHARED src/main/cpp/probe.cpp)
target_link_libraries(main PRIVATE SDL2::SDL2 vulkan)
