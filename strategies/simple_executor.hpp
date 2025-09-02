#pragma once

#include <random>
#include <chrono>
#include <string>
#include "intraday_strategies.hpp"

namespace hft::strategies {

// Simplified order result structure
struct OrderResult {
    enum Status { PENDING, FILLED, REJECTED, PARTIALLY_FILLED } status = PENDING;
    
    std::string order_id;
    std::string symbol;
    Signal::Direction direction;
    double requested_quantity = 0.0;
    double filled_quantity = 0.0;
    double requested_price = 0.0;
    double fill_price = 0.0;
    uint64_t fill_time = 0;
    double slippage_bps = 0.0;
    double commission = 0.0;
    std::string rejection_reason;
    
    bool is_filled() const { return status == FILLED; }
    bool is_rejected() const { return status == REJECTED; }
    double get_slippage() const { return slippage_bps; }
};

// Position size for execution
struct ExecutionSize {
    double shares = 0.0;
    double notional_value = 0.0;
    bool is_valid = false;
};

class SimpleExecutor {
private:
    // Slippage model parameters
    struct SlippageModel {
        double base_slippage_bps = 2.0;        // Base slippage: 2 bps
        double opening_hour_multiplier = 2.5;   // Higher slippage during opening
        double volume_impact_factor = 0.1;      // Impact based on order size
        double spread_multiplier = 0.5;         // Slippage relative to spread
    } slippage_model_;
    
    // Commission structure
    struct CommissionModel {
        double per_share = 0.005;              // $0.005 per share
        double minimum_commission = 1.00;      // $1.00 minimum
        double maximum_commission = 50.00;     // $50.00 maximum
    } commission_model_;
    
    // Execution simulation
    mutable std::mt19937 rng_;
    std::uniform_real_distribution<double> slippage_noise_;
    
    uint64_t next_order_id_ = 1;
    
    // Current market state (simplified)
    std::unordered_map<std::string, double> current_bids_;
    std::unordered_map<std::string, double> current_asks_;
    std::unordered_map<std::string, double> current_spreads_bps_;
    
public:
    SimpleExecutor() : rng_(std::random_device{}()), slippage_noise_(-0.5, 0.5) {
        // Initialize with some market data
        update_market_data("SPY", 450.00, 450.02);
        update_market_data("QQQ", 375.00, 375.02);
        update_market_data("AAPL", 175.00, 175.03);
    }
    
    // Update current market prices for slippage calculation
    void update_market_data(const std::string& symbol, double bid, double ask) {
        current_bids_[symbol] = bid;
        current_asks_[symbol] = ask;
        
        double mid = (bid + ask) / 2.0;
        current_spreads_bps_[symbol] = mid > 0 ? ((ask - bid) / mid) * 10000.0 : 10.0;
    }
    
    // Execute market order with realistic slippage
    OrderResult execute_market_order(const std::string& symbol, Signal::Direction direction, 
                                   double quantity) {
        OrderResult result;
        result.order_id = "ORD_" + std::to_string(next_order_id_++);
        result.symbol = symbol;
        result.direction = direction;
        result.requested_quantity = quantity;
        result.fill_time = get_timestamp_ns();
        
        // Check if we have market data
        if (current_bids_.find(symbol) == current_bids_.end()) {
            result.status = OrderResult::REJECTED;
            result.rejection_reason = "No market data available for " + symbol;
            return result;
        }
        
        // Get current market
        double current_bid = current_bids_[symbol];
        double current_ask = current_asks_[symbol];
        double current_spread_bps = current_spreads_bps_[symbol];
        
        // Determine base execution price
        double base_price = (direction == Signal::LONG) ? current_ask : current_bid;
        result.requested_price = base_price;
        
        // Calculate realistic slippage
        double slippage_bps = calculate_slippage(symbol, quantity, current_spread_bps);
        result.slippage_bps = slippage_bps;
        
        // Apply slippage to get fill price
        double slippage_factor = slippage_bps / 10000.0;
        if (direction == Signal::LONG) {
            result.fill_price = base_price * (1.0 + slippage_factor); // Pay more when buying
        } else {
            result.fill_price = base_price * (1.0 - slippage_factor); // Receive less when selling
        }
        
        // For intraday trading, assume fills are immediate but with slippage
        result.filled_quantity = quantity;
        result.status = OrderResult::FILLED;
        
        // Calculate commission
        result.commission = calculate_commission(quantity);
        
        return result;
    }
    
