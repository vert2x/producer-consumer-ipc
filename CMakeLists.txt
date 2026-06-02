cmake_minimum_required(VERSION 3.16)
project(shm_demo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common module — compiled once and linked into both binaries
add_library(common STATIC common/shm_init.cpp common/runtime_control.cpp common/payload_source.cpp)
target_include_directories(common PUBLIC common)
target_link_libraries(common rt pthread)

add_executable(producer producer/producer_app.cpp)
target_link_libraries(producer common)

add_executable(consumer consumer/consumer_app.cpp)
target_link_libraries(consumer common)
