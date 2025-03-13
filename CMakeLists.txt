cmake_minimum_required(VERSION 3.1)

project(lr VERSION 1.0
    DESCRIPTION "Linked Ring Data Structure"
    LANGUAGES C)

# Set compiler flags
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
endif()

# Library configuration
add_library(lr STATIC 
    src/lr.c 
    src/lr_file.c
)
target_include_directories(lr PUBLIC include)

# Testing configuration
enable_testing()

# Define a function to create tests more easily
function(add_lr_test test_name test_file)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} lr ${ARGN})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Basic tests
add_lr_test(test_basic test/basic.c)
add_lr_test(test_debug_segfault test/debug_segfault.c)

# Edge case tests
add_lr_test(test_max_value test/max_value_test.c)
add_lr_test(test_edge test/edge.c)

# Multi-threaded tests
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_lr_test(test_multi_thread test/multi_thread.c Threads::Threads)

# File tests
file(COPY test/one_line.txt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_lr_test(test_files test/files.c)

# Installation configuration
include(GNUInstallDirs)
install(TARGETS lr
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

