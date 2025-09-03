#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <fstream>
#include <random>
#include <iomanip>
#include <unordered_map>
#include "ring_buffer.hpp"
#include "position_tracker.hpp"
#include "risk/enhanced_risk_manager.hpp"

namespace hft {

/**
 * Simulated WebSocket Feed Handler for MVP
 * 
 * For laptop MVP, we'll simulate market data instead of 
 * implementing full WebSocket clients. This allows testing
 * the core logic without network complexity.
 */
class MockFeedHandler {
private:
    SPSCRingBuffer<1024>& market_data_buffer_;
    SPSCSequencer& sequencer_;
    std::atomic<bool> running_{false};
    std::thread feed_thread_;
    
    // Market data simulation
    std::mt19937 rng_;
    std::normal_distribution<double> price_walk_;
    
    struct SimulatedMarket {
        double btc_price = 45000.0;  // Starting BTC price
        double eth_price = 3000.0;   // Starting ETH price
        double coinbase_spread = 0.001;  // 10 bps
        double binance_spread = 0.0008;  // 8 bps
    } sim_market_;
    
    // Price sanity check parameters
    struct PriceLimits {
        double min_price = 0.01;
        double max_price = 1000000.0;
        double max_change_percent = 0.10; // 10% max change per tick
    };
    
    std::unordered_map<std::string, PriceLimits> price_limits_ = {
        {"BTC", {1000.0, 500000.0, 0.05}},
        {"ETH", {50.0, 50000.0, 0.07}}
    };
    
    std::unordered_map<std::string, double> last_valid_prices_;

public:
    MockFeedHandler(SPSCRingBuffer<1024>& buffer, SPSCSequencer& seq) 
        : market_data_buffer_(buffer), sequencer_(seq), rng_(std::random_device{}()),
          price_walk_(0.0, 0.0001) {} // Small price movements
    
    void start() {
        running_.store(true);
        feed_thread_ = std::thread(&MockFeedHandler::feed_loop, this);
        std::cout << "📡 Mock feed handler started (simulating Coinbase + Binance)\n";
    }
    
    void stop() {
        running_.store(false);
        if (feed_thread_.joinable()) {
            feed_thread_.join();
        }
        std::cout << "📡 Feed handler stopped\n";
    }

private:
    void feed_loop() {
        while (running_.load()) {
            // Generate BTC data for both venues
            generate_market_data(SymbolId(1), "BTC");  
            
            // Generate ETH data for both venues  
            generate_market_data(SymbolId(2), "ETH");
            
            // 100ms update frequency for laptop MVP
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void generate_market_data(SymbolId symbol_id, const std::string& symbol) {
        double base_price = (symbol == "BTC") ? sim_market_.btc_price : sim_market_.eth_price;
        
        // Random walk the price
        double price_change = price_walk_(rng_);
        base_price += base_price * price_change;
        
        // Validate before using
        if (!validate_price(symbol, base_price)) {
            base_price = last_valid_prices_[symbol]; // Use last valid
        }
        
        // Update stored price
        if (symbol == "BTC") sim_market_.btc_price = base_price;
        else sim_market_.eth_price = base_price;
        
        // Generate slightly different prices for each venue
        double coinbase_skew = (rng_() % 1000 - 500) / 100000.0;  // ±0.5bps
        double binance_skew = (rng_() % 1000 - 500) / 100000.0;
        
        // Create market data for Coinbase
        create_tick(symbol_id, Venue::COINBASE, base_price, coinbase_skew, sim_market_.coinbase_spread);
        
        // Create market data for Binance  
        create_tick(symbol_id, Venue::BINANCE, base_price, binance_skew, sim_market_.binance_spread);
    }
    
    void create_tick(SymbolId symbol_id, Venue venue, double mid_price, 
                    double skew, double spread) {
        
        double adjusted_mid = mid_price * (1.0 + skew);
        double half_spread = adjusted_mid * spread / 2.0;
        
        SequencedMessage msg{};
        msg.type = MessageType::MARKET_DATA_TICK;
        msg.venue = venue;
        msg.symbol_id = symbol_id;
        msg.strategy_id = 0;
        msg.source_id = static_cast<uint16_t>(venue);
        msg.correlation_id = 0;
        
        // Set market data
        msg.market_data.bid_price = to_fixed_price(adjusted_mid - half_spread);
        msg.market_data.bid_size = to_fixed_price(0.1 + (rng_() % 100) / 1000.0); // Random size
        msg.market_data.ask_price = to_fixed_price(adjusted_mid + half_spread);
        msg.market_data.ask_size = to_fixed_price(0.1 + (rng_() % 100) / 1000.0);
        
        // Try to write to buffer (non-blocking)
        if (!market_data_buffer_.write(msg)) {
            // Buffer full - could log this in production
            std::cerr << "⚠️  Market data buffer full\n";
        }
    }
    
    bool validate_price(const std::string& symbol, double& price) {
        auto& limits = price_limits_[symbol];
        
        // Absolute bounds check
        if (price < limits.min_price || price > limits.max_price) {
            std::cerr << "❌ Price out of bounds: " << symbol << " = " << price << std::endl;
            return false;
        }
        
        // Rate of change check
        if (last_valid_prices_.count(symbol)) {
            double last_price = last_valid_prices_[symbol];
            double change_pct = std::abs((price - last_price) / last_price);
            
            if (change_pct > limits.max_change_percent) {
                std::cerr << "⚠️ Price change too large: " << symbol << " " 
                         << (change_pct * 100) << "% change" << std::endl;
                // Clip to max change
                price = last_price * (1.0 + 
                    (price > last_price ? limits.max_change_percent : -limits.max_change_percent));
            }
        }
        
        last_valid_prices_[symbol] = price;
        return true;
    }
};

// Legacy ArbitrageStrategy class removed - replaced with intraday strategies
// See strategies/intraday_strategies.hpp for new trading strategies

} // namespace hft
