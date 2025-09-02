cmake_minimum_required(VERSION 3.13)
project(PicoDraw C CXX ASM)

include(FetchContent)

FetchContent_Declare(
        pico_extras
        GIT_REPOSITORY https://github.com/raspberrypi/pico-extras.git
        GIT_TAG master
)
FetchContent_MakeAvailable(pico_extras)

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
elseif (NOT PICO_SDK_PATH) # Not set using -DPICO_SDK_PATH=... or environment
    message(FATAL_ERROR "SDK location was not specified. Please set PICO_SDK_PATH environment variable.")
endif ()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

set(PICO_BOARD pico2)
pico_sdk_init()

add_library(PicoDraw STATIC)

file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
target_sources(PicoDraw PRIVATE ${SRC_FILES})

target_include_directories(PicoDraw
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
        PRIVATE ${pico_extras_SOURCE_DIR}/src/common/pico_scanvideo/include
        PRIVATE ${pico_extras_SOURCE_DIR}/src/rp2_common/pico_scanvideo_dpi/include
)

target_link_libraries(PicoDraw
        pico_multicore
        pico_stdlib
        pico_scanvideo_dpi
        hardware_dma
        hardware_pio
        hardware_spi
)
