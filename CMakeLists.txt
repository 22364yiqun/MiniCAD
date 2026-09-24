cmake_minimum_required(VERSION 3.16)
project(homework1)

set(CMAKE_CXX_STANDARD 20)

add_executable(homework1 src/main.cpp)

set(LOCAL_QT_PREFIX "${CMAKE_SOURCE_DIR}/qt/6.8.3/mingw_64")
if (EXISTS "${LOCAL_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT_PREFIX}")
endif()
find_package(Qt6 COMPONENTS Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)
add_executable(minicad_qt WIN32 src/qt_main.cpp)
target_link_libraries(minicad_qt PRIVATE Qt6::Widgets)
