cmake_minimum_required(VERSION 3.21)
project(DecMtx)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(3rdparty/enet)
include_directories(3rdparty/enet/include)

add_executable(DecMtxPeer main.cpp)
target_link_libraries(DecMtxPeer PUBLIC enet)
