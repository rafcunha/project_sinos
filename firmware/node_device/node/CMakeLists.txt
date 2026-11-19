cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME node)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Lib/wioe5/src/wioe5.c
    Lib/wioe5/src/wioe5_hex.c
    Lib/wioe5/src/wioe5_line.c
    Lib/wioe5/src/wioe5_parse.c
    Lib/wioe5/src/wioe5_ringbuf.c
    Lib/wioe5/port/wioe5_port_stm32.c
    Lib/node_payload/node_payload.c
    App/app_lorawan.c
    App/app_payload.c
    App/app_node.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Lib/wioe5/inc
    Lib/wioe5/port
    Lib/node_payload
    App
)

# ---- Build options ----------------------------------------------------------
# CI builds with -DNODE_WERROR=ON: any warning in the project sources fails the build.
option(NODE_WERROR "Treat warnings as errors in the node sources" OFF)
if(NODE_WERROR)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Werror)
endif()

# Firmware identification embedded in the image (g_fw_ident in App/app_node.c).
# FW_VERSION comes from the release pipeline (-DFW_VERSION=v1.2.3); the git SHA is
# read at configure time, so re-run CMake configure to refresh it locally.
set(FW_VERSION "0.0.0-dev" CACHE STRING "Firmware version embedded in the image")
set(FW_GIT_SHA "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _fw_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _fw_sha_rc)
    if(_fw_sha_rc EQUAL 0 AND _fw_sha)
        set(FW_GIT_SHA "${_fw_sha}")
    endif()
endif()
message(STATUS "Firmware version: ${FW_VERSION}+${FW_GIT_SHA}")

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    # Debug builds keep a 4 KB trace of the Wio-E5 UART traffic (wioe5_trace).
    $<$<CONFIG:Debug>:WIOE5_PORT_TRACE_SIZE=4096u>
    "FW_VERSION=\"${FW_VERSION}\""
    "FW_GIT_SHA=\"${FW_GIT_SHA}\""
)

# Keep the identification string even though nothing references it (--gc-sections).
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--undefined=g_fw_ident)

# ---- Flashable images ---------------------------------------------------------
# node.bin (raw image from 0x08000000) and node.hex next to node.elf, plus a size report.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin and ${CMAKE_PROJECT_NAME}.hex"
    VERBATIM
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
