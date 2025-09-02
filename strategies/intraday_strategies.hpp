#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include "../messages.hpp"

namespace hft::strategies {

// Time of day utilities for strategy scheduling
struct TimeOfDay {
    int hour;
    int minute;
    
    TimeOfDay(int h = 0, int m = 0) : hour(h), minute(m) {}
    
    bool is_opening_hour() const {
        return (hour == 9 && minute >= 30) || (hour == 10 && minute < 30);
    }
    
    bool is_regular_hours() const {
        return (hour >= 10 && hour < 15) || (hour == 15 && minute == 0);
    }
    
    bool is_between_1030_and_1500() const {
        return (hour == 10 && minute >= 30) || 
               (hour >= 11 && hour < 15) || 
               (hour == 15 && minute == 0);
    }
    
    static TimeOfDay get_market_time() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        return TimeOfDay(tm.tm_hour, tm.tm_min);
    }
};

// Enhanced market data structure with technical indicators
struct EnhancedMarketData {
    // Basic market data
    std::string symbol;
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    uint64_t bid_size = 0;
    uint64_t ask_size = 0;
    
    // VWAP and volume data
    double vwap = 0.0;
    double day_open = 0.0;
    double prev_close = 0.0;
    uint64_t cumulative_volume = 0;
    uint64_t average_volume_30d = 0;
    
    // Technical indicators
    double rsi_5 = 50.0;
    double rsi_14 = 50.0;
    double std_dev_20 = 0.0;
    double distance_from_vwap_percent = 0.0;
    
    // Derived properties
    double mid_price() const { return (bid + ask) / 2.0; }
    double spread_bps() const {
        double mid = mid_price();
        return mid > 0 ? ((ask - bid) / mid) * 10000.0 : 0.0;
    }
    
    uint64_t timestamp_ns = 0;
};

// Trading signal structure
struct Signal {
    bool is_valid = false;
    std::string strategy_type;
    std::string symbol;
    
    enum Direction { LONG, SHORT, FLAT } direction = FLAT;
    
    double entry_price = 0.0;
    double stop_price = 0.0;
    double target_price = 0.0;
    double confidence = 0.0;
    double expected_edge_bps = 0.0;
    
    // Risk parameters
    double stop_distance_points = 0.0;
    uint32_t hold_time_ms = 0;
    
    uint64_t timestamp_ns = 0;
    
    Signal() = default;
    Signal(const std::string& strategy, const std::string& sym) 
        : strategy_type(strategy), symbol(sym) {}
};

