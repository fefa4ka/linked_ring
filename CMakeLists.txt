cmake_minimum_required(VERSION 3.1)

project(lr VERSION 1.0
    DESCRIPTION "Linked Ring Data Structure"
    LANGUAGES C)

add_library(lr STATIC src/lr.c src/lr_file.c)
target_include_directories(lr PUBLIC include)

enable_testing()

# Basic tests
add_executable(test_basic test/basic.c)
target_link_libraries(test_basic lr)

add_test(NAME test_basic
    COMMAND test_basic)

# Edge cases
add_executable(test_edge test/edge.c)
target_link_libraries(test_edge lr)

add_test(NAME test_edge
    COMMAND test_edge)

# Multi-threaded tests
add_executable(test_multi_thread test/multi_thread.c)
set(THREADS_PREFER_PTHREAD_FLAG ON)
target_link_libraries(test_multi_thread PRIVATE lr pthread)

# Files tests
# Copy test text files to the build directory
file(COPY test/one_line.txt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_executable(test_files test/files.c)
target_link_libraries(test_files lr)
add_test(NAME test_files
    COMMAND test_files)

