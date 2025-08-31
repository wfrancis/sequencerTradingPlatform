#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "messages.hpp"
#include "ring_buffer.hpp"
#include "sequencer.hpp"
#include "crypto_config.hpp"
#include "feed_handler.hpp"
#include "position_tracker.hpp"
#include "enhanced_risk_manager.hpp"

using namespace hft;

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", shutting down gracefully...\n";
    running.store(false);
}

int main() {
    std::cout << "🚀 HFT Crypto Arbitrage MVP Starting...\n";
    std::cout << "📊 Monitoring BTC/ETH on Coinbase & Binance\n";
    std::cout << "💡 Press Ctrl+C to stop\n\n";
    
    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize configuration
        CryptoConfig config;
        std::cout << "⚙️  Configuration loaded:\n";
        std::cout << "   - Paper trading: " << (config.strategy.paper_trading ? "ON" : "OFF") << "\n";
        std::cout << "   - Arbitrage threshold: " << (config.strategy.arbitrage_threshold * 10000) << " bps\n";
        std::cout << "   - Max position size: " << config.risk.max_position_size << "\n\n";
        
        // Create ring buffers for message passing
        SPSCRingBuffer<1024> market_data_buffer;
        SPSCRingBuffer<1024> signal_buffer;
        
        // Create sequencer for message ordering
        SPSCSequencer sequencer;
        
        // Initialize position tracker and risk manager
        PositionTracker position_tracker;
        EnhancedRiskManager risk_manager(&position_tracker);
        
        std::cout << "🛡️ Enhanced risk management initialized\n";
        std::cout << "📊 Position tracking enabled\n";
        std::cout << "💾 Trade logging: trade_log.csv\n";
        std::cout << "📋 Risk events: risk_events.csv\n\n";
        
        // Initialize market data feed handler (simulated)
        MockFeedHandler feed_handler(market_data_buffer, sequencer);
        
        // Initialize enhanced arbitrage strategy
        ArbitrageStrategy strategy(market_data_buffer, signal_buffer, config, 
                                 &position_tracker, &risk_manager);
        
        // Start market data feed
        feed_handler.start();
        
        // Main trading loop
        auto start_time = std::chrono::steady_clock::now();
        auto last_status_print = start_time;
        
        std::cout << "✅ Trading loop started\n\n";
        
        while (running.load()) {
            // Process market data and look for arbitrage opportunities
            strategy.process_market_data();
            
            // Process any generated signals
            SequencedMessage signal;
            while (signal_buffer.read(signal)) {
                if (signal.type == MessageType::SIGNAL) {
                    std::cout << "🎯 Arbitrage Signal: " 
                              << std::fixed << std::setprecision(1)
                              << signal.signal.expected_edge << " bps edge, "
                              << "strength: " << signal.signal.strength << "\n";
                    
                    if (config.strategy.paper_trading) {
                        std::cout << "   📝 Paper trade executed (simulated)\n";
                    }
                }
            }
            
            // Print status every 5 seconds
            auto now = std::chrono::steady_clock::now();
            if (now - last_status_print >= std::chrono::seconds(5)) {
                strategy.print_status();
                position_tracker.print_status();
                risk_manager.print_risk_status();
                last_status_print = now;
                
                // Print performance stats
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
                std::cout << "   ⏱️  Runtime: " << elapsed.count() << "s\n";
                std::cout << "   📊 Market data available: " << market_data_buffer.available() << "\n";
                std::cout << "   📈 Signals generated: " << signal_buffer.available() << "\n\n";
            }
            
            // Small sleep to prevent excessive CPU usage on laptop
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Cleanup
        feed_handler.stop();
        
        auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
        
        std::cout << "\n✅ HFT MVP stopped gracefully\n";
        std::cout << "📊 Total runtime: " << total_runtime.count() << " seconds\n";
        std::cout << "🎯 Thank you for testing the HFT system!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}