// VWAP Reversion Strategy Implementation
class VWAPReversionStrategy {
private:
    struct VWAPConfig {
        double min_distance_from_vwap = 0.01;  // 1% minimum distance
        double rsi_oversold_threshold = 30.0;
        double rsi_overbought_threshold = 70.0;
        double min_volume_ratio = 1.0;         // Compared to 30-day average
        double max_spread_bps = 20.0;          // Maximum spread to trade
        double stop_loss_percent = 0.005;      // 0.5% stop loss
        double target_profit_percent = 0.008;  // 0.8% target
    } config_;
    
public:
    Signal evaluate(const EnhancedMarketData& data) {
        Signal signal("VWAP_REVERSION", data.symbol);
        
        // Check if we're in the appropriate time window
        TimeOfDay current_time = TimeOfDay::get_market_time();
        if (!current_time.is_between_1030_and_1500()) {
            return signal; // Invalid signal outside time window
        }
        
        // Basic data validation
        if (data.vwap == 0.0 || data.last == 0.0 || data.spread_bps() > config_.max_spread_bps) {
            return signal;
        }
        
        // Calculate distance from VWAP
        double distance_from_vwap = (data.last - data.vwap) / data.vwap;
        
        // Volume confirmation
        bool sufficient_volume = data.cumulative_volume > (data.average_volume_30d * config_.min_volume_ratio);
        
        // Long signal: Price below VWAP + RSI oversold
        if (distance_from_vwap < -config_.min_distance_from_vwap && 
            data.rsi_5 < config_.rsi_oversold_threshold && 
            sufficient_volume) {
            
            signal.is_valid = true;
            signal.direction = Signal::LONG;
            signal.entry_price = data.ask;  // Market buy
            signal.stop_price = data.last - (data.last * config_.stop_loss_percent);
            signal.target_price = data.vwap;  // Target VWAP return
            signal.stop_distance_points = signal.entry_price - signal.stop_price;
            signal.confidence = calculate_confidence(data, distance_from_vwap);
            signal.expected_edge_bps = std::abs(distance_from_vwap) * 10000.0;
            signal.hold_time_ms = 300000; // 5 minutes expected hold
        }
        
        // Short signal: Price above VWAP + RSI overbought  
        else if (distance_from_vwap > config_.min_distance_from_vwap && 
                 data.rsi_5 > config_.rsi_overbought_threshold && 
                 sufficient_volume) {
            
            signal.is_valid = true;
            signal.direction = Signal::SHORT;
            signal.entry_price = data.bid;  // Market sell
            signal.stop_price = data.last + (data.last * config_.stop_loss_percent);
            signal.target_price = data.vwap;  // Target VWAP return
            signal.stop_distance_points = signal.stop_price - signal.entry_price;
            signal.confidence = calculate_confidence(data, distance_from_vwap);
            signal.expected_edge_bps = std::abs(distance_from_vwap) * 10000.0;
            signal.hold_time_ms = 300000; // 5 minutes expected hold
        }
        
        signal.timestamp_ns = get_timestamp_ns();
        return signal;
    }
    
private:
    double calculate_confidence(const EnhancedMarketData& data, double vwap_distance) {
        double confidence = 0.5; // Base confidence
        
        // Increase confidence for stronger signals
        if (std::abs(vwap_distance) > 0.015) confidence += 0.1; // >1.5% from VWAP
        if (data.cumulative_volume > data.average_volume_30d * 1.2) confidence += 0.1; // High volume
        if (data.rsi_14 < 40 || data.rsi_14 > 60) confidence += 0.1; // Longer-term confirmation
        
        TimeOfDay current_time = TimeOfDay::get_market_time();
        if (current_time.is_between_1030_and_1500()) confidence += 0.1; // Optimal time
        
        return std::min(confidence, 0.9);
    }
};

// Opening Drive Strategy Implementation  
class OpeningDriveStrategy {
private:
    struct OpeningConfig {
        double min_gap_percent = 0.005;        // 0.5% minimum gap
        double max_gap_percent = 0.03;         // 3% maximum gap (avoid gaps too large)
        double volume_threshold_ratio = 1.5;   // 1.5x average volume
        double stop_loss_percent = 0.008;      // 0.8% stop loss
        double target_profit_percent = 0.015;  // 1.5% target
        double max_spread_bps = 25.0;
    } config_;
    
public:
    Signal evaluate(const EnhancedMarketData& data) {
        Signal signal("OPENING_DRIVE", data.symbol);
        
        // Only trade in opening hour
        TimeOfDay current_time = TimeOfDay::get_market_time();
        if (!current_time.is_opening_hour()) {
            return signal;
        }
        
        // Basic validation
        if (data.day_open == 0.0 || data.prev_close == 0.0 || 
            data.spread_bps() > config_.max_spread_bps) {
            return signal;
        }
        
        // Calculate gap
        double gap_percent = (data.day_open - data.prev_close) / data.prev_close;
        
        // Volume confirmation
        bool high_volume = data.cumulative_volume > (data.average_volume_30d * config_.volume_threshold_ratio);
        
        // Gap up momentum (long signal)
        if (gap_percent > config_.min_gap_percent && 
            gap_percent < config_.max_gap_percent &&
            data.last > data.day_open && // Price continuing higher
            high_volume) {
            
            signal.is_valid = true;
            signal.direction = Signal::LONG;
            signal.entry_price = data.ask;
            signal.stop_price = std::min(data.day_open, data.last - (data.last * config_.stop_loss_percent));
            signal.target_price = data.last + (data.last * config_.target_profit_percent);
            signal.stop_distance_points = signal.entry_price - signal.stop_price;
            signal.confidence = calculate_opening_confidence(data, gap_percent);
            signal.expected_edge_bps = gap_percent * 10000.0;
            signal.hold_time_ms = 1800000; // 30 minutes expected hold
        }
        
        // Gap down momentum (short signal)
        else if (gap_percent < -config_.min_gap_percent && 
                 gap_percent > -config_.max_gap_percent &&
                 data.last < data.day_open && // Price continuing lower
                 high_volume) {
            
            signal.is_valid = true;
            signal.direction = Signal::SHORT;
            signal.entry_price = data.bid;
            signal.stop_price = std::max(data.day_open, data.last + (data.last * config_.stop_loss_percent));
            signal.target_price = data.last - (data.last * config_.target_profit_percent);
            signal.stop_distance_points = signal.stop_price - signal.entry_price;
            signal.confidence = calculate_opening_confidence(data, gap_percent);
            signal.expected_edge_bps = std::abs(gap_percent) * 10000.0;
            signal.hold_time_ms = 1800000; // 30 minutes expected hold
        }
        
        signal.timestamp_ns = get_timestamp_ns();
        return signal;
    }
    
private:
    double calculate_opening_confidence(const EnhancedMarketData& data, double gap_percent) {
        double confidence = 0.6; // Higher base for momentum strategy
        
        if (std::abs(gap_percent) > 0.01) confidence += 0.1; // >1% gap
        if (data.cumulative_volume > data.average_volume_30d * 2.0) confidence += 0.1; // Very high volume
        if (data.rsi_5 > 70 || data.rsi_5 < 30) confidence += 0.1; // RSI momentum confirmation
        
        return std::min(confidence, 0.9);
    }
};

