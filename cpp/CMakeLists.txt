cmake_minimum_required(VERSION 3.10)
project(simplelb CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/load_balancer.cpp
    src/load_balancer.h
)

# Executable target
add_executable(simplelb ${SOURCES})

# Target include directories
target_include_directories(simplelb PRIVATE src)

# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(simplelb ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(simplelb Threads::Threads)
endif()
