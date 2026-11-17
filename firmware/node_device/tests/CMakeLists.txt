cmake_minimum_required(VERSION 3.22)

# Host-side unit tests for the Wio-E5 driver (Lib/wioe5).
# See ../docs/wioe5/test_plan.md.
project(wioe5_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WIOE5_SANITIZE "Build with AddressSanitizer + UBSan (Linux/WSL/CI)" OFF)
option(WIOE5_COVERAGE "Build with gcov instrumentation" OFF)

set(WIOE5_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../node/Lib/wioe5)

set(STRICT_WARNINGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion)

# ---- Unity (vendored, unmodified) ---------------------------------------
add_library(unity STATIC vendor/unity/unity.c)
target_include_directories(unity PUBLIC vendor/unity)

# ---- Driver under test (no STM32 port) ----------------------------------
add_library(wioe5 STATIC
    ${WIOE5_DIR}/src/wioe5.c
    ${WIOE5_DIR}/src/wioe5_hex.c
    ${WIOE5_DIR}/src/wioe5_line.c
    ${WIOE5_DIR}/src/wioe5_parse.c
    ${WIOE5_DIR}/src/wioe5_ringbuf.c
)
target_include_directories(wioe5
    PUBLIC  ${WIOE5_DIR}/inc
    PUBLIC  ${WIOE5_DIR}/src   # internal headers are tested directly
)
target_compile_options(wioe5 PRIVATE ${STRICT_WARNINGS})

# ---- Application cycle (hardware-independent part of node/App) -----------
set(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../node/App)
add_library(app_core STATIC ${APP_DIR}/app_lorawan.c)
target_include_directories(app_core PUBLIC ${APP_DIR})
target_link_libraries(app_core PUBLIC wioe5)
target_compile_options(app_core PRIVATE ${STRICT_WARNINGS})

# ---- Test support ---------------------------------------------------------
add_library(test_support STATIC
    support/fake_port.c
    support/modem_sim.c
    support/test_common.c
)
target_include_directories(test_support PUBLIC support)
target_link_libraries(test_support PUBLIC wioe5 unity)
target_compile_options(test_support PRIVATE -Wall -Wextra -Werror)

if(WIOE5_SANITIZE)
    foreach(t wioe5 test_support)
        target_compile_options(${t} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${t} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()

if(WIOE5_COVERAGE)
    target_compile_options(wioe5 PRIVATE --coverage -O0)
    target_link_options(wioe5 PUBLIC --coverage)
endif()

# ---- Tests ----------------------------------------------------------------
enable_testing()

set(WIOE5_TESTS
    test_ringbuf
    test_line
    test_parse
    test_hex
    test_engine
    test_op_setup
    test_op_join
    test_op_send
    test_op_power
    test_app_lorawan
)

foreach(t ${WIOE5_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE test_support app_core)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60)
endforeach()
