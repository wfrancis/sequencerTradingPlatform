#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <string>
#include <mutex>
#include <fstream>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>
#include "enhanced_risk_manager.hpp"
#include "position_tracker.hpp"
#include "messages.hpp"

namespace risk {

/**
 * Market Risk Metrics
 */
struct MarketRiskMetrics {
    double volatility_1m = 0.0;         // 1-minute rolling volatility
    double volatility_5m = 0.0;         // 5-minute rolling volatility  
    double volatility_1h = 0.0;         // 1-hour rolling volatility
    double var_1d_95 = 0.0;            // 1-day 95% VaR
    double var_1d_99 = 0.0;            // 1-day 99% VaR
    double skewness = 0.0;             // Return skewness
    double kurtosis = 0.0;             // Return kurtosis
    uint64_t last_update_ns = 0;
};

/**
 * Liquidity Risk Metrics
 */
struct LiquidityRiskMetrics {
    double bid_ask_spread_bps = 0.0;    // Current spread in basis points
    double avg_spread_1m = 0.0;        // 1-minute average spread
    double market_depth_bid = 0.0;     // Aggregate bid depth
    double market_depth_ask = 0.0;     // Aggregate ask depth
    double depth_ratio = 0.0;          // Bid/Ask depth ratio
    double market_impact_estimate = 0.0; // Estimated market impact
    bool is_liquid = true;             // Overall liquidity flag
    uint64_t last_update_ns = 0;
};

/**
 * Dynamic Risk Limits
 */
struct DynamicRiskLimits {
    RiskLimits base_limits;
    
    // Dynamic multipliers based on market conditions
    double volatility_multiplier = 1.0;    // Reduce limits in high vol
    double liquidity_multiplier = 1.0;     // Reduce limits in low liquidity
    double correlation_multiplier = 1.0;   // Reduce limits with high correlation
    double pnl_multiplier = 1.0;          // Reduce limits after losses
    
    // Calculated effective limits
    double effective_max_position_btc = 0.0;
    double effective_max_position_eth = 0.0;
    double effective_max_order_size = 0.0;
    double effective_min_spread_bps = 0.0;
    
    void calculate_effective_limits() {
        double combined_multiplier = std::min({
            volatility_multiplier,
            liquidity_multiplier,
            correlation_multiplier,
            pnl_multiplier
        });
        
        effective_max_position_btc = base_limits.max_btc_position * combined_multiplier;
        effective_max_position_eth = base_limits.max_eth_position * combined_multiplier;
        effective_max_order_size = base_limits.max_order_notional * combined_multiplier;
        effective_min_spread_bps = base_limits.min_spread_bps / combined_multiplier;
    }
};

/**
 * System Risk Metrics
 */
struct SystemRiskMetrics {
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    double network_latency_us = 0.0;
    uint32_t message_queue_depth = 0;
    uint32_t order_processing_delay_us = 0;
    bool exchange_connectivity = true;
    bool market_data_stale = false;
    uint32_t risk_violations_last_hour = 0;
    uint64_t last_heartbeat_ns = 0;
};

/**
 * Comprehensive Risk Manager
 * 
 * Enhances the basic risk manager with:
 * - Real-time market risk monitoring
 * - Dynamic risk limit adjustment
 * - Liquidity risk controls
 * - Advanced operational risk monitoring
 */
class ComprehensiveRiskManager {
private:
    // Core components
    std::unique_ptr<EnhancedRiskManager> enhanced_risk_manager_;
    PositionTracker* position_tracker_;
    
    // Risk metrics storage
    std::unordered_map<std::string, MarketRiskMetrics> market_risk_metrics_;
    std::unordered_map<std::string, LiquidityRiskMetrics> liquidity_risk_metrics_;
    SystemRiskMetrics system_risk_metrics_;
    DynamicRiskLimits dynamic_limits_;
    
    // Price history for calculations
    static constexpr size_t MAX_PRICE_HISTORY = 3600; // 1 hour at 1 second intervals
    std::unordered_map<std::string, std::deque<double>> price_history_;
    std::unordered_map<std::string, std::deque<uint64_t>> timestamp_history_;
    
    std::atomic<uint32_t> high_risk_events_today_{0};
    std::atomic<bool> emergency_risk_mode_{false};
    
