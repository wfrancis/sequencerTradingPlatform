#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <chrono>
#include "messages.hpp"

namespace hft {

/**
 * Configuration for crypto trading MVP
 */
struct CryptoConfig {
    // Trading pairs to monitor
    std::vector<std::string> symbols = {"BTCUSD", "ETHUSD"};
    
    // Exchange endpoints
    struct ExchangeConfig {
        std::string websocket_url;
        std::string rest_api_url;
        std::string api_key;
        std::string api_secret;
        bool sandbox_mode = true;  // Start with sandbox
    };
    
    std::unordered_map<Venue, ExchangeConfig> exchanges = {
        {Venue::COINBASE, {
            .websocket_url = "wss://ws-feed-public.sandbox.exchange.coinbase.com",
            .rest_api_url = "https://api-public.sandbox.exchange.coinbase.com",
            .api_key = "",
            .api_secret = "",
            .sandbox_mode = true
        }},
        {Venue::BINANCE, {
            .websocket_url = "wss://testnet.binance.vision/ws/btcusdt@ticker",
            .rest_api_url = "https://testnet.binance.vision/api/v3",
            .api_key = "",
            .api_secret = "",
            .sandbox_mode = true
        }}
    };
    
    // Risk limits for MVP
    struct RiskLimits {
        double max_position_size = 0.01;  // Small positions for testing
        double max_order_size = 0.001;    // Tiny orders
        double max_daily_loss = 100.0;    // $100 max loss
        double min_spread_threshold = 0.001; // Min spread for arb
    } risk;
    
    // Strategy parameters
    struct StrategyParams {
        double arbitrage_threshold = 0.002;  // 20 bps minimum spread
        uint64_t max_latency_ms = 1000;      // 1s max for laptop
        double position_size = 0.001;        // Small test size
        bool paper_trading = true;           // Start with paper trading
    } strategy;
    
    // System settings
    struct SystemConfig {
        size_t ring_buffer_size = 1024;     // Smaller for laptop
        uint32_t tick_frequency_ms = 100;   // 100ms refresh for MVP
        std::string log_file = "hft_mvp.log";
        bool enable_console_log = true;
    } system;
};

/**
 * Symbol mapping between exchanges
 */
class SymbolMapper {
private:
    std::unordered_map<std::string, std::unordered_map<Venue, std::string>> symbol_map_;
    
public:
    SymbolMapper() {
        // BTC mappings
        symbol_map_["BTCUSD"] = {
            {Venue::COINBASE, "BTC-USD"},
            {Venue::BINANCE, "BTCUSDT"}
        };
        
        // ETH mappings  
        symbol_map_["ETHUSD"] = {
            {Venue::COINBASE, "ETH-USD"},
            {Venue::BINANCE, "ETHUSDT"}
        };
    }
    
    std::string get_exchange_symbol(const std::string& internal_symbol, Venue venue) const {
        auto it = symbol_map_.find(internal_symbol);
        if (it != symbol_map_.end()) {
            auto venue_it = it->second.find(venue);
            if (venue_it != it->second.end()) {
                return venue_it->second;
            }
        }
        return internal_symbol; // fallback
    }
    
    SymbolId get_symbol_id(const std::string& symbol) const {
        if (symbol == "BTCUSD") return 1;
        if (symbol == "ETHUSD") return 2;
        return 0;
    }
};

/**
 * Market data aggregator for cross-exchange arbitrage
 */
struct MarketSnapshot {
    uint64_t timestamp_ns;
    SymbolId symbol_id;
    
    struct VenueData {
        Price bid_price = 0;
        Quantity bid_size = 0;
        Price ask_price = 0;
        Quantity ask_size = 0;
        uint64_t last_update_ns = 0;
        bool is_stale = true;
    };
    
    std::unordered_map<Venue, VenueData> venues;
    
    /**
     * Calculate arbitrage opportunity
     */
    struct ArbitrageOpportunity {
        Venue buy_venue;
        Venue sell_venue;
        Price buy_price;
        Price sell_price;
        double spread_bps;
        Quantity max_quantity;
        bool is_valid;
    };
    
    ArbitrageOpportunity find_arbitrage() const {
        ArbitrageOpportunity best_opp{};
        best_opp.is_valid = false;
        best_opp.spread_bps = 0.0;
        
        // Check all venue pairs
        for (const auto& [venue1, data1] : venues) {
            for (const auto& [venue2, data2] : venues) {
                if (venue1 == venue2 || data1.is_stale || data2.is_stale) continue;
                
                // Buy on venue1, sell on venue2
                if (data1.ask_price > 0 && data2.bid_price > 0 && 
                    data2.bid_price > data1.ask_price) {
                    
                    double spread = to_float_price(data2.bid_price - data1.ask_price);
                    double mid_price = to_float_price((data1.ask_price + data2.bid_price) / 2);
                    double spread_bps = (spread / mid_price) * 10000.0;
                    
                    if (spread_bps > best_opp.spread_bps) {
                        best_opp = {
                            .buy_venue = venue1,
                            .sell_venue = venue2,
                            .buy_price = data1.ask_price,
                            .sell_price = data2.bid_price,
                            .spread_bps = spread_bps,
                            .max_quantity = std::min(data1.ask_size, data2.bid_size),
                            .is_valid = true
                        };
                    }
                }
            }
        }
        
        return best_opp;
    }
    
    bool is_data_fresh(uint64_t max_age_ns = 5000000000ULL) const { // 5 second default
        // Use portable high-resolution clock for timestamp
        auto now_chrono = std::chrono::high_resolution_clock::now();
        uint64_t now = static_cast<uint64_t>(now_chrono.time_since_epoch().count());
        for (const auto& [venue, data] : venues) {
            if (now - data.last_update_ns > max_age_ns) {
                return false;
            }
        }
        return true;
    }
};

} // namespace hft
