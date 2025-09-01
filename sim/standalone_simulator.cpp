#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <memory>
#include <string>
#include "../shared_types.hpp"
#include "../base_market_generator.hpp"
#include "base_exchange_simulator.hpp"

// Simplified standalone version that demonstrates the complete high-fidelity simulator
using namespace hft;

// Enhanced Market Data Generator using shared base
class StandaloneMarketDataGenerator : public SimpleMarketGenerator {
public:
    StandaloneMarketDataGenerator() : SimpleMarketGenerator() {
        // Initialize with standalone-specific prices
        prices_[BTC_USD] = 50000.0;  // BTC
        prices_[ETH_USD] = 3000.0;   // ETH
    }
    
    void simulate_normal_market() {
        generate_market_update();
    }
    
    void simulate_flash_crash() {
        printf("💥 Injecting flash crash - prices dropping 15%%\n");
        trigger_flash_crash();
    }
    
    void simulate_high_volatility() {
        printf("⚡ High volatility mode activated\n");
        trigger_volatility_spike();
    }
    
    void simulate_news_shock() {
        printf("📰 News shock - random 5-10%% price movement\n");
        SimpleMarketGenerator::simulate_news_shock();
    }
    
    void reset_volatility() {
        restore_normal_market();
    }
};

// Now using shared SimpleExchangeSimulator from base_exchange_simulator.hpp

// Now using shared SimpleInfrastructureSimulator from base_exchange_simulator.hpp

// Complete Simulation Controller using shared components
class StandaloneSimulationController {
private:
    std::unique_ptr<StandaloneMarketDataGenerator> market_data_gen_;
    std::unique_ptr<SimpleExchangeSimulator> exchange_sim_;
    std::unique_ptr<SimpleInfrastructureSimulator> infra_sim_;
    std::atomic<bool> running_{false};
    
public:
    StandaloneSimulationController() {
        market_data_gen_ = std::make_unique<StandaloneMarketDataGenerator>();
        exchange_sim_ = std::make_unique<SimpleExchangeSimulator>();
        infra_sim_ = std::make_unique<SimpleInfrastructureSimulator>();
    }
    
