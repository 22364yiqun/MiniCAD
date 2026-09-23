cmake_minimum_required(VERSION 3.16)
project(homework1)

set(CMAKE_CXX_STANDARD 20)

add_executable(homework1 src/main.cpp)

if (WIN32)
    add_executable(homework1_viewer WIN32 src/viewer.cpp)
    target_link_libraries(homework1_viewer PRIVATE opengl32 gdi32 user32)
endif()