    mutable std::mutex mutex_;
    std::atomic<bool> monitoring_enabled_{true};
    
    // Configuration
    struct RiskConfig {
        double volatility_threshold_high = 0.05;    // 5% high vol threshold
        double liquidity_threshold_low = 0.001;     // 0.1% low liquidity
        double var_threshold_percent = 0.02;        // 2% VaR threshold
        bool enable_dynamic_limits = true;
        bool enable_stress_testing = true;
    } config_;

public:
    ComprehensiveRiskManager(PositionTracker* position_tracker) 
        : position_tracker_(position_tracker),
          enhanced_risk_manager_(std::make_unique<EnhancedRiskManager>(position_tracker)) {
        
        printf("🛡️ Comprehensive Risk Manager initialized\n");
        printf("   Dynamic Risk Limits: %s\n", config_.enable_dynamic_limits ? "ENABLED" : "DISABLED");
        printf("   Stress Testing: %s\n", config_.enable_stress_testing ? "ENABLED" : "DISABLED");
    }
    
    /**
     * Enhanced trade validation with comprehensive risk checks
     */
    bool can_execute_trade(const std::string& exchange, const std::string& symbol,
                          double quantity, double price, double current_spread_bps = 0.0) {
        
        // 1. Run basic enhanced risk manager checks first
        if (!enhanced_risk_manager_->can_execute_trade(exchange, symbol, quantity, price, current_spread_bps)) {
            return false;
        }
        
        // 2. Market risk checks
        if (!check_market_risk(exchange, symbol, quantity, price)) {
            log_risk_event("MARKET_RISK_VIOLATION", exchange, symbol, 
                         "Market risk limits exceeded", std::abs(quantity * price));
            return false;
        }
        
        // 3. Liquidity risk checks
        if (!check_liquidity_risk(exchange, symbol, quantity, price)) {
            log_risk_event("LIQUIDITY_RISK_VIOLATION", exchange, symbol,
                         "Liquidity risk limits exceeded", current_spread_bps);
            return false;
        }
        
        // 4. Dynamic limit checks
        if (config_.enable_dynamic_limits && 
            !check_dynamic_limits(exchange, symbol, quantity, price)) {
            log_risk_event("DYNAMIC_LIMIT_VIOLATION", exchange, symbol,
                         "Dynamic risk limits exceeded", quantity);
            return false;
        }
        
        // 5. System risk checks
        if (!check_system_risk()) {
            log_risk_event("SYSTEM_RISK_VIOLATION", exchange, symbol,
                         "System risk concerns detected", 0.0);
            return false;
        }
        
        return true;
    }
    
    /**
     * Update market data and recalculate risk metrics
     */
    void update_market_data(const std::string& exchange, const std::string& symbol, 
                           double price, double bid, double ask, double volume) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // Update price history
        auto& prices = price_history_[symbol];
        auto& timestamps = timestamp_history_[symbol];
        
        prices.push_back(price);
        timestamps.push_back(current_time);
        
        // Maintain rolling window
        if (prices.size() > MAX_PRICE_HISTORY) {
            prices.pop_front();
            timestamps.pop_front();
        }
        
        // Update market risk metrics
        update_market_risk_metrics(symbol, prices, timestamps);
        
        // Update liquidity metrics
        update_liquidity_risk_metrics(exchange, symbol, bid, ask, volume);
        
        // Recalculate dynamic limits
        if (config_.enable_dynamic_limits) {
            update_dynamic_limits();
        }
    }
    
    /**
     * Emergency risk shutdown
     */
    void emergency_shutdown(const std::string& reason) {
        emergency_risk_mode_ = true;
        enhanced_risk_manager_->emergency_stop(reason);
        
        printf("🚨 EMERGENCY RISK SHUTDOWN: %s\n", reason.c_str());
        log_risk_event("EMERGENCY_SHUTDOWN", "SYSTEM", "ALL", reason, 0.0);
    }
    
    /**
     * Generate comprehensive risk report
     */
    void generate_risk_report() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n🛡️ === COMPREHENSIVE RISK REPORT ===\n");
        
        // System status
        printf("\n🚨 System Status:\n");
        printf("  Emergency Mode: %s\n", emergency_risk_mode_ ? "ACTIVE" : "OFF");
        printf("  Monitoring: %s\n", monitoring_enabled_ ? "ENABLED" : "DISABLED");
        printf("  High Risk Events Today: %d\n", high_risk_events_today_.load());
        