    void run_normal_trading_simulation(int duration_seconds = 30) {
        printf("\n🎬 Starting Normal Trading Simulation\n");
        printf("Duration: %d seconds\n", duration_seconds);
        
        running_.store(true);
        auto start_time = std::chrono::steady_clock::now();
        
        double btc_start = market_data_gen_->get_price(BTC_USD);
        double eth_start = market_data_gen_->get_price(ETH_USD);
        
        while (running_.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) break;
            
            // Simulate market activity
            market_data_gen_->simulate_normal_market();
            
            // Simulate some trading
            if (elapsed.count() % 5 == 0) {  // Every 5 seconds
                double btc_price = market_data_gen_->get_price(BTC_USD);
                exchange_sim_->submit_order(Side::BUY, to_fixed_price(btc_price * 0.999), 
                                          to_fixed_quantity(0.1), "trader1");
                infra_sim_->send_message("ORDER_BTC_BUY");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(BTC_USD);
        double eth_end = market_data_gen_->get_price(ETH_USD);
        
        printf("\n📊 Normal Trading Results:\n");
        printf("   BTC: $%.2f → $%.2f (%.2f%%)\n", btc_start, btc_end, 
               ((btc_end - btc_start) / btc_start) * 100);
        printf("   ETH: $%.2f → $%.2f (%.2f%%)\n", eth_start, eth_end,
               ((eth_end - eth_start) / eth_start) * 100);
        printf("   Total trades: %llu\n", static_cast<unsigned long long>(exchange_sim_->get_total_trades()));
        
        running_.store(false);
    }
    
    void run_flash_crash_simulation(int duration_seconds = 60) {
        printf("\n💥 Starting Flash Crash Simulation\n");
        printf("Duration: %d seconds\n", duration_seconds);
        
        running_.store(true);
        auto start_time = std::chrono::steady_clock::now();
        
        double btc_start = market_data_gen_->get_price(BTC_USD);
        double eth_start = market_data_gen_->get_price(ETH_USD);
        
        bool crash_triggered = false;
        
        while (running_.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) break;
            
            // Trigger flash crash at 10 seconds
            if (elapsed.count() == 10 && !crash_triggered) {
                market_data_gen_->simulate_flash_crash();
                crash_triggered = true;
            }
            
            // Recovery starts at 30 seconds
            if (elapsed.count() == 30) {
                printf("🔄 Flash crash recovery beginning\n");
                market_data_gen_->reset_volatility();
            }
            
            market_data_gen_->simulate_normal_market();
            
            // Simulate panicked trading during crash
            if (crash_triggered && elapsed.count() < 30) {
                double btc_price = market_data_gen_->get_price(BTC_USD);
                exchange_sim_->submit_order(Side::SELL, to_fixed_price(btc_price * 0.99), 
                                          to_fixed_quantity(0.5), "panic_seller");
                infra_sim_->send_message("PANIC_SELL_ORDER");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(BTC_USD);
        double eth_end = market_data_gen_->get_price(ETH_USD);
        
        printf("\n📊 Flash Crash Results:\n");
        printf("   BTC: $%.2f → $%.2f (%.2f%%)\n", btc_start, btc_end, 
               ((btc_end - btc_start) / btc_start) * 100);
        printf("   ETH: $%.2f → $%.2f (%.2f%%)\n", eth_start, eth_end,
               ((eth_end - eth_start) / eth_start) * 100);
        printf("   Total trades: %llu\n", static_cast<unsigned long long>(exchange_sim_->get_total_trades()));
        
        running_.store(false);
    }
    
    void run_stress_test_simulation(int duration_seconds = 90) {
        printf("\n🔥 Starting Comprehensive Stress Test\n");
        printf("Duration: %d seconds\n", duration_seconds);
        
        running_.store(true);
        auto start_time = std::chrono::steady_clock::now();
        
        double btc_start = market_data_gen_->get_price(BTC_USD);
        double eth_start = market_data_gen_->get_price(ETH_USD);
        
        while (running_.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) break;
            
            // Different stress events at different times
            if (elapsed.count() == 10) {
                market_data_gen_->simulate_high_volatility();
            } else if (elapsed.count() == 30) {
                market_data_gen_->simulate_news_shock();
            } else if (elapsed.count() == 45) {
                infra_sim_->simulate_network_issues();
            } else if (elapsed.count() == 60) {
                infra_sim_->simulate_exchange_outage();
            } else if (elapsed.count() == 70) {
                infra_sim_->restore_network();
                printf("🔄 Network connectivity restored\n");
            }
            
            market_data_gen_->simulate_normal_market();
            
            // Continuous trading activity
            double btc_price = market_data_gen_->get_price(BTC_USD);
            exchange_sim_->submit_order(Side::BUY, to_fixed_price(btc_price * 0.999), 
                                      to_fixed_quantity(0.2), "stress_trader");
            infra_sim_->send_message("STRESS_TEST_ORDER");
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(BTC_USD);
        double eth_end = market_data_gen_->get_price(ETH_USD);
        
        printf("\n📊 Stress Test Results:\n");
        printf("   BTC: $%.2f → $%.2f (%.2f%%)\n", btc_start, btc_end, 
               ((btc_end - btc_start) / btc_start) * 100);
        printf("   ETH: $%.2f → $%.2f (%.2f%%)\n", eth_start, eth_end,
               ((eth_end - eth_start) / eth_start) * 100);
        printf("   Total trades: %llu\n", static_cast<unsigned long long>(exchange_sim_->get_total_trades()));
        printf("   Messages sent: %llu\n", static_cast<unsigned long long>(infra_sim_->get_messages_sent()));
        printf("   Messages lost: %llu (%.2f%%)\n", 
               static_cast<unsigned long long>(infra_sim_->get_messages_lost()),
               (static_cast<double>(infra_sim_->get_messages_lost()) / infra_sim_->get_messages_sent()) * 100);
        
        running_.store(false);
    }
    
    void stop() {
        running_.store(false);
    }
    
    void reset_stats() {
        exchange_sim_->reset_stats();
        market_data_gen_->reset_volatility();
        infra_sim_->restore_network();
    }
};

// Global simulation controller
std::unique_ptr<StandaloneSimulationController> g_controller;
std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", stopping simulation...\n";
    if (g_controller) {
        g_controller->stop();
    }
    g_running.store(false);
}

void run_interactive_demo() {
    std::cout << "\n🎮 Interactive High-Fidelity Trading Simulator\n";
    std::cout << "==============================================\n";
    std::cout << "\nAvailable scenarios:\n";
    std::cout << "  1 - Normal Trading Day (30 seconds)\n";
    std::cout << "  2 - Flash Crash Event (60 seconds)\n";
    std::cout << "  3 - Comprehensive Stress Test (90 seconds)\n";
    std::cout << "  q - Quit\n\n";
    
    char choice;
    while (g_running.load()) {
        std::cout << "Enter your choice: ";
        std::cin >> choice;
        
        if (!g_running.load()) break;
        
        switch (choice) {
            case '1':
                g_controller->reset_stats();
                g_controller->run_normal_trading_simulation(30);
                break;
            case '2':
                g_controller->reset_stats();
                g_controller->run_flash_crash_simulation(60);
                break;
            case '3':
                g_controller->reset_stats();
                g_controller->run_stress_test_simulation(90);
                break;
            case 'q':
                g_running.store(false);
                break;
            default:
                std::cout << "Invalid choice. Try again.\n";
                continue;
        }
        
        if (!g_running.load()) break;
        
        std::cout << "\n" << std::string(60, '-') << "\n";
        std::cout << "Simulation completed. Choose another scenario or 'q' to quit.\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "🚀 High-Fidelity Trading System Simulator\n";
    std::cout << "==========================================\n";
    std::cout << "A comprehensive simulation environment that mimics:\n";
    std::cout << "• Real exchange behavior and market dynamics\n";
    std::cout << "• Network conditions and infrastructure failures\n";
    std::cout << "• Various market scenarios and stress conditions\n\n";
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize simulation controller
        g_controller = std::make_unique<StandaloneSimulationController>();
        
        std::cout << "✅ High-fidelity simulator initialized successfully!\n";
        
        // Check for demo mode
        bool auto_demo = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--demo" || arg == "-d") {
                auto_demo = true;
                break;
            }
        }
        
        if (auto_demo) {
            std::cout << "\n🎬 Running Automated Demo Sequence\n";
            std::cout << "==================================\n";
            
            // Run all scenarios automatically
            std::cout << "\n1️⃣ Normal Trading Simulation:\n";
            g_controller->run_normal_trading_simulation(15);
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            std::cout << "\n2️⃣ Flash Crash Simulation:\n";
            g_controller->reset_stats();
            g_controller->run_flash_crash_simulation(30);
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            std::cout << "\n3️⃣ Comprehensive Stress Test:\n";
            g_controller->reset_stats();
            g_controller->run_stress_test_simulation(45);
            
        } else {
            // Interactive mode
            run_interactive_demo();
        }
        
        std::cout << "\n✅ High-fidelity simulation completed successfully!\n";
        std::cout << "🎯 Thank you for using the HFT Simulator!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Simulation error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
