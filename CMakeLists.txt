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
    position_tracker.hpp
    enhanced_risk_manager.hpp
    logger.hpp
    connection_manager.hpp
    network_simulator.hpp
    real_time_monitor.hpp
    monitoring_integration.hpp
)

# Simulator header files
set(SIMULATOR_HEADER_FILES
    market_data_generator.hpp
    exchange_simulator.hpp
    infrastructure_simulator.hpp
    scenario_engine.hpp
    simulation_controller.hpp
)

# Create original HFT executable
add_executable(hft 
    main.cpp
    ${HEADER_FILES}
)

# Create simulator executable
add_executable(hft_simulator 
    simulation_runner.cpp
    ${HEADER_FILES}
    ${SIMULATOR_HEADER_FILES}
)

# Link libraries
target_link_libraries(hft pthread)
target_link_libraries(hft_simulator pthread)

# Include directories
target_include_directories(hft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(hft_simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Add NUMA if available
if(NUMA_FOUND)
    target_link_libraries(hft ${NUMA_LIBRARIES})
    target_link_libraries(hft_simulator ${NUMA_LIBRARIES})
    target_include_directories(hft PRIVATE ${NUMA_INCLUDE_DIRS})
    target_include_directories(hft_simulator PRIVATE ${NUMA_INCLUDE_DIRS})
    target_compile_definitions(hft PRIVATE HAS_NUMA=1)
    target_compile_definitions(hft_simulator PRIVATE HAS_NUMA=1)
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
    target_compile_options(hft_simulator PRIVATE 
        -Wall -Wextra -Wpedantic
        -fno-omit-frame-pointer
        -msse4.2 -mavx2  # For x86 intrinsics
    )
endif()

# Create standalone simulator executable (no dependencies on problematic files)
add_executable(standalone_simulator standalone_simulator.cpp)
target_link_libraries(standalone_simulator pthread)

# Create high-speed HFT simulator for performance testing
add_executable(high_speed_simulator high_speed_simulator.cpp)
target_link_libraries(high_speed_simulator pthread)

# Summary of build targets
message(STATUS "Build targets configured:")
message(STATUS "  hft - Original HFT trading system")
message(STATUS "  hft_simulator - High-fidelity trading system simulator")
message(STATUS "  standalone_simulator - Standalone demonstration of high-fidelity simulator")
message(STATUS "  high_speed_simulator - Ultra-fast HFT speed performance demo")
