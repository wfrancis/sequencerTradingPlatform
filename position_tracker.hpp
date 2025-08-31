#pragma once

#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <fstream>
#include "messages.hpp"

struct Position {
    double quantity = 0.0;      // Net position (+ long, - short)
    double avg_price = 0.0;     // Volume-weighted average price
    double realized_pnl = 0.0;  // Realized profit/loss
    double unrealized_pnl = 0.0; // Mark-to-market P&L
    double total_volume = 0.0;  // Total traded volume
    uint64_t last_update_ns = 0;
    
    void add_trade(double trade_qty, double trade_price) {
        if ((quantity > 0 && trade_qty < 0) || (quantity < 0 && trade_qty > 0)) {
            // Closing position - calculate realized P&L
            double closing_qty = std::min(std::abs(trade_qty), std::abs(quantity));
            double closing_pnl = closing_qty * (trade_price - avg_price) * (quantity > 0 ? 1 : -1);
            realized_pnl += closing_pnl;
        }
        
        // Update position
        if (quantity == 0.0) {
            avg_price = trade_price;
        } else if ((quantity > 0 && trade_qty > 0) || (quantity < 0 && trade_qty < 0)) {
            // Adding to position - update average price
            double new_qty = quantity + trade_qty;
            avg_price = (avg_price * std::abs(quantity) + trade_price * std::abs(trade_qty)) / std::abs(new_qty);
        }
        
        quantity += trade_qty;
        total_volume += std::abs(trade_qty);
        last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    void update_unrealized_pnl(double mark_price) {
        if (quantity != 0.0) {
            unrealized_pnl = quantity * (mark_price - avg_price);
        } else {
            unrealized_pnl = 0.0;
        }
    }
    
    double total_pnl() const {
        return realized_pnl + unrealized_pnl;
    }
};

struct ExchangeBalance {
    double btc_balance = 1.0;    // Starting with 1 BTC
    double eth_balance = 10.0;   // Starting with 10 ETH  
    double usd_balance = 50000.0; // Starting with $50k USD
    double btc_available = 1.0;   // Available for trading
    double eth_available = 10.0;
    double usd_available = 50000.0;
    uint64_t last_update_ns = 0;
    
    void update_balance(const std::string& asset, double delta_total, double delta_available) {
        if (asset == "BTC") {
            btc_balance += delta_total;
            btc_available += delta_available;
        } else if (asset == "ETH") {
            eth_balance += delta_total;
            eth_available += delta_available;
        } else if (asset == "USD") {
            usd_balance += delta_total;
            usd_available += delta_available;
        }
        last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    bool has_sufficient_balance(const std::string& asset, double required_amount) const {
        if (asset == "BTC") return btc_available >= required_amount;
        if (asset == "ETH") return eth_available >= required_amount;
        if (asset == "USD") return usd_available >= required_amount;
        return false;
    }
};

class PositionTracker {
private:
    // Position tracking: [exchange][symbol] -> Position
    std::unordered_map<std::string, std::unordered_map<std::string, Position>> positions_;
    
    // Balance tracking: [exchange] -> ExchangeBalance
    std::unordered_map<std::string, ExchangeBalance> balances_;
    
    // Market prices for P&L calculation: [exchange][symbol] -> price
    std::unordered_map<std::string, std::unordered_map<std::string, double>> market_prices_;
    
    // Performance tracking
    double total_realized_pnl_ = 0.0;
    double daily_pnl_ = 0.0;
    double max_drawdown_ = 0.0;
    double peak_equity_ = 100000.0; // Starting equity
    uint64_t trading_session_start_ = 0;
    
    mutable std::mutex mutex_;
    std::atomic<bool> logging_enabled_{true};
    
    // Slippage modeling
    struct SlippageModel {
        double linear_impact = 0.0001;    // 1 bp per unit
        double square_root_impact = 0.001; // Square root market impact
        double temporary_impact = 0.0005;  // Temporary price impact
        
