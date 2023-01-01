cmake_minimum_required(VERSION 3.1)

project(lr VERSION 1.0
    DESCRIPTION "Linked Ring Data Structure"
    LANGUAGES C)

add_library(lr STATIC src/lr.c)
target_include_directories(lr PUBLIC include)

add_executable(test_basic test/basic.c)
target_link_libraries(test_basic lr)

add_test(NAME basic_test
    COMMAND basic_test)

add_executable(test_edge test/edge.c)
target_link_libraries(test_edge lr)

add_test(NAME edge_test
    COMMAND edge_test)
