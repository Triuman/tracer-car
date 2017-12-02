cmake_minimum_required(VERSION 3.8)
project(client)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES main.c)
add_executable(client ${SOURCE_FILES})

include(FindPkgConfig)
pkg_check_modules(GLIB glib-2.0 REQUIRED)
include_directories(${GLIB_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} ${GLIB_LIBRARIES})

find_package(GStreamer)
pkg_check_modules(GLIB glib-2.0 REQUIRED)
if(GStreamer_FOUND)
    message(STATUS "FOUND!!")
    include_directories(${c_INCLUDE_DIRS})
endif()

target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES})