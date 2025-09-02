#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <random>
#include "intraday_strategies.hpp"
#include "technical_indicators.hpp"

namespace hft::strategies {

// Enhanced market data feed for intraday trading
class EnhancedMarketDataFeed {
public:
    // Single exchange focus (NYSE for SPY/QQQ)
    static constexpr const char* PRIMARY_VENUE = "NYSE";
    
    // Reduced symbol set for intraday trading
    static const std::vector<std::string> SYMBOLS;
    
private:
    std::atomic<bool> running_{false};
    std::thread feed_thread_;
    
    // Technical indicators for each symbol
    std::unordered_map<std::string, std::unique_ptr<indicators::IndicatorManager>> indicators_;
    
    // Market data simulation parameters
    struct MarketSimulation {
        std::unordered_map<std::string, double> current_prices;
        std::unordered_map<std::string, double> day_opens;
        std::unordered_map<std::string, double> prev_closes;
        std::unordered_map<std::string, uint64_t> cumulative_volumes;
        
        // Simulation parameters
        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<double> price_walk{0.0, 0.0002}; // Small movements
        std::uniform_int_distribution<uint64_t> volume_dist{100, 1000};
    } simulation_;
    
    // Callbacks for market data delivery
    std::function<void(const EnhancedMarketData&)> data_callback_;
    
public:
    EnhancedMarketDataFeed() {
        initialize_symbols();
        initialize_market_simulation();
    }
    
    ~EnhancedMarketDataFeed() {
        stop();
    }
    
    void set_data_callback(std::function<void(const EnhancedMarketData&)> callback) {
        data_callback_ = callback;
    }
    
    void start() {
        if (running_.exchange(true)) {
            return; // Already running
        }
        
        feed_thread_ = std::thread(&EnhancedMarketDataFeed::feed_loop, this);
        std::cout << "📡 Enhanced market data feed started (NYSE: SPY, QQQ, AAPL)\n";
    }
    
    void stop() {
        if (!running_.exchange(false)) {
            return; // Already stopped
        }
        
        if (feed_thread_.joinable()) {
            feed_thread_.join();
        }
        
        std::cout << "📡 Enhanced feed handler stopped\n";
    }
    
    // Get current market data for a symbol
    EnhancedMarketData get_market_data(const std::string& symbol) {
        auto it = indicators_.find(symbol);
        if (it == indicators_.end()) {
            return EnhancedMarketData{}; // Empty data
        }
        
        return create_enhanced_market_data(symbol, *it->second);
    }
    
    // Get market data for all symbols
    std::vector<EnhancedMarketData> get_all_market_data() {
        std::vector<EnhancedMarketData> data_list;
        
        for (const auto& symbol : SYMBOLS) {
            auto data = get_market_data(symbol);
            if (data.timestamp_ns > 0) { // Valid data
                data_list.push_back(data);
            }
        }
        
        return data_list;
    }
    
    void print_status() const {
        std::cout << "\n📊 Enhanced Feed Status:\n";
        std::cout << "   Running: " << (running_ ? "YES" : "NO") << "\n";
        std::cout << "   Symbols: " << SYMBOLS.size() << "\n";
        
        for (const auto& symbol : SYMBOLS) {
            auto it = simulation_.current_prices.find(symbol);
            if (it != simulation_.current_prices.end()) {
                std::cout << "   " << symbol << ": $" << std::fixed << std::setprecision(2) 
                         << it->second << "\n";
            }
        }
    }

private:
    void initialize_symbols() {
        for (const auto& symbol : SYMBOLS) {
            indicators_[symbol] = std::make_unique<indicators::IndicatorManager>();
        }
    }
    
