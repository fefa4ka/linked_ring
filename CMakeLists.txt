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
)
target_include_directories(lr PUBLIC include)

# Testing configuration
enable_testing()
add_subdirectory(test)

# Installation configuration
include(GNUInstallDirs)
install(TARGETS lr
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