// Pair Divergence Strategy (SPY vs QQQ)
class PairDivergenceStrategy {
private:
    struct PairData {
        EnhancedMarketData spy_data;
        EnhancedMarketData qqq_data;
        bool has_both = false;
    } pair_data_;
    
    struct PairConfig {
        double min_zscore = 2.0;              // Minimum Z-score for divergence
        double correlation_threshold = 0.8;    // Minimum correlation
        double stop_loss_percent = 0.006;     // 0.6% stop loss (hedged position)
        double target_profit_percent = 0.012; // 1.2% target  
        double max_spread_bps = 30.0;
    } config_;
    
    // Historical price ratio tracking (simplified)
    std::vector<double> ratio_history_;
    static constexpr size_t MAX_HISTORY = 100;
    
public:
    void update_pair_data(const EnhancedMarketData& data) {
        if (data.symbol == "SPY") {
            pair_data_.spy_data = data;
        } else if (data.symbol == "QQQ") {
            pair_data_.qqq_data = data;
        }
        
        pair_data_.has_both = (pair_data_.spy_data.timestamp_ns > 0 && 
                              pair_data_.qqq_data.timestamp_ns > 0);
        
        // Update ratio history
        if (pair_data_.has_both) {
            double ratio = pair_data_.spy_data.last / pair_data_.qqq_data.last;
            ratio_history_.push_back(ratio);
            if (ratio_history_.size() > MAX_HISTORY) {
                ratio_history_.erase(ratio_history_.begin());
            }
        }
    }
    
    Signal evaluate() {
        Signal signal("PAIR_DIVERGENCE", "SPY/QQQ");
        
        if (!pair_data_.has_both || ratio_history_.size() < 20) {
            return signal; // Need sufficient data
        }
        
        // Calculate current ratio and Z-score
        double current_ratio = pair_data_.spy_data.last / pair_data_.qqq_data.last;
        double mean_ratio = calculate_mean();
        double std_dev = calculate_std_dev(mean_ratio);
        
        if (std_dev == 0.0) return signal;
        
        double zscore = (current_ratio - mean_ratio) / std_dev;
        
        // Check spreads
        if (pair_data_.spy_data.spread_bps() > config_.max_spread_bps || 
            pair_data_.qqq_data.spread_bps() > config_.max_spread_bps) {
            return signal;
        }
        
        // SPY relatively expensive vs QQQ (short SPY, long QQQ)
        if (zscore > config_.min_zscore) {
            signal.is_valid = true;
            signal.direction = Signal::SHORT; // Short the expensive one (SPY)
            signal.entry_price = pair_data_.spy_data.bid; // Short SPY at bid
            signal.stop_price = pair_data_.spy_data.last + (pair_data_.spy_data.last * config_.stop_loss_percent);
            signal.target_price = pair_data_.spy_data.last - (pair_data_.spy_data.last * config_.target_profit_percent);
            signal.stop_distance_points = signal.stop_price - signal.entry_price;
            signal.confidence = calculate_pair_confidence(std::abs(zscore));
            signal.expected_edge_bps = std::abs(zscore) * 50.0; // Rough edge estimate
            signal.hold_time_ms = 900000; // 15 minutes expected hold
        }
        
        // SPY relatively cheap vs QQQ (long SPY, short QQQ)  
        else if (zscore < -config_.min_zscore) {
            signal.is_valid = true;
            signal.direction = Signal::LONG; // Long the cheap one (SPY)
            signal.entry_price = pair_data_.spy_data.ask; // Long SPY at ask
            signal.stop_price = pair_data_.spy_data.last - (pair_data_.spy_data.last * config_.stop_loss_percent);
            signal.target_price = pair_data_.spy_data.last + (pair_data_.spy_data.last * config_.target_profit_percent);
            signal.stop_distance_points = signal.entry_price - signal.stop_price;
            signal.confidence = calculate_pair_confidence(std::abs(zscore));
            signal.expected_edge_bps = std::abs(zscore) * 50.0;
            signal.hold_time_ms = 900000; // 15 minutes expected hold
        }
        
        signal.timestamp_ns = get_timestamp_ns();
        return signal;
    }
    
private:
    double calculate_mean() const {
        double sum = 0.0;
        for (double ratio : ratio_history_) {
            sum += ratio;
        }
        return sum / ratio_history_.size();
    }
    