        double calculate_slippage(double quantity, double avg_volume, 
                                 double spread, bool is_taker) const {
            double participation_rate = std::abs(quantity) / avg_volume;
            double permanent_impact = linear_impact * participation_rate + 
                                     square_root_impact * std::sqrt(participation_rate);
            double total_impact = permanent_impact;
            
            if (is_taker) {
                total_impact += spread / 2.0 + temporary_impact;
            }
            
            return total_impact;
        }
    };
    
    std::unordered_map<std::string, SlippageModel> slippage_models_;
    std::unordered_map<std::string, double> average_volumes_;
    
    void log_trade(const std::string& exchange, const std::string& symbol, 
                   double quantity, double price, const std::string& side) {
        if (!logging_enabled_) return;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ofstream log_file("trade_log.csv", std::ios::app);
        if (log_file.is_open()) {
            log_file << time_t << "," << exchange << "," << symbol << "," 
                    << side << "," << quantity << "," << price << "," 
                    << (quantity * price) << "\n";
        }
    }

public:
    PositionTracker() {
        trading_session_start_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // Initialize exchange balances
        balances_["COINBASE"] = ExchangeBalance{};
        balances_["BINANCE"] = ExchangeBalance{};
        
        // Initialize slippage models
        slippage_models_["BTC/USD"] = SlippageModel{};
        slippage_models_["ETH/USD"] = SlippageModel{};
        
        // Initialize average volumes (simplified estimates)
        average_volumes_["BTC/USD"] = 100.0;  // 100 BTC average volume
        average_volumes_["ETH/USD"] = 1000.0; // 1000 ETH average volume
        
        // Create trade log header
        std::ofstream log_file("trade_log.csv");
        if (log_file.is_open()) {
            log_file << "timestamp,exchange,symbol,side,quantity,price,notional\n";
        }
    }
    
    void update_market_price(const std::string& exchange, const std::string& symbol, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        market_prices_[exchange][symbol] = price;
        
        // Update unrealized P&L for this symbol
        if (positions_[exchange].count(symbol)) {
            positions_[exchange][symbol].update_unrealized_pnl(price);
        }
    }
    
    void add_trade(const std::string& exchange, const std::string& symbol, 
                   double quantity, double price) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update position
        positions_[exchange][symbol].add_trade(quantity, price);
        
        // Update balances (simplified - assumes USD settlement)
        double notional = quantity * price;
        if (symbol.find("BTC") != std::string::npos) {
            balances_[exchange].update_balance("BTC", quantity, quantity);
            balances_[exchange].update_balance("USD", -notional, -notional);
        } else if (symbol.find("ETH") != std::string::npos) {
            balances_[exchange].update_balance("ETH", quantity, quantity);
            balances_[exchange].update_balance("USD", -notional, -notional);
        }
        
        // Log the trade
        log_trade(exchange, symbol, quantity, price, quantity > 0 ? "BUY" : "SELL");
        
        // Update performance metrics
        update_performance_metrics();
    }
    
    void add_trade_with_slippage(const std::string& exchange, 
                                 const std::string& symbol,
                                 double quantity, 
                                 double quoted_price,
                                 bool is_taker = true) {
        double slippage = slippage_models_[symbol].calculate_slippage(
            quantity, average_volumes_[symbol], 0.001, is_taker);
        
        double executed_price = quoted_price * (1.0 + 
            (quantity > 0 ? slippage : -slippage));
        
        add_trade(exchange, symbol, quantity, executed_price);
    }
    
    Position get_position(const std::string& exchange, const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto exchange_it = positions_.find(exchange);
        if (exchange_it != positions_.end()) {
            auto symbol_it = exchange_it->second.find(symbol);
            if (symbol_it != exchange_it->second.end()) {
                return symbol_it->second;
            }
        }
        return Position{}; // Return empty position if not found
    }
    
    ExchangeBalance get_balance(const std::string& exchange) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = balances_.find(exchange);
        if (it != balances_.end()) {
            return it->second;
        }
        return ExchangeBalance{};
    }
    
    double get_total_equity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total_equity = 0.0;
        
