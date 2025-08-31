#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <memory>
#include <random>
#include <vector>
#include <map>
#include <functional>
#include <atomic>

// High-speed HFT simulator that runs at realistic microsecond speeds
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

enum class Venue : uint8_t { COINBASE = 0, BINANCE = 1 };
enum class Side : uint8_t { BUY = 0, SELL = 1 };

// High-Performance Market Data Generator
class HighSpeedMarketDataGenerator {
private:
    std::mt19937_64 rng_;
    std::map<SymbolId, double> prices_;
    std::map<SymbolId, double> volatilities_;
    std::atomic<uint64_t> ticks_generated_{0};
    
public:
    HighSpeedMarketDataGenerator() : rng_(std::random_device{}()) {
        prices_[1] = 50000.0;  // BTC
        prices_[2] = 3000.0;   // ETH
        volatilities_[1] = 0.0001;  // Much smaller moves per tick
        volatilities_[2] = 0.0002;
    }
    
    void generate_tick() {
        ticks_generated_.fetch_add(1);
        
        for (auto& [symbol, price] : prices_) {
            // Micro price movements typical in HFT
            std::normal_distribution<double> micro_change(0.0, volatilities_[symbol] * price);
            double change = micro_change(rng_);
            price += change;
            price = std::max(price, 100.0);
        }
    }
    
    void inject_volatility_spike() {
        for (auto& [symbol, vol] : volatilities_) {
            vol *= 50.0; // Massive volatility spike
        }
    }
    
    void inject_flash_crash() {
        for (auto& [symbol, price] : prices_) {
            price *= 0.95; // 5% instant drop
        }
    }
    
    void reset_volatility() {
        volatilities_[1] = 0.0001;
        volatilities_[2] = 0.0002;
    }
    
    double get_price(SymbolId symbol) const {
        auto it = prices_.find(symbol);
        return it != prices_.end() ? it->second : 0.0;
    }
    
    uint64_t get_ticks_generated() const { return ticks_generated_.load(); }
    void reset_stats() { ticks_generated_.store(0); }
};

// Ultra-Low-Latency Exchange Simulator
class HighSpeedExchangeSimulator {
private:
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<uint64_t> fills_executed_{0};
    std::atomic<OrderId> next_order_id_{1};
    std::mt19937_64 rng_;
    
public:
    HighSpeedExchangeSimulator() : rng_(std::random_device{}()) {}
    
    OrderId submit_order_ultra_fast(Side side, Price price, Quantity quantity) {
        OrderId id = next_order_id_.fetch_add(1);
        orders_processed_.fetch_add(1);
        
        // Simulate ultra-low latency (1-10 microseconds)
        // No sleep - real HFT systems process in nanoseconds!
        
        // High fill rate for demo
        if (std::uniform_real_distribution<>(0, 1)(rng_) > 0.1) { // 90% fill rate
            fills_executed_.fetch_add(1);
        }
        
        return id;
    }
    
    uint64_t get_orders_processed() const { return orders_processed_.load(); }
    uint64_t get_fills_executed() const { return fills_executed_.load(); }
    void reset_stats() { 
        orders_processed_.store(0); 
        fills_executed_.store(0);
    }
};

// High-Performance Infrastructure Simulator
class HighSpeedInfrastructureSimulator {
private:
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_lost_{0};
    double packet_loss_rate_ = 0.00001; // Very low under normal conditions
    
public:
    bool send_message_fast() {
        messages_sent_.fetch_add(1);
        
        // Ultra-fast message processing - no artificial delays
        std::random_device rd;
        std::mt19937 gen(rd());
        if (std::uniform_real_distribution<>(0, 1)(gen) < packet_loss_rate_) {
            messages_lost_.fetch_add(1);
            return false;
        }
        return true;
    }
    
    void simulate_network_stress() {
        packet_loss_rate_ = 0.001;  // 0.1% loss during stress
    }
    
    void restore_network() { packet_loss_rate_ = 0.00001; }
    
    uint64_t get_messages_sent() const { return messages_sent_.load(); }
    uint64_t get_messages_lost() const { return messages_lost_.load(); }
    void reset_stats() { 
        messages_sent_.store(0);
        messages_lost_.store(0);
    }
};

