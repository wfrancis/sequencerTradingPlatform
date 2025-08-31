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
#include "crypto_config.hpp"
#include "position_tracker.hpp"
#include "enhanced_risk_manager.hpp"

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

/**
 * Enhanced arbitrage strategy with position tracking and risk management
 */
class ArbitrageStrategy {
private:
    SPSCRingBuffer<1024>& market_data_buffer_;
    SPSCRingBuffer<1024>& signal_buffer_;
    const CryptoConfig& config_;
    PositionTracker* position_tracker_;
    EnhancedRiskManager* risk_manager_;
    
    // Market state tracking
    std::unordered_map<SymbolId, MarketSnapshot> market_snapshots_;
    
    // Performance tracking
    uint64_t signals_generated_ = 0;
    uint64_t trades_executed_ = 0;
    uint64_t trades_rejected_ = 0;

public:
    ArbitrageStrategy(SPSCRingBuffer<1024>& market_buffer,
                     SPSCRingBuffer<1024>& signal_buffer,
                     const CryptoConfig& config,
                     PositionTracker* position_tracker,
                     EnhancedRiskManager* risk_manager)
        : market_data_buffer_(market_buffer), signal_buffer_(signal_buffer), 
          config_(config), position_tracker_(position_tracker), risk_manager_(risk_manager) {}
    
    void process_market_data() {
        SequencedMessage msg;
        
        // Process all available market data
        while (market_data_buffer_.read(msg)) {
            if (msg.type == MessageType::MARKET_DATA_TICK) {
                update_market_snapshot(msg);
                
                // Update position tracker with latest market prices
                std::string exchange = venue_name(msg.venue);
                std::string symbol = (msg.symbol_id == 1) ? "BTC/USD" : "ETH/USD";
                double mid_price = (to_float_price(msg.market_data.bid_price) + 
                                  to_float_price(msg.market_data.ask_price)) / 2.0;
                position_tracker_->update_market_price(exchange, symbol, mid_price);
                
                check_arbitrage_opportunities(msg.symbol_id);
            }
        }
    }
    
    void print_status() const {
        std::cout << "\n📊 Enhanced Strategy Status:\n";
        std::cout << "   Signals Generated: " << signals_generated_ << "\n";
        std::cout << "   Trades Executed: " << trades_executed_ << "\n";
        std::cout << "   Trades Rejected: " << trades_rejected_ << "\n";
        std::cout << "   Success Rate: " << std::fixed << std::setprecision(1) 
                  << (signals_generated_ > 0 ? (100.0 * trades_executed_ / signals_generated_) : 0.0) << "%\n";
        
        // Show risk status
        std::cout << "   Trading Halted: " << (risk_manager_->is_trading_halted() ? "YES" : "NO") << "\n";
        
        for (const auto& [symbol_id, snapshot] : market_snapshots_) {
            std::string symbol = (symbol_id == 1) ? "BTC" : "ETH";
            std::cout << "   " << symbol << " Markets:\n";
            
            for (const auto& [venue, data] : snapshot.venues) {
                if (!data.is_stale) {
                    std::cout << "     " << venue_name(venue) << ": "
                              << std::fixed << std::setprecision(2)
                              << to_float_price(data.bid_price) << " / " 
                              << to_float_price(data.ask_price) << "\n";
                }
            }
            
            auto arb = snapshot.find_arbitrage();
            if (arb.is_valid && arb.spread_bps > config_.strategy.arbitrage_threshold * 10000) {
                std::cout << "   🎯 Arbitrage: " << std::fixed << std::setprecision(1) 
                          << arb.spread_bps << " bps (" << venue_name(arb.buy_venue) 
                          << " → " << venue_name(arb.sell_venue) << ")\n";
            }
        }
    }

private:
    void update_market_snapshot(const SequencedMessage& msg) {
        MarketSnapshot& snapshot = market_snapshots_[msg.symbol_id];
        snapshot.symbol_id = msg.symbol_id;
        snapshot.timestamp_ns = msg.timestamp_ns;
        
        auto& venue_data = snapshot.venues[msg.venue];
        venue_data.bid_price = msg.market_data.bid_price;
        venue_data.bid_size = msg.market_data.bid_size;
        venue_data.ask_price = msg.market_data.ask_price;
        venue_data.ask_size = msg.market_data.ask_size;
        venue_data.last_update_ns = msg.timestamp_ns;
        venue_data.is_stale = false;
    }
    
