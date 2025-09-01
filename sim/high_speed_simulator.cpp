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
#include "../shared_types.hpp"
#include "../base_market_generator.hpp"
#include "base_exchange_simulator.hpp"

// High-speed HFT simulator that runs at realistic microsecond speeds
namespace hft {

// Using types from shared_types.hpp (no namespace needed)

// Use shared high-speed market data generator
using HighSpeedMarketDataGenerator = HighSpeedMarketGenerator;

// Adapter for ultra-fast order submission
class HighSpeedExchangeAdapter : public HighSpeedExchangeSimulator {
public:
    OrderId submit_order_ultra_fast(Side side, Price price, Quantity quantity) {
        return submit_order(side, price, quantity, "hft_trader");
    }
};

// Adapter for fast message sending
class HighSpeedInfrastructureAdapter : public HighSpeedInfrastructureSimulator {
public:
    bool send_message_fast() {
        send_message("HFT_MESSAGE");
        return get_messages_lost() == 0 || (get_messages_sent() > get_messages_lost());
    }
};

// High-Speed Simulation Controller
class HighSpeedSimulationController {
private:
    std::unique_ptr<HighSpeedMarketDataGenerator> market_gen_;
    std::unique_ptr<HighSpeedExchangeAdapter> exchange_;
    std::unique_ptr<HighSpeedInfrastructureAdapter> infra_;
    std::atomic<bool> running_{false};
    
public:
    HighSpeedSimulationController() {
        market_gen_ = std::make_unique<HighSpeedMarketDataGenerator>();
        exchange_ = std::make_unique<HighSpeedExchangeAdapter>();
        infra_ = std::make_unique<HighSpeedInfrastructureAdapter>();
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
                market_gen_->generate_market_update();
                tick_count++;
                
                // Submit orders based on market data
                if (tick_count % 10 == 0) { // Every 10th tick
                    double btc_price = market_gen_->get_price(BTC_USD);
                    exchange_->submit_order_ultra_fast(
                        Side::BUY, 
                        to_fixed_price(btc_price * 0.9999), 
                        to_fixed_quantity(0.001)
                    );
                    infra_->send_message_fast();
                    order_count++;
                }
            }
            
            // Inject some market events
            if (elapsed.count() == 2) {
                market_gen_->trigger_volatility_spike();
            } else if (elapsed.count() == 4) {
                market_gen_->trigger_flash_crash();
            }
            
            // Tiny sleep to prevent 100% CPU usage while maintaining high speed
            std::this_thread::sleep_for(std::chrono::microseconds(100)); // 100 microseconds
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        // Performance metrics
        uint64_t ticks_generated = market_gen_->get_updates_generated();
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
            market_gen_->generate_market_update();
            exchange_->submit_order_ultra_fast(Side::BUY, to_fixed_price(50000), 
                                             to_fixed_quantity(0.1));
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
            market_gen_->generate_market_update();
            if (i % 10 == 0) {
                exchange_->submit_order_ultra_fast(Side::BUY, to_fixed_price(50000), 
                                                 to_fixed_quantity(0.001));
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
