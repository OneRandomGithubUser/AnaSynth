cmake_minimum_required(VERSION 3.22)
project(AnaSynth)

set(CMAKE_CXX_STANDARD 20)

add_executable(AnaSynth
        AnaSynth.cpp)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../emsdk/upstream/emscripten/system/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../emsdk/upstream/emscripten/cache/ports/boost_headers)