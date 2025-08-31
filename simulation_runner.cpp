#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "simulation_controller.hpp"

using namespace hft::simulator;

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", stopping simulation...\n";
    running.store(false);
}

void run_demo_scenarios(SimulationController& controller) {
    std::cout << "\n🎬 Running Demo Simulation Scenarios:\n\n";
    
    // 1. Normal Trading Test
    std::cout << "=" << std::string(60, '=') << "\n";
    std::cout << "📊 SCENARIO 1: Normal Trading Conditions\n";
    std::cout << "=" << std::string(60, '=') << "\n";
    
    auto results1 = controller.run_normal_trading_simulation(30000); // 30 seconds
    results1.print_summary();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 2. High Volatility Test
    std::cout << "\n" << "=" << std::string(60, '=') << "\n";
    std::cout << "⚡ SCENARIO 2: High Volatility Event\n";
    std::cout << "=" << std::string(60, '=') << "\n";
    
    auto results2 = controller.run_simulation("high_volatility", 20000); // 20 seconds
    results2.print_summary();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 3. Flash Crash Test
    std::cout << "\n" << "=" << std::string(60, '=') << "\n";
    std::cout << "💥 SCENARIO 3: Flash Crash Event\n";
    std::cout << "=" << std::string(60, '=') << "\n";
    
    auto results3 = controller.run_flash_crash_test(25000); // 25 seconds
    results3.print_summary();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 4. Exchange Outage Test
    std::cout << "\n" << "=" << std::string(60, '=') << "\n";
    std::cout << "🔌 SCENARIO 4: Exchange Outage\n";
    std::cout << "=" << std::string(60, '=') << "\n";
    
    auto results4 = controller.run_simulation("exchange_outage", 20000); // 20 seconds
    results4.print_summary();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 5. Comprehensive Stress Test
    std::cout << "\n" << "=" << std::string(60, '=') << "\n";
    std::cout << "🔥 SCENARIO 5: Comprehensive Stress Test\n";
    std::cout << "=" << std::string(60, '=') << "\n";
    
    auto results5 = controller.run_stress_test(40000); // 40 seconds
    results5.print_summary();
}

void interactive_mode(SimulationController& controller) {
    std::cout << "\n🎮 Interactive Simulation Mode\n";
    std::cout << "Available commands:\n";
    std::cout << "  1 - Run normal trading simulation\n";
    std::cout << "  2 - Run high volatility simulation\n";
    std::cout << "  3 - Run flash crash simulation\n";
    std::cout << "  4 - Run exchange outage simulation\n";
    std::cout << "  5 - Run network issues simulation\n";
    std::cout << "  6 - Run comprehensive stress test\n";
    std::cout << "  l - List all available scenarios\n";
    std::cout << "  s - Show simulation status\n";
    std::cout << "  q - Quit\n\n";
    
    char choice;
    while (running.load()) {
        std::cout << "Enter your choice: ";
        std::cin >> choice;
        
        switch (choice) {
            case '1': {
                auto results = controller.run_normal_trading_simulation(30000);
                results.print_summary();
                break;
            }
            case '2': {
                auto results = controller.run_simulation("high_volatility", 20000);
                results.print_summary();
                break;
            }
            case '3': {
                auto results = controller.run_flash_crash_test(25000);
                results.print_summary();
                break;
            }
            case '4': {
                auto results = controller.run_simulation("exchange_outage", 20000);
                results.print_summary();
                break;
            }
            case '5': {
                auto results = controller.run_simulation("network_issues", 15000);
                results.print_summary();
                break;
            }
            case '6': {
                auto results = controller.run_stress_test(60000);
                results.print_summary();
                break;
            }
            case 'l':
                controller.list_scenarios();
                break;
            case 's':
                controller.print_status();
                break;
            case 'q':
                running.store(false);
                break;
            default:
                std::cout << "Invalid choice. Try again.\n";
                break;
        }
        
        if (!running.load()) break;
        
        std::cout << "\n" << std::string(50, '-') << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "🚀 High-Fidelity Trading System Simulator\n";
    std::cout << "==========================================\n\n";
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize simulation controller
        SimulationController controller;
        SimulationConfig config;
        config.time_acceleration = 1.0;  // Real-time simulation
        config.simulate_network_issues = true;
        config.simulate_exchange_outages = true;
        
        controller.initialize(config);
        
        std::cout << "✅ Simulation controller initialized successfully!\n";
        
        // Show available scenarios
        controller.list_scenarios();
        
        // Check command line arguments
        bool demo_mode = false;
        bool interactive_mode_flag = false;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--demo" || arg == "-d") {
                demo_mode = true;
            } else if (arg == "--interactive" || arg == "-i") {
                interactive_mode_flag = true;
            } else if (arg == "--help" || arg == "-h") {
                std::cout << "\nUsage: " << argv[0] << " [OPTIONS]\n";
                std::cout << "Options:\n";
                std::cout << "  -d, --demo         Run demo scenarios automatically\n";
                std::cout << "  -i, --interactive  Run in interactive mode\n";
                std::cout << "  -h, --help         Show this help message\n";
                std::cout << "\nBy default, runs in interactive mode.\n";
                return 0;
            }
        }
        
        if (demo_mode) {
            // Run demo scenarios automatically
            run_demo_scenarios(controller);
        } else {
            // Default to interactive mode
            interactive_mode(controller);
        }
        
        std::cout << "\n✅ Simulation completed successfully!\n";
        std::cout << "📊 Thank you for using the HFT Simulator!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Simulation error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