// High-Speed Simulation Controller
class HighSpeedSimulationController {
private:
    std::unique_ptr<HighSpeedMarketDataGenerator> market_gen_;
    std::unique_ptr<HighSpeedExchangeSimulator> exchange_;
    std::unique_ptr<HighSpeedInfrastructureSimulator> infra_;
    std::atomic<bool> running_{false};
    
public:
    HighSpeedSimulationController() {
        market_gen_ = std::make_unique<HighSpeedMarketDataGenerator>();
        exchange_ = std::make_unique<HighSpeedExchangeSimulator>();
        infra_ = std::make_unique<HighSpeedInfrastructureSimulator>();
    }
    
    void run_hft_speed_demo(int duration_seconds = 5) {
        printf("\n⚡ High-Frequency Trading Speed Demo\n");
        printf("====================================\n");
        printf("Running at REAL HFT speeds for %d seconds...\n\n", duration_seconds);
        
        running_.store(true);
        auto start_time = std::chrono::high_resolution_clock::now();
        
        uint64_t tick_count = 0;
        uint64_t order_count = 0;
        
        while (running_.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::high_resolution_clock::now() - start_time);
            
            if (elapsed.count() >= duration_seconds) break;
            
            // Process market data at HFT speed
            for (int i = 0; i < 1000; ++i) { // 1000 ticks per iteration
                market_gen_->generate_tick();
                tick_count++;
                
                // Submit orders based on market data
                if (tick_count % 10 == 0) { // Every 10th tick
                    double btc_price = market_gen_->get_price(1);
                    exchange_->submit_order_ultra_fast(
                        Side::BUY, 
                        to_fixed_price(btc_price * 0.9999), 
                        static_cast<Quantity>(0.001 * QUANTITY_MULTIPLIER)
                    );
                    infra_->send_message_fast();
                    order_count++;
                }
            }
            
            // Inject some market events
            if (elapsed.count() == 2) {
                market_gen_->inject_volatility_spike();
            } else if (elapsed.count() == 4) {
                market_gen_->inject_flash_crash();
            }
            
            // Tiny sleep to prevent 100% CPU usage while maintaining high speed
            std::this_thread::sleep_for(std::chrono::microseconds(100)); // 100 microseconds
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        // Performance metrics
        uint64_t ticks_generated = market_gen_->get_ticks_generated();
        uint64_t orders_processed = exchange_->get_orders_processed();
        uint64_t fills_executed = exchange_->get_fills_executed();
        uint64_t messages_sent = infra_->get_messages_sent();
        uint64_t messages_lost = infra_->get_messages_lost();
        
        printf("🏁 High-Speed Simulation Results:\n");
        printf("   Actual Duration: %llu ms\n", static_cast<unsigned long long>(actual_duration));
        printf("   Market Ticks Generated: %llu (%.0f ticks/sec)\n", 
               static_cast<unsigned long long>(ticks_generated),
               (double)ticks_generated / (actual_duration / 1000.0));
        printf("   Orders Processed: %llu (%.0f orders/sec)\n", 
               static_cast<unsigned long long>(orders_processed),
               (double)orders_processed / (actual_duration / 1000.0));
        printf("   Fills Executed: %llu (%.1f%% fill rate)\n", 
               static_cast<unsigned long long>(fills_executed),
               (double)fills_executed / orders_processed * 100.0);
        printf("   Messages Sent: %llu\n", static_cast<unsigned long long>(messages_sent));
        printf("   Messages Lost: %llu (%.4f%%)\n", 
               static_cast<unsigned long long>(messages_lost),
               (double)messages_lost / messages_sent * 100.0);
        
        double throughput_million = (double)ticks_generated / 1000000.0;
        printf("\n🚀 Throughput: %.2f MILLION market events processed!\n", throughput_million);
        
        if (ticks_generated > 1000000) {
            printf("✅ Successfully demonstrated HFT-level performance!\n");
        } else {
            printf("⚠️  Lower throughput - may need optimization\n");
        }
        
        running_.store(false);
    }
    
