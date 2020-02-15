cmake_minimum_required(VERSION 3.0)

project(scv)

set (CMAKE_CXX_STANDARD 11)

include(${CMAKE_SOURCE_DIR}/cmake/modules/FindFFmpeg.cmake)
#include_directories("${FFMPEG_AVCODEC_INCLUDE_DIR}/libavcodec" "${FFMPEG_AVCODEC_INCLUDE_DIR}/libavformat" "${FFMPEG_AVCODEC_INCLUDE_DIR}/libavutil")

#set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake/modules/")

file(GLOB SCVFILES  "${PROJECT_SOURCE_DIR}/src/*.cpp")

add_executable(scv ${SCVFILES})

target_link_libraries(scv ${FFMPEG_LIBAVCODEC} ${FFMPEG_LIBAVFORMAT} ${FFMPEG_LIBAVUTIL})

install(TARGETS scv RUNTIME DESTINATION bin)
