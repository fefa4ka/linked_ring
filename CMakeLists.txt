cmake_minimum_required(VERSION 3.1)

project(lr VERSION 1.0
    DESCRIPTION "Linked Ring Data Structure"
    LANGUAGES C)

add_library(lr STATIC src/lr.c)
target_include_directories(lr PUBLIC include)


enable_testing()

add_executable(test_basic test/basic.c)
target_link_libraries(test_basic lr)

add_test(NAME test_basic
    COMMAND test_basic)

add_executable(test_edge test/edge.c)
target_link_libraries(test_edge lr)

add_test(NAME test_edge
    COMMAND test_edge)

add_executable(test_multi_thread test/multi_thread.c)
set(THREADS_PREFER_PTHREAD_FLAG ON)
target_link_libraries(test_multi_thread PRIVATE lr pthread)


