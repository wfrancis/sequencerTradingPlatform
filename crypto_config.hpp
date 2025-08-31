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
    
    struct VenueDepth {
        static constexpr size_t MAX_DEPTH = 5;
        Price bid_prices[MAX_DEPTH] = {0};
        Quantity bid_sizes[MAX_DEPTH] = {0};
        Price ask_prices[MAX_DEPTH] = {0};
        Quantity ask_sizes[MAX_DEPTH] = {0};
        uint8_t bid_depth = 0;
        uint8_t ask_depth = 0;
    };
    
    struct VenueData {
        Price bid_price = 0;
        Quantity bid_size = 0;
        Price ask_price = 0;
        Quantity ask_size = 0;
        uint64_t last_update_ns = 0;
        bool is_stale = true;
    };
    
    std::unordered_map<Venue, VenueData> venues;
    std::unordered_map<Venue, VenueDepth> venue_depths;
    
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
        // Enhanced arbitrage calculation with depth
        Quantity executable_quantity;  // Based on depth
        double weighted_avg_buy_price;
        double weighted_avg_sell_price;
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
    
    ArbitrageOpportunity find_arbitrage_with_depth() const {
        ArbitrageOpportunity best_opp{};
        best_opp.is_valid = false;
        best_opp.spread_bps = 0.0;
        
        // Check all venue pairs with depth consideration
        for (const auto& [venue1, data1] : venues) {
            for (const auto& [venue2, data2] : venues) {
                if (venue1 == venue2 || data1.is_stale || data2.is_stale) continue;
                
                // Get depth data if available
                auto depth1_it = venue_depths.find(venue1);
                auto depth2_it = venue_depths.find(venue2);
                
                if (depth1_it != venue_depths.end() && depth2_it != venue_depths.end()) {
                    const auto& depth1 = depth1_it->second;
                    const auto& depth2 = depth2_it->second;
                    
                    // Calculate executable quantity based on depth
                    Quantity exec_qty = 0;
                    double weighted_buy = 0.0, weighted_sell = 0.0;
                    double total_buy_cost = 0.0, total_sell_revenue = 0.0;
                    
                    // Match ask levels from venue1 with bid levels from venue2
                    for (size_t i = 0; i < depth1.ask_depth && i < depth2.bid_depth; ++i) {
                        if (depth2.bid_prices[i] > depth1.ask_prices[i]) {
                            Quantity level_qty = std::min(depth1.ask_sizes[i], depth2.bid_sizes[i]);
                            exec_qty += level_qty;
                            
                            total_buy_cost += to_float_price(depth1.ask_prices[i]) * to_float_price(level_qty);
                            total_sell_revenue += to_float_price(depth2.bid_prices[i]) * to_float_price(level_qty);
                        }
                    }
                    
                    if (exec_qty > 0) {
                        weighted_buy = total_buy_cost / to_float_price(exec_qty);
                        weighted_sell = total_sell_revenue / to_float_price(exec_qty);
                        
                        double spread = weighted_sell - weighted_buy;
                        double mid_price = (weighted_buy + weighted_sell) / 2.0;
                        double spread_bps = (spread / mid_price) * 10000.0;
                        
                        if (spread_bps > best_opp.spread_bps) {
                            best_opp = {
                                .buy_venue = venue1,
                                .sell_venue = venue2,
                                .buy_price = to_fixed_price(weighted_buy),
                                .sell_price = to_fixed_price(weighted_sell),
                                .spread_bps = spread_bps,
                                .max_quantity = exec_qty,
                                .is_valid = true,
                                .executable_quantity = exec_qty,
                                .weighted_avg_buy_price = weighted_buy,
                                .weighted_avg_sell_price = weighted_sell
                            };
                        }
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