    void run_comparison_demo() {
        printf("\n🔄 Speed Comparison Demo\n");
        printf("========================\n");
        
        // Reset stats
        market_gen_->reset_stats();
        exchange_->reset_stats();
        infra_->reset_stats();
        
        printf("\n1️⃣ Demo Speed (1 tick per second):\n");
        auto demo_start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 10; ++i) {
            market_gen_->generate_tick();
            exchange_->submit_order_ultra_fast(Side::BUY, to_fixed_price(50000), 
                                             static_cast<Quantity>(0.1 * QUANTITY_MULTIPLIER));
            infra_->send_message_fast();
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Demo speed
        }
        
        auto demo_end = std::chrono::high_resolution_clock::now();
        auto demo_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            demo_end - demo_start).count();
        
        printf("   Demo mode: 10 events in %llu ms\n", static_cast<unsigned long long>(demo_duration));
        
        // Reset for HFT speed test
        market_gen_->reset_stats();
        exchange_->reset_stats();
        infra_->reset_stats();
        
        printf("\n2️⃣ HFT Speed (thousands of ticks per second):\n");
        auto hft_start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 100000; ++i) { // 100,000 events
            market_gen_->generate_tick();
            if (i % 10 == 0) {
                exchange_->submit_order_ultra_fast(Side::BUY, to_fixed_price(50000), 
                                                 static_cast<Quantity>(0.001 * QUANTITY_MULTIPLIER));
                infra_->send_message_fast();
            }
        }
        
        auto hft_end = std::chrono::high_resolution_clock::now();
        auto hft_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            hft_end - hft_start).count();
        
        printf("   HFT mode: 100,000 events in %llu ms\n", static_cast<unsigned long long>(hft_duration));
        printf("   Speed improvement: %.0fx faster!\n", (double)demo_duration / hft_duration);
        
        printf("\n📊 HFT Speed Analysis:\n");
        printf("   Events per second: %.0f\n", 100000.0 / (hft_duration / 1000.0));
        printf("   Microseconds per event: %.2f μs\n", (hft_duration * 1000.0) / 100000.0);
    }
    
    void stop() { running_.store(false); }
};

} // namespace hft

std::unique_ptr<hft::HighSpeedSimulationController> g_controller;
std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", stopping simulation...\n";
    if (g_controller) {
        g_controller->stop();
    }
    g_running.store(false);
}

int main(int argc, char* argv[]) {
    std::cout << "⚡ HIGH-SPEED HFT Simulator\n";
    std::cout << "===========================\n";
    std::cout << "Demonstrating REAL high-frequency trading speeds:\n";
    std::cout << "• Microsecond-level market tick processing\n";
    std::cout << "• Millions of events per second capability\n";
    std::cout << "• Ultra-low latency order processing\n\n";
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_controller = std::make_unique<hft::HighSpeedSimulationController>();
        
        std::cout << "✅ High-speed simulator initialized!\n";
        
        bool run_comparison = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--compare" || arg == "-c") {
                run_comparison = true;
                break;
            }
        }
        
        if (run_comparison) {
            g_controller->run_comparison_demo();
        } else {
            std::cout << "\nOptions:\n";
            std::cout << "  1 - Run 5-second HFT speed demo\n";
            std::cout << "  2 - Run 10-second intensive demo\n";
            std::cout << "  3 - Run speed comparison\n";
            std::cout << "  q - Quit\n\n";
            
            char choice;
            std::cout << "Enter your choice: ";
            std::cin >> choice;
            
            switch (choice) {
                case '1':
                    g_controller->run_hft_speed_demo(5);
                    break;
                case '2':
                    g_controller->run_hft_speed_demo(10);
                    break;
                case '3':
                    g_controller->run_comparison_demo();
                    break;
                case 'q':
                    break;
                default:
                    std::cout << "Running default 5-second demo...\n";
                    g_controller->run_hft_speed_demo(5);
                    break;
            }
        }
        
        std::cout << "\n✅ High-speed simulation completed!\n";
        std::cout << "🎯 This demonstrates the performance difference between\n";
        std::cout << "    demo mode (human-readable) and real HFT speeds!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Simulation error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
