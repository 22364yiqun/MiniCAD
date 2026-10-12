cmake_minimum_required(VERSION 3.16)
project(homework1)

set(CMAKE_CXX_STANDARD 20)

set(LOCAL_QT_PREFIX "${CMAKE_SOURCE_DIR}/qt/6.8.3/mingw_64")
if (EXISTS "${LOCAL_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT_PREFIX}")
endif()
find_package(Qt6 COMPONENTS Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)
add_executable(minicad_qt WIN32 src/qt_main.cpp)
target_link_libraries(minicad_qt PRIVATE Qt6::Widgets)

if(WIN32)
    get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
    get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${QT_BIN_DIR}/windeployqt.exe")
        add_custom_command(TARGET minicad_qt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${QT_BIN_DIR};${COMPILER_BIN_DIR};$ENV{PATH}"
                "${QT_BIN_DIR}/windeployqt.exe"
                --release --compiler-runtime --no-translations
                "$<TARGET_FILE:minicad_qt>"
            VERBATIM)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(qt_history_test tests/qt_history_test.cpp)
    target_link_libraries(qt_history_test PRIVATE Qt6::Widgets)
    get_filename_component(QT_PLATFORM_PLUGIN_DIR
        "${Qt6_DIR}/../../../plugins/platforms" ABSOLUTE)
    if(EXISTS "${QT_PLATFORM_PLUGIN_DIR}/qoffscreen.dll")
        add_custom_command(TARGET qt_history_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:qt_history_test>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT_PLATFORM_PLUGIN_DIR}/qoffscreen.dll"
                "$<TARGET_FILE_DIR:qt_history_test>/platforms/qoffscreen.dll")
        add_test(NAME qt_history_test COMMAND qt_history_test)
    endif()
endif()
