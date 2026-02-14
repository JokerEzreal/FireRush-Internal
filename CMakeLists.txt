cmake_minimum_required(VERSION 3.20)
project(FireRush-Internal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

# Collect all source files
file(GLOB_RECURSE DLL_SOURCES "src/*.cpp")
file(GLOB_RECURSE DLL_HEADERS "src/*.h")

# Main DLL (x86)
add_library(firerush_overlay SHARED ${DLL_SOURCES} ${DLL_HEADERS})
target_include_directories(firerush_overlay PRIVATE src)
target_link_libraries(firerush_overlay PRIVATE user32 kernel32)

# Simple LoadLibrary injector (x86)
add_executable(injector tools/injector.cpp)
target_compile_options(injector PRIVATE /O2)