        // Market risk summary
        printf("\n📊 Market Risk Summary:\n");
        for (const auto& [symbol, metrics] : market_risk_metrics_) {
            printf("  %s: Vol=%.2f%% VaR95=%.2f%% Skew=%.2f\n",
                   symbol.c_str(), metrics.volatility_1h * 100, 
                   metrics.var_1d_95 * 100, metrics.skewness);
        }
        
        // Liquidity risk summary
        printf("\n💧 Liquidity Risk Summary:\n");
        for (const auto& [symbol, metrics] : liquidity_risk_metrics_) {
            printf("  %s: Spread=%.1fbps Depth=%.2f/%.2f Liquid=%s\n",
                   symbol.c_str(), metrics.bid_ask_spread_bps,
                   metrics.market_depth_bid, metrics.market_depth_ask,
                   metrics.is_liquid ? "YES" : "NO");
        }
        
        // Dynamic limits
        if (config_.enable_dynamic_limits) {
            printf("\n⚡ Dynamic Risk Limits:\n");
            printf("  Volatility Multiplier: %.2f\n", dynamic_limits_.volatility_multiplier);
            printf("  Liquidity Multiplier: %.2f\n", dynamic_limits_.liquidity_multiplier);
            printf("  Effective BTC Limit: %.4f\n", dynamic_limits_.effective_max_position_btc);
            printf("  Effective ETH Limit: %.2f\n", dynamic_limits_.effective_max_position_eth);
        }
        
        printf("=====================================\n\n");
    }
    
    // Accessors
    EnhancedRiskManager* get_enhanced_risk_manager() { return enhanced_risk_manager_.get(); }
    const MarketRiskMetrics& get_market_risk_metrics(const std::string& symbol) const {
        auto it = market_risk_metrics_.find(symbol);
        static MarketRiskMetrics empty{};
        return it != market_risk_metrics_.end() ? it->second : empty;
    }
    
    bool is_emergency_mode() const { return emergency_risk_mode_; }
    const DynamicRiskLimits& get_dynamic_limits() const { return dynamic_limits_; }