    void initialize_market_simulation() {
        // Initialize with realistic starting prices
        simulation_.current_prices["SPY"] = 450.00;
        simulation_.current_prices["QQQ"] = 375.00;
        simulation_.current_prices["AAPL"] = 175.00;
        
        // Set day opens (with small gaps)
        for (const auto& [symbol, price] : simulation_.current_prices) {
            double gap = (simulation_.rng() % 200 - 100) / 10000.0; // ±1% gap
            simulation_.day_opens[symbol] = price * (1.0 + gap);
            simulation_.prev_closes[symbol] = price;
            simulation_.cumulative_volumes[symbol] = 0;
        }
        
        // Initialize indicators with session data
        for (const auto& [symbol, indicator] : indicators_) {
            indicator->set_session_data(simulation_.day_opens[symbol], 
                                      simulation_.prev_closes[symbol]);
            
            // Set historical volume averages (realistic values)
            if (symbol == "SPY") indicator->set_historical_volume(50000000); // 50M avg
            else if (symbol == "QQQ") indicator->set_historical_volume(35000000); // 35M avg
            else if (symbol == "AAPL") indicator->set_historical_volume(40000000); // 40M avg
        }
    }
    
    void feed_loop() {
        std::cout << "📈 Market data simulation started\n";
        
        while (running_.load()) {
            for (const auto& symbol : SYMBOLS) {
                update_symbol_data(symbol);
                
                if (data_callback_) {
                    auto data = get_market_data(symbol);
                    if (data.timestamp_ns > 0) {
                        data_callback_(data);
                    }
                }
            }
            
            // Update every 1 second for intraday strategies (not HFT speed needed)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    void update_symbol_data(const std::string& symbol) {
        auto& current_price = simulation_.current_prices[symbol];
        
        // Generate realistic price movement
        double price_change = simulation_.price_walk(simulation_.rng);
        
        // Add some correlation between SPY and QQQ
        if (symbol == "QQQ" && simulation_.current_prices.count("SPY")) {
            double spy_price = simulation_.current_prices["SPY"];
            double correlation_factor = 0.7; // 70% correlation
            price_change = price_change * correlation_factor + 
                          (spy_price / 450.0 - 1.0) * 0.1 * (1.0 - correlation_factor);
        }
        
        current_price += current_price * price_change;
        
        // Generate volume
        uint64_t volume = simulation_.volume_dist(simulation_.rng);
        simulation_.cumulative_volumes[symbol] += volume;
        
        // Update technical indicators
        auto& indicator = indicators_[symbol];
        indicator->update(current_price, volume, get_timestamp_ns());
    }
    
    EnhancedMarketData create_enhanced_market_data(const std::string& symbol, 
                                                  const indicators::IndicatorManager& indicator) {
        EnhancedMarketData data;
        
        // Basic market data
        data.symbol = symbol;
        double current_price = simulation_.current_prices[symbol];
        double spread = current_price * 0.0001; // 1 bps spread
        
        data.bid = current_price - spread / 2.0;
        data.ask = current_price + spread / 2.0;
        data.last = current_price;
        data.bid_size = 100 + (simulation_.rng() % 900); // 100-1000 shares
        data.ask_size = 100 + (simulation_.rng() % 900);
        
        // VWAP and volume data
        data.vwap = indicator.get_vwap();
        data.day_open = indicator.get_day_open();
        data.prev_close = indicator.get_prev_close();
        data.cumulative_volume = indicator.get_cumulative_volume();
        data.average_volume_30d = indicator.get_average_volume_30d();
        
        // Technical indicators
        data.rsi_5 = indicator.get_rsi_5();
        data.rsi_14 = indicator.get_rsi_14();
        data.std_dev_20 = indicator.get_std_dev_20();
        data.distance_from_vwap_percent = indicator.get_distance_from_vwap_percent();
        
        data.timestamp_ns = get_timestamp_ns();
        
        return data;
    }
};

// Initialize static member
const std::vector<std::string> EnhancedMarketDataFeed::SYMBOLS = {"SPY", "QQQ", "AAPL"};

} // namespace hft::strategies