    void check_arbitrage_opportunities(SymbolId symbol_id) {
        const auto& snapshot = market_snapshots_[symbol_id];
        auto opportunity = snapshot.find_arbitrage();
        
        if (opportunity.is_valid && 
            opportunity.spread_bps > config_.strategy.arbitrage_threshold * 10000) {
            
            signals_generated_++;
            
            // Enhanced risk checks before signal generation
            std::string buy_exchange = venue_name(opportunity.buy_venue);
            std::string sell_exchange = venue_name(opportunity.sell_venue);
            std::string symbol_str = (symbol_id == 1) ? "BTC/USD" : "ETH/USD";
            
            // Determine trade quantities (conservative for MVP)
            double trade_quantity = (symbol_id == 1) ? 0.001 : 0.01; // 0.001 BTC or 0.01 ETH
            
            // Check if we can execute both legs of the arbitrage
            bool can_buy = risk_manager_->can_execute_trade(
                buy_exchange, symbol_str, trade_quantity, opportunity.buy_price, opportunity.spread_bps);
            bool can_sell = risk_manager_->can_execute_trade(
                sell_exchange, symbol_str, -trade_quantity, opportunity.sell_price, opportunity.spread_bps);
            
            if (can_buy && can_sell) {
                // Risk checks passed - generate trading signal
                SequencedMessage signal{};
                signal.type = MessageType::SIGNAL;
                signal.symbol_id = symbol_id;
                signal.strategy_id = 1; // Arbitrage strategy
                
                signal.signal.signal_type = 1; // Arbitrage signal
                signal.signal.strength = std::min(1.0f, static_cast<float>(opportunity.spread_bps / 100.0f));
                signal.signal.target_price = (opportunity.buy_price + opportunity.sell_price) / 2;
                signal.signal.confidence = 0.8f;
                signal.signal.expected_edge = static_cast<float>(opportunity.spread_bps);
                signal.signal.hold_time_ms = 1000; // 1 second expected hold
                
                if (signal_buffer_.write(signal)) {
                    // Simulate paper trade execution
                    execute_paper_arbitrage(symbol_str, trade_quantity, 
                                          opportunity.buy_price, opportunity.sell_price,
                                          buy_exchange, sell_exchange, opportunity.spread_bps);
                    trades_executed_++;
                }
            } else {
                trades_rejected_++;
                printf("❌ Arbitrage rejected by risk manager: %s %.1f bps\n", 
                       symbol_str.c_str(), opportunity.spread_bps);
            }
        }
    }
    
    void execute_paper_arbitrage(const std::string& symbol, double quantity,
                               double buy_price, double sell_price,
                               const std::string& buy_exchange, const std::string& sell_exchange,
                               double spread_bps) {
        // Simulate simultaneous buy and sell orders
        position_tracker_->add_trade(buy_exchange, symbol, quantity, buy_price);   // Buy
        position_tracker_->add_trade(sell_exchange, symbol, -quantity, sell_price); // Sell
        
        double profit = quantity * (sell_price - buy_price);
        printf("🎯 Arbitrage Signal: %.1f bps edge, strength: %.1f\n", 
               spread_bps, std::min(1.0, spread_bps / 100.0));
        printf("   📝 Paper trades: Buy %.6f %s @ $%.2f (%s), Sell @ $%.2f (%s)\n",
               quantity, symbol.c_str(), buy_price, buy_exchange.c_str(), 
               sell_price, sell_exchange.c_str());
        printf("   💰 Expected profit: $%.2f\n", profit);
    }
    
private:
    std::string venue_name(Venue venue) const {
        switch (venue) {
            case Venue::COINBASE: return "Coinbase";
            case Venue::BINANCE: return "Binance";
            default: return "Unknown";
        }
    }
};

} // namespace hft