    double calculate_std_dev(double mean) const {
        double sum_sq_diff = 0.0;
        for (double ratio : ratio_history_) {
            double diff = ratio - mean;
            sum_sq_diff += diff * diff;
        }
        return std::sqrt(sum_sq_diff / ratio_history_.size());
    }
    
    double calculate_pair_confidence(double abs_zscore) const {
        double confidence = 0.5;
        
        if (abs_zscore > 2.5) confidence += 0.1; // Strong divergence
        if (abs_zscore > 3.0) confidence += 0.1; // Very strong divergence
        if (ratio_history_.size() >= 50) confidence += 0.1; // Sufficient data
        
        return std::min(confidence, 0.8); // Cap at 80% for pairs
    }
};

// Strategy Scheduler and Coordinator
class StrategyScheduler {
public:
    bool should_run_vwap_reversion() const {
        TimeOfDay current = TimeOfDay::get_market_time();
        return current.is_between_1030_and_1500();
    }
    
    bool should_run_opening_drive() const {
        TimeOfDay current = TimeOfDay::get_market_time();
        return current.is_opening_hour();
    }
    
    bool should_run_pair_divergence() const {
        TimeOfDay current = TimeOfDay::get_market_time();
        return current.is_regular_hours();
    }
};

// Main Strategy Engine
class IntradayStrategyEngine {
private:
    OpeningDriveStrategy opening_drive_;
    VWAPReversionStrategy vwap_reversion_;
    PairDivergenceStrategy pair_divergence_;
    StrategyScheduler scheduler_;
    
public:
    Signal process_market_data(const EnhancedMarketData& data) {
        // Update pair strategy with new data
        pair_divergence_.update_pair_data(data);
        
        TimeOfDay current_time = TimeOfDay::get_market_time();
        
        // Route to appropriate strategy based on time and conditions
        if (current_time.is_opening_hour() && scheduler_.should_run_opening_drive()) {
            auto signal = opening_drive_.evaluate(data);
            if (signal.is_valid) return signal;
        }
        
        if (current_time.is_regular_hours() && scheduler_.should_run_vwap_reversion()) {
            auto signal = vwap_reversion_.evaluate(data);
            if (signal.is_valid) return signal;
        }
        
        // Pairs can run anytime during regular hours
        if (scheduler_.should_run_pair_divergence()) {
            auto signal = pair_divergence_.evaluate();
            if (signal.is_valid) return signal;
        }
        
        // Return invalid signal if no strategy triggered
        return Signal();
    }
    
    // Method to process multiple symbols simultaneously
    std::vector<Signal> process_multiple_symbols(const std::vector<EnhancedMarketData>& market_data_list) {
        std::vector<Signal> signals;
        
        for (const auto& data : market_data_list) {
            auto signal = process_market_data(data);
            if (signal.is_valid) {
                signals.push_back(signal);
            }
        }
        
        return signals;
    }
};

} // namespace hft::strategies
