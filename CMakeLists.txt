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

# Shared header files (used across multiple executables)
set(SHARED_HEADER_FILES
    shared_types.hpp
    base_market_generator.hpp
    sim/base_exchange_simulator.hpp
)

# Header files by category
set(RISK_HEADER_FILES
    risk/enhanced_risk_manager.hpp
    risk/comprehensive_risk_manager.hpp
    risk/institutional_risk_manager.hpp
    risk/unified_risk_controller.hpp
)

set(STRATEGY_HEADER_FILES
    strategies/intraday_strategies.hpp
    strategies/intraday_risk_manager.hpp
    strategies/enhanced_feed_handler.hpp
    strategies/simple_executor.hpp
    strategies/performance_tracker.hpp
    strategies/technical_indicators.hpp
)

set(SESSION_HEADER_FILES
    session_manager.hpp
)

set(MONITOR_HEADER_FILES
    monitor/system_health_monitor.hpp
    monitor/real_time_monitor.hpp
    monitor/monitoring_integration.hpp
)

set(SIM_HEADER_FILES
    sim/network_simulator.hpp
)

# Core header files (for IDE support)
set(HEADER_FILES
    messages.hpp
    ring_buffer.hpp 
    sequencer.hpp
    crypto_config.hpp
    feed_handler.hpp
    position_tracker.hpp
    logger.hpp
    connection_manager.hpp
    ${SHARED_HEADER_FILES}
    ${RISK_HEADER_FILES}
    ${STRATEGY_HEADER_FILES}
    ${MONITOR_HEADER_FILES}
    ${SESSION_HEADER_FILES}
    ${SIM_HEADER_FILES}
)

# Simulator header files
set(SIMULATOR_HEADER_FILES
    market_data_generator.hpp
    sim/exchange_simulator.hpp
    sim/infrastructure_simulator.hpp
    scenario_engine.hpp
    sim/simulation_controller.hpp
)

# Create original HFT executable
add_executable(hft 
    main.cpp
    ${HEADER_FILES}
)

# Create simulator executable
add_executable(hft_simulator 
    sim/simulation_runner.cpp
    ${HEADER_FILES}
    ${SIMULATOR_HEADER_FILES}
)

# Link libraries
target_link_libraries(hft pthread)
target_link_libraries(hft_simulator pthread)

# Include directories
target_include_directories(hft PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)
target_include_directories(hft_simulator PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)

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

# Create standalone simulator executable (uses shared headers)
add_executable(standalone_simulator 
    sim/standalone_simulator.cpp
    ${SHARED_HEADER_FILES}
)
target_link_libraries(standalone_simulator pthread)
target_include_directories(standalone_simulator PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)

# Create high-speed HFT simulator for performance testing (uses shared headers)
add_executable(high_speed_simulator 
    sim/high_speed_simulator.cpp
    ${SHARED_HEADER_FILES}
)
target_link_libraries(high_speed_simulator pthread)
target_include_directories(high_speed_simulator PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)

# Create risk system demonstration
add_executable(risk_system_demo 
    risk/risk_system_demo.cpp
    ${HEADER_FILES}
)
target_link_libraries(risk_system_demo pthread)
target_include_directories(risk_system_demo PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
)

# Create integrated HFT + Risk Control demonstration
add_executable(integrated_risk_hft_demo 
    risk/integrated_risk_hft_demo.cpp
    ${HEADER_FILES}
)
target_link_libraries(integrated_risk_hft_demo pthread)
target_include_directories(integrated_risk_hft_demo PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
)

# Create intraday trading system executable (new primary target)
add_executable(intraday_trader 
    main.cpp
    ${HEADER_FILES}
)
target_link_libraries(intraday_trader pthread)
target_include_directories(intraday_trader PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)

# Create strategy test executable
add_executable(test_strategies 
    test_strategies.cpp
    ${HEADER_FILES}
)
target_link_libraries(test_strategies pthread)
target_include_directories(test_strategies PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/strategies
    ${CMAKE_CURRENT_SOURCE_DIR}/monitor
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
)

# Summary of build targets
message(STATUS "Build targets configured:")
message(STATUS "  hft - Original HFT trading system (legacy)")
message(STATUS "  intraday_trader - NEW: Intraday trading system with VWAP/momentum strategies")
message(STATUS "  hft_simulator - High-fidelity trading system simulator")
message(STATUS "  standalone_simulator - Standalone demonstration of high-fidelity simulator")
message(STATUS "  high_speed_simulator - Ultra-fast HFT speed performance demo")
message(STATUS "  risk_system_demo - Comprehensive risk management system demonstration")
message(STATUS "  integrated_risk_hft_demo - Real-time HFT + Risk Control integration demo")
message(STATUS "  test_strategies - Strategy validation and unit tests")
