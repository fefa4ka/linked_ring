# Testing configuration for Linked Ring Buffer

    add_compile_options(-w)
# Define a function to create tests more easily
function(add_lr_test test_name test_file)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} lr ${ARGN})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Basic tests
add_lr_test(test_basic basic.c)
add_lr_test(test_debug_segfault debug_segfault.c)
add_lr_test(test_segfault_single test_segfault_single.c)

# Edge case tests
add_lr_test(test_max_value max_value_test.c)
add_lr_test(test_edge edge.c)
add_lr_test(test_edge_cases edge_cases.c)
add_lr_test(test_circular circular_test.c)
add_lr_test(test_push_pop push_pop_test.c)

# Multi-threaded tests
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_lr_test(test_multi_thread multi_thread.c Threads::Threads)

# File tests
file(COPY one_line.txt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_lr_test(test_files files.c)
