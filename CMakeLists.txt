cmake_minimum_required(VERSION 3.16)
project(hft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance and optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address -fsanitize=thread")

# Find optional packages
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(NUMA numa)
endif()

# Header files (for IDE support)
set(HEADER_FILES
    messages.hpp
    ring_buffer.hpp 
    sequencer.hpp
    crypto_config.hpp
    feed_handler.hpp
)

# Create executable
add_executable(hft 
    main.cpp
    ${HEADER_FILES}
)

# Link libraries
target_link_libraries(hft pthread)

# Include directories
target_include_directories(hft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Add NUMA if available
if(NUMA_FOUND)
    target_link_libraries(hft ${NUMA_LIBRARIES})
    target_include_directories(hft PRIVATE ${NUMA_INCLUDE_DIRS})
    target_compile_definitions(hft PRIVATE HAS_NUMA=1)
else()
    message(STATUS "NUMA library not found - compiling without NUMA optimizations")
endif()

# Compiler-specific optimizations
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(hft PRIVATE 
        -Wall -Wextra -Wpedantic
        -fno-omit-frame-pointer
        -msse4.2 -mavx2  # For x86 intrinsics
    )
endif()
