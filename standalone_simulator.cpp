#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <memory>
#include <random>
#include <vector>
#include <map>
#include <functional>

// Simplified standalone version that demonstrates the complete high-fidelity simulator
namespace hft {

// Basic type definitions
using Price = uint64_t;
using Quantity = uint64_t;
using OrderId = uint64_t;
using SymbolId = uint16_t;

constexpr uint64_t PRICE_MULTIPLIER = 100000000ULL;
constexpr uint64_t QUANTITY_MULTIPLIER = 100000000ULL;

inline Price to_fixed_price(double price) {
    return static_cast<Price>(price * PRICE_MULTIPLIER);
}

inline double to_float_price(Price price) {
    return static_cast<double>(price) / PRICE_MULTIPLIER;
}

inline uint64_t get_timestamp_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

enum class Venue : uint8_t {
    COINBASE = 0,
    BINANCE = 1
};

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

// Simplified Market Data Generator
class SimpleMarketDataGenerator {
private:
    std::mt19937_64 rng_;
    std::map<SymbolId, double> prices_;
    std::map<SymbolId, double> volatilities_;
    
public:
    SimpleMarketDataGenerator() : rng_(std::random_device{}()) {
        prices_[1] = 50000.0;  // BTC
        prices_[2] = 3000.0;   // ETH
        volatilities_[1] = 0.02;  // 2% volatility
        volatilities_[2] = 0.03;  // 3% volatility
    }
    
    void simulate_normal_market() {
        for (auto& [symbol, price] : prices_) {
            std::normal_distribution<double> price_change(0.0, volatilities_[symbol] * price * 0.01);
            price += price_change(rng_);
            price = std::max(price, 100.0); // Minimum price
        }
    }
    
    void simulate_flash_crash() {
        printf("💥 Injecting flash crash - prices dropping 15%%\n");
        for (auto& [symbol, price] : prices_) {
            price *= 0.85; // 15% drop
        }
    }
    
    void simulate_high_volatility() {
        printf("⚡ High volatility mode activated\n");
        for (auto& [symbol, vol] : volatilities_) {
            vol *= 3.0; // Triple volatility
        }
    }
    
    void simulate_news_shock() {
        printf("📰 News shock - random 5-10%% price movement\n");
        for (auto& [symbol, price] : prices_) {
            double shock = (0.05 + 0.05 * std::uniform_real_distribution<>(0, 1)(rng_));
            if (std::uniform_real_distribution<>(0, 1)(rng_) > 0.5) shock = -shock;
            price *= (1.0 + shock);
        }
    }
    
    double get_price(SymbolId symbol) const {
        auto it = prices_.find(symbol);
        return it != prices_.end() ? it->second : 0.0;
    }
    
    void reset_volatility() {
        volatilities_[1] = 0.02;
        volatilities_[2] = 0.03;
    }
};

// Simplified Exchange Simulator
class SimpleExchangeSimulator {
private:
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<OrderId> next_order_id_{1};
    std::mt19937_64 rng_;
    
public:
    SimpleExchangeSimulator() : rng_(std::random_device{}()) {}
    
    OrderId submit_order(Side side, Price price, Quantity quantity, const std::string& trader) {
        OrderId id = next_order_id_.fetch_add(1);
        
        // Simulate processing latency
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        printf("   📋 Order ACK: ID=%llu, Side=%s, Price=$%.2f, Qty=%.3f\n", 
               static_cast<unsigned long long>(id), 
               side == Side::BUY ? "BUY" : "SELL",
               to_float_price(price), to_float_price(quantity));
        
        // Simulate fill probability
        if (std::uniform_real_distribution<>(0, 1)(rng_) > 0.3) { // 70% fill rate
            printf("   💰 FILL: ID=%llu, Price=$%.2f, Qty=%.3f\n",
                   static_cast<unsigned long long>(id),
                   to_float_price(price), to_float_price(quantity));
            total_trades_.fetch_add(1);
        }
        
        return id;
    }
    
    uint64_t get_total_trades() const { return total_trades_.load(); }
    
