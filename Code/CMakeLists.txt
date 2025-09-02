cmake_minimum_required(VERSION 3.13)
project(PicoDraw C CXX ASM)

add_library(PicoDraw STATIC)

file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
target_sources(PicoDraw PRIVATE ${SRC_FILES})

target_include_directories(PicoDraw
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
)