private:
    bool check_market_risk(const std::string& exchange, const std::string& symbol,
                          double quantity, double price) {
        auto it = market_risk_metrics_.find(symbol);
        if (it == market_risk_metrics_.end()) return true; // No data, allow trade
        
        const auto& metrics = it->second;
        
        // Check volatility threshold
        if (metrics.volatility_1h > config_.volatility_threshold_high) {
            return false;
        }
        
        // Check VaR threshold
        double position_var = std::abs(quantity * price) * metrics.var_1d_95;
        double total_equity = position_tracker_->get_total_equity();
        if (position_var > total_equity * config_.var_threshold_percent) {
            return false;
        }
        
        return true;
    }
    
    bool check_liquidity_risk(const std::string& exchange, const std::string& symbol,
                             double quantity, double price) {
        std::string key = exchange + ":" + symbol;
        auto it = liquidity_risk_metrics_.find(key);
        if (it == liquidity_risk_metrics_.end()) return true;
        
        const auto& metrics = it->second;
        
        // Check if market is liquid enough
        if (!metrics.is_liquid) return false;
        
        // Check market impact
        double notional = std::abs(quantity * price);
        double estimated_impact = notional * metrics.market_impact_estimate;
        if (estimated_impact > price * 0.01) { // 1% max impact
            return false;
        }
        
        return true;
    }
    
    bool check_dynamic_limits(const std::string& exchange, const std::string& symbol,
                             double quantity, double price) {
        double notional = std::abs(quantity * price);
        
        if (notional > dynamic_limits_.effective_max_order_size) {
            return false;
        }
        
        if (symbol.find("BTC") != std::string::npos && 
            std::abs(quantity) > dynamic_limits_.effective_max_position_btc) {
            return false;
        }
        
        if (symbol.find("ETH") != std::string::npos && 
            std::abs(quantity) > dynamic_limits_.effective_max_position_eth) {
            return false;
        }
        
        return true;
    }
    
    bool check_system_risk() {
        // Check system health metrics
        if (system_risk_metrics_.cpu_usage_percent > 90.0) return false;
        if (system_risk_metrics_.memory_usage_percent > 85.0) return false;
        if (!system_risk_metrics_.exchange_connectivity) return false;
        if (system_risk_metrics_.market_data_stale) return false;
        
        return true;
    }
    
    void update_market_risk_metrics(const std::string& symbol, 
                                   const std::deque<double>& prices,
                                   const std::deque<uint64_t>& timestamps) {
        if (prices.size() < 2) return;
        
        auto& metrics = market_risk_metrics_[symbol];
        
        // Calculate returns
        std::vector<double> returns;
        for (size_t i = 1; i < prices.size(); ++i) {
            double ret = std::log(prices[i] / prices[i-1]);
            returns.push_back(ret);
        }
        
        // Calculate volatility (annualized)
        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double variance = 0.0;
        for (double ret : returns) {
            variance += (ret - mean_return) * (ret - mean_return);
        }
        variance /= returns.size();
        
        metrics.volatility_1h = std::sqrt(variance * 365 * 24); // Annualized hourly vol
        
        // Calculate VaR (simple parametric)
        std::sort(returns.begin(), returns.end());
        size_t var_95_idx = static_cast<size_t>(returns.size() * 0.05);
        size_t var_99_idx = static_cast<size_t>(returns.size() * 0.01);
        
        if (var_95_idx < returns.size()) {
            metrics.var_1d_95 = -returns[var_95_idx];
        }
        if (var_99_idx < returns.size()) {
            metrics.var_1d_99 = -returns[var_99_idx];
        }
        
        metrics.last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    void update_liquidity_risk_metrics(const std::string& exchange, const std::string& symbol,
                                      double bid, double ask, double volume) {
        std::string key = exchange + ":" + symbol;
        auto& metrics = liquidity_risk_metrics_[key];
        
        double mid = (bid + ask) / 2.0;
        metrics.bid_ask_spread_bps = ((ask - bid) / mid) * 10000.0;
        metrics.market_depth_bid = bid * volume; // Simplified
        metrics.market_depth_ask = ask * volume;
        
        // Simple liquidity assessment
        metrics.is_liquid = (metrics.bid_ask_spread_bps < 100.0) && (volume > 0.01);
        
        metrics.last_update_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    void update_dynamic_limits() {
        dynamic_limits_.base_limits = enhanced_risk_manager_->get_limits();
        
        // Adjust based on market volatility
        double avg_vol = 0.0;
        size_t vol_count = 0;
        for (const auto& [symbol, metrics] : market_risk_metrics_) {
            avg_vol += metrics.volatility_1h;
            vol_count++;
        }
        if (vol_count > 0) {
            avg_vol /= vol_count;
            dynamic_limits_.volatility_multiplier = std::max(0.1, 1.0 - (avg_vol - 0.02) * 10.0);
        }
        
        // Adjust based on liquidity
        double avg_spread = 0.0;
        size_t spread_count = 0;
        for (const auto& [key, metrics] : liquidity_risk_metrics_) {
            avg_spread += metrics.bid_ask_spread_bps;
            spread_count++;
        }
        if (spread_count > 0) {
            avg_spread /= spread_count;
            dynamic_limits_.liquidity_multiplier = std::max(0.1, 1.0 - (avg_spread - 10.0) * 0.01);
        }
        
        // Adjust based on P&L
        double daily_pnl = position_tracker_->get_daily_pnl();
        if (daily_pnl < 0) {
            dynamic_limits_.pnl_multiplier = std::max(0.5, 1.0 + daily_pnl / 1000.0);
        }
        
        dynamic_limits_.calculate_effective_limits();
    }
    
    void log_risk_event(const std::string& type, const std::string& exchange,
                       const std::string& symbol, const std::string& description,
                       double value) {
        high_risk_events_today_++;
        
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        std::ofstream log_file("comprehensive_risk_events.csv", std::ios::app);
        if (log_file.is_open()) {
            log_file << current_time << "," << type << "," << exchange << "," 
                    << symbol << "," << description << "," << value << "\n";
        }
        
        printf("⚠️ COMPREHENSIVE RISK EVENT: %s - %s %s (%.2f)\n",
               description.c_str(), exchange.c_str(), symbol.c_str(), value);
    }
};

} // namespace risk