        for (const auto& [exchange, balance] : balances_) {
            total_equity += balance.usd_balance;
            
            // Add BTC value (assuming current market price)
            if (market_prices_.count(exchange) && market_prices_.at(exchange).count("BTC/USD")) {
                double btc_price = market_prices_.at(exchange).at("BTC/USD");
                total_equity += balance.btc_balance * btc_price;
            }
            
            // Add ETH value
            if (market_prices_.count(exchange) && market_prices_.at(exchange).count("ETH/USD")) {
                double eth_price = market_prices_.at(exchange).at("ETH/USD");
                total_equity += balance.eth_balance * eth_price;
            }
        }
        
        return total_equity;
    }
    
    double get_total_unrealized_pnl() const {
        std::lock_guard<std::mutex> lock(mutex_);
        double total_unrealized = 0.0;
        
        for (const auto& [exchange, symbols] : positions_) {
            for (const auto& [symbol, position] : symbols) {
                total_unrealized += position.unrealized_pnl;
            }
        }
        
        return total_unrealized;
    }
    
    double get_total_realized_pnl() const {
        return total_realized_pnl_;
    }
    
    double get_daily_pnl() const {
        return daily_pnl_;
    }
    
    double get_max_drawdown() const {
        return max_drawdown_;
    }
    
    bool can_trade(const std::string& exchange, const std::string& symbol, 
                   double quantity, double price) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto balance_it = balances_.find(exchange);
        if (balance_it == balances_.end()) return false;
        
        const auto& balance = balance_it->second;
        double notional = std::abs(quantity * price);
        
        if (quantity > 0) {
            // Buying - need USD
            return balance.has_sufficient_balance("USD", notional);
        } else {
            // Selling - need the asset
            if (symbol.find("BTC") != std::string::npos) {
                return balance.has_sufficient_balance("BTC", std::abs(quantity));
            } else if (symbol.find("ETH") != std::string::npos) {
                return balance.has_sufficient_balance("ETH", std::abs(quantity));
            }
        }
        
        return false;
    }
    
    void print_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n📊 === POSITION TRACKER STATUS ===\n");
        printf("💰 Total Equity: $%.2f\n", get_total_equity());
        printf("📈 Total Realized P&L: $%.2f\n", total_realized_pnl_);
        printf("📊 Total Unrealized P&L: $%.2f\n", get_total_unrealized_pnl());
        printf("📅 Daily P&L: $%.2f\n", daily_pnl_);
        printf("📉 Max Drawdown: $%.2f\n", max_drawdown_);
        
        printf("\n🏛️ Exchange Balances:\n");
        for (const auto& [exchange, balance] : balances_) {
            printf("  %s: $%.2f USD, %.6f BTC, %.4f ETH\n", 
                   exchange.c_str(), balance.usd_available, 
                   balance.btc_available, balance.eth_available);
        }
        
        printf("\n📋 Active Positions:\n");
        for (const auto& [exchange, symbols] : positions_) {
            for (const auto& [symbol, position] : symbols) {
                if (std::abs(position.quantity) > 1e-8) {
                    printf("  %s %s: %.6f @ $%.2f (P&L: $%.2f)\n",
                           exchange.c_str(), symbol.c_str(), position.quantity,
                           position.avg_price, position.total_pnl());
                }
            }
        }
        printf("=====================================\n\n");
    }

private:
    void update_performance_metrics() {
        double current_equity = get_total_equity();
        
        // Update peak and drawdown
        if (current_equity > peak_equity_) {
            peak_equity_ = current_equity;
        } else {
            double drawdown = peak_equity_ - current_equity;
            if (drawdown > max_drawdown_) {
                max_drawdown_ = drawdown;
            }
        }
        
        // Update total realized P&L
        total_realized_pnl_ = 0.0;
        for (const auto& [exchange, symbols] : positions_) {
            for (const auto& [symbol, position] : symbols) {
                total_realized_pnl_ += position.realized_pnl;
            }
        }
        
        // Daily P&L (simplified - would need proper session tracking)
        daily_pnl_ = current_equity - 100000.0; // Starting equity
    }
};