    // Execute limit order (simplified - immediate fill if price is favorable)
    OrderResult execute_limit_order(const std::string& symbol, Signal::Direction direction,
                                  double quantity, double limit_price) {
        OrderResult result;
        result.order_id = "LMT_" + std::to_string(next_order_id_++);
        result.symbol = symbol;
        result.direction = direction;
        result.requested_quantity = quantity;
        result.requested_price = limit_price;
        result.fill_time = get_timestamp_ns();
        
        // Check market data
        if (current_bids_.find(symbol) == current_bids_.end()) {
            result.status = OrderResult::REJECTED;
            result.rejection_reason = "No market data available";
            return result;
        }
        
        double current_bid = current_bids_[symbol];
        double current_ask = current_asks_[symbol];
        
        // Check if limit order can be filled immediately
        bool can_fill = false;
        double fill_price = limit_price;
        
        if (direction == Signal::LONG && limit_price >= current_ask) {
            // Buying at or above ask - can fill at ask (or better)
            can_fill = true;
            fill_price = std::min(limit_price, current_ask);
        } else if (direction == Signal::SHORT && limit_price <= current_bid) {
            // Selling at or below bid - can fill at bid (or better)
            can_fill = true;
            fill_price = std::max(limit_price, current_bid);
        }
        
        if (can_fill) {
            result.status = OrderResult::FILLED;
            result.filled_quantity = quantity;
            result.fill_price = fill_price;
            result.slippage_bps = ((fill_price - limit_price) / limit_price) * 10000.0;
            result.commission = calculate_commission(quantity);
        } else {
            // In a real system, this would be a pending order
            // For simplicity, we'll reject unfillable limit orders
            result.status = OrderResult::REJECTED;
            result.rejection_reason = "Limit price not immediately fillable";
        }
        
        return result;
    }
    
    // Execute signal with appropriate order type
    OrderResult execute_signal(const Signal& signal, double quantity) {
        if (!signal.is_valid || quantity <= 0) {
            OrderResult result;
            result.status = OrderResult::REJECTED;
            result.rejection_reason = "Invalid signal or quantity";
            return result;
        }
        
        // For intraday strategies, use market orders for immediate execution
        return execute_market_order(signal.symbol, signal.direction, quantity);
    }
    
    // Calculate P&L from an execution
    double calculate_pnl(const OrderResult& entry_order, const OrderResult& exit_order) const {
        if (!entry_order.is_filled() || !exit_order.is_filled()) {
            return 0.0;
        }
        
        if (entry_order.symbol != exit_order.symbol) {
            return 0.0; // Different symbols
        }
        
        double gross_pnl = 0.0;
        
        if (entry_order.direction == Signal::LONG) {
            // Long position: profit when exit price > entry price
            gross_pnl = (exit_order.fill_price - entry_order.fill_price) * entry_order.filled_quantity;
        } else {
            // Short position: profit when exit price < entry price  
            gross_pnl = (entry_order.fill_price - exit_order.fill_price) * entry_order.filled_quantity;
        }
        
        // Subtract commissions
        double net_pnl = gross_pnl - entry_order.commission - exit_order.commission;
        
        return net_pnl;
    }
    
    void print_status() const {
        std::cout << "\n💼 SimpleExecutor Status:\n";
        std::cout << "   Next Order ID: " << next_order_id_ << "\n";
        std::cout << "   Tracked Symbols: " << current_bids_.size() << "\n";
        
        for (const auto& [symbol, bid] : current_bids_) {
            double ask = current_asks_.at(symbol);
            double spread = current_spreads_bps_.at(symbol);
            std::cout << "   " << symbol << ": " << std::fixed << std::setprecision(2)
                      << bid << "/" << ask << " (spread: " << spread << " bps)\n";
        }
    }

private:
    double calculate_slippage(const std::string& symbol, double quantity, double spread_bps) {
        double slippage = slippage_model_.base_slippage_bps;
        
        // Time-based slippage adjustment
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        // Higher slippage during opening hour
        if ((tm.tm_hour == 9 && tm.tm_min >= 30) || (tm.tm_hour == 10 && tm.tm_min < 30)) {
            slippage *= slippage_model_.opening_hour_multiplier;
        }
        
        // Volume impact (larger orders have more slippage)
        double notional = quantity * 100.0; // Approximate notional value
        if (notional > 10000) { // Orders > $10k
            slippage += (notional / 10000.0) * slippage_model_.volume_impact_factor;
        }
        
        // Spread-based slippage (wider spreads = more slippage)
        slippage += spread_bps * slippage_model_.spread_multiplier;
        
        // Add some randomness
        slippage += slippage_noise_(rng_);
        
        return std::max(slippage, 0.1); // Minimum 0.1 bps slippage
    }
    
    double calculate_commission(double quantity) const {
        double commission = quantity * commission_model_.per_share;
        commission = std::max(commission, commission_model_.minimum_commission);
        commission = std::min(commission, commission_model_.maximum_commission);
        return commission;
    }
};

} // namespace hft::strategies