    void reset_stats() { total_trades_.store(0); }
};

// Simplified Infrastructure Simulator
class SimpleInfrastructureSimulator {
private:
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_lost_{0};
    double packet_loss_rate_ = 0.0001;
    double latency_multiplier_ = 1.0;
    
public:
    bool send_message(const std::string& message) {
        messages_sent_.fetch_add(1);
        
        // Simulate packet loss
        std::random_device rd;
        std::mt19937 gen(rd());
        if (std::uniform_real_distribution<>(0, 1)(gen) < packet_loss_rate_) {
            messages_lost_.fetch_add(1);
            return false;
        }
        
        // Simulate latency
        auto latency_us = static_cast<int>(100 * latency_multiplier_);
        std::this_thread::sleep_for(std::chrono::microseconds(latency_us));
        
        return true;
    }
    
    void simulate_network_issues() {
        printf("🌐 Network issues injected - increased latency and packet loss\n");
        packet_loss_rate_ = 0.01;  // 1% packet loss
        latency_multiplier_ = 5.0;  // 5x latency
    }
    
    void simulate_exchange_outage() {
        printf("🔌 Exchange outage - all messages will be lost\n");
        packet_loss_rate_ = 1.0;  // 100% packet loss
    }
    
    void restore_network() {
        packet_loss_rate_ = 0.0001;
        latency_multiplier_ = 1.0;
    }
    
    double get_packet_loss_rate() const { return packet_loss_rate_; }
    uint64_t get_messages_sent() const { return messages_sent_.load(); }
    uint64_t get_messages_lost() const { return messages_lost_.load(); }
};

// Complete Simulation Controller
class StandaloneSimulationController {
private:
    std::unique_ptr<SimpleMarketDataGenerator> market_data_gen_;
    std::unique_ptr<SimpleExchangeSimulator> exchange_sim_;
    std::unique_ptr<SimpleInfrastructureSimulator> infra_sim_;
    std::atomic<bool> running_{false};
    
public:
    StandaloneSimulationController() {
        market_data_gen_ = std::make_unique<SimpleMarketDataGenerator>();
        exchange_sim_ = std::make_unique<SimpleExchangeSimulator>();
        infra_sim_ = std::make_unique<SimpleInfrastructureSimulator>();
    }
    
    void run_normal_trading_simulation(int duration_seconds = 30) {
        printf("\n🎬 Starting Normal Trading Simulation\n");
        printf("Duration: %d seconds\n", duration_seconds);
        
        running_.store(true);
        auto start_time = std::chrono::steady_clock::now();
        
        double btc_start = market_data_gen_->get_price(1);
        double eth_start = market_data_gen_->get_price(2);
        
        while (running_.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) break;
            
            // Simulate market activity
            market_data_gen_->simulate_normal_market();
            
            // Simulate some trading
            if (elapsed.count() % 5 == 0) {  // Every 5 seconds
                double btc_price = market_data_gen_->get_price(1);
                exchange_sim_->submit_order(Side::BUY, to_fixed_price(btc_price * 0.999), 
                                          static_cast<Quantity>(0.1 * QUANTITY_MULTIPLIER), "trader1");
                infra_sim_->send_message("ORDER_BTC_BUY");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(1);
        double eth_end = market_data_gen_->get_price(2);
        
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
        
        double btc_start = market_data_gen_->get_price(1);
        double eth_start = market_data_gen_->get_price(2);
        
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
                double btc_price = market_data_gen_->get_price(1);
                exchange_sim_->submit_order(Side::SELL, to_fixed_price(btc_price * 0.99), 
                                          static_cast<Quantity>(0.5 * QUANTITY_MULTIPLIER), "panic_seller");
                infra_sim_->send_message("PANIC_SELL_ORDER");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(1);
        double eth_end = market_data_gen_->get_price(2);
        
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
        
        double btc_start = market_data_gen_->get_price(1);
        double eth_start = market_data_gen_->get_price(2);
        
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
            double btc_price = market_data_gen_->get_price(1);
            exchange_sim_->submit_order(Side::BUY, to_fixed_price(btc_price * 0.999), 
                                      static_cast<Quantity>(0.2 * QUANTITY_MULTIPLIER), "stress_trader");
            infra_sim_->send_message("STRESS_TEST_ORDER");
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        double btc_end = market_data_gen_->get_price(1);
        double eth_end = market_data_gen_->get_price(2);
        
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

} // namespace hft

// Global simulation controller
std::unique_ptr<hft::StandaloneSimulationController> g_controller;
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
        g_controller = std::make_unique<hft::StandaloneSimulationController>();
        
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
