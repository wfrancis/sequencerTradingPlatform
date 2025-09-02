#pragma once

#include <atomic>
#include <chrono>
#include <string>
#include <mutex>
#include <fstream>
#include "../position_tracker.hpp"
#include "intraday_strategies.hpp"

namespace hft::strategies {

// Intraday-specific risk limits
struct IntradayRiskLimits {
    // Capital allocation
    double capital = 5000.0;
    double max_position_value = 1000.0;    // 20% of capital
    double max_loss_per_trade = 40.0;      // 0.8% of capital
    double daily_loss_limit = 100.0;       // 2% of capital
    double daily_profit_target = 60.0;     // 1.2% of capital
    
    // Position limits
    uint32_t max_trades_per_day = 4;
    uint32_t max_consecutive_losses = 2;
    double max_single_position_percent = 20.0; // 20% of capital per position
    
    // Time-based limits
    uint32_t min_seconds_between_trades = 300; // 5 minutes minimum
    uint32_t session_timeout_hours = 6;        // Maximum session length
    
    // Strategy-specific risk amounts
    double opening_drive_risk = 40.0;
    double vwap_reversion_risk = 30.0;
    double pair_divergence_risk = 50.0;
};

// Position sizing calculation result
struct PositionSize {
    double shares = 0.0;
    double max_value = 0.0;
    double risk_amount = 0.0;
    bool is_valid = false;
    std::string reason;
};

// Session state tracking
struct SessionRisk {
    double daily_pnl = 0.0;
    uint32_t trades_today = 0;
    uint32_t consecutive_losses = 0;
    uint64_t last_trade_time = 0;
    uint64_t session_start_time = 0;
    
    // Circuit breaker state
    bool trading_halted = false;
    std::string halt_reason;
    uint64_t halt_time = 0;
    
    // Performance tracking
    uint32_t winning_trades = 0;
    uint32_t losing_trades = 0;
    double largest_win = 0.0;
    double largest_loss = 0.0;
    double total_fees = 0.0;
    
    double get_win_rate() const {
        uint32_t total = winning_trades + losing_trades;
        return total > 0 ? static_cast<double>(winning_trades) / total : 0.0;
    }
    
    double get_avg_win() const {
        return winning_trades > 0 ? daily_pnl / winning_trades : 0.0;
    }
};

// Risk event types for intraday trading
enum class IntradayRiskEvent {
    TRADE_EXECUTED,
    TRADE_REJECTED_DAILY_LIMIT,
    TRADE_REJECTED_CONSECUTIVE_LOSSES,
    TRADE_REJECTED_TRADE_FREQUENCY,
    TRADE_REJECTED_POSITION_SIZE,
    CIRCUIT_BREAKER_TRIGGERED,
    PROFIT_TARGET_REACHED,
    SESSION_TIMEOUT,
    EMERGENCY_HALT
};

class IntradayRiskManager {
private:
    IntradayRiskLimits limits_;
    SessionRisk session_;
    PositionTracker* position_tracker_;
    
    mutable std::mutex mutex_;
    std::atomic<bool> emergency_halt_{false};
    
    // Logging
    std::ofstream trade_log_;
    std::ofstream risk_log_;
    
    void log_risk_event(IntradayRiskEvent event, const std::string& description, 
                       double value = 0.0) {
        auto timestamp = get_timestamp_ns();
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (risk_log_.is_open()) {
            risk_log_ << timestamp << ","
                     << static_cast<int>(event) << ","
                     << description << ","
                     << value << ","
                     << session_.daily_pnl << ","
                     << session_.trades_today << "\n";
            risk_log_.flush();
        }
    }
    
    void log_trade(const Signal& signal, const PositionSize& size, bool approved) {
        auto timestamp = get_timestamp_ns();
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (trade_log_.is_open()) {
            trade_log_ << timestamp << ","
                      << signal.strategy_type << ","
                      << signal.symbol << ","
                      << (signal.direction == Signal::LONG ? "LONG" : "SHORT") << ","
                      << signal.entry_price << ","
                      << signal.stop_price << ","
                      << signal.target_price << ","
                      << size.shares << ","
                      << size.risk_amount << ","
                      << signal.confidence << ","
                      << (approved ? "APPROVED" : "REJECTED") << ","
                      << session_.daily_pnl << "\n";
            trade_log_.flush();
        }
    }

public:
    explicit IntradayRiskManager(PositionTracker* position_tracker = nullptr) 
        : position_tracker_(position_tracker) {
        
        session_.session_start_time = get_timestamp_ns();
        
        // Initialize log files
        trade_log_.open("intraday_trades.csv", std::ios::app);
        if (trade_log_.is_open()) {
            trade_log_ << "timestamp,strategy,symbol,direction,entry_price,stop_price,"
                      << "target_price,shares,risk_amount,confidence,status,daily_pnl\n";
        }
        
        risk_log_.open("intraday_risk.csv", std::ios::app);
        if (risk_log_.is_open()) {
            risk_log_ << "timestamp,event_type,description,value,daily_pnl,trades_today\n";
        }
        
        printf("🛡️ Intraday Risk Manager initialized\n");
        printf("   Capital: $%.0f\n", limits_.capital);
        printf("   Max Loss Per Trade: $%.0f\n", limits_.max_loss_per_trade);
        printf("   Daily Loss Limit: $%.0f\n", limits_.daily_loss_limit);
        printf("   Daily Profit Target: $%.0f\n", limits_.daily_profit_target);
        printf("   Max Trades Per Day: %u\n", limits_.max_trades_per_day);
    }
    
    ~IntradayRiskManager() {
        if (trade_log_.is_open()) trade_log_.close();
        if (risk_log_.is_open()) risk_log_.close();
    }
    
    // Main risk check - called before every trade
    bool can_trade(const Signal& signal = Signal()) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Emergency halt check
        if (emergency_halt_) {
            return false;
        }
        
        // Trading halted check
        if (session_.trading_halted) {
            return false;
        }
        
        // Daily trade limit
        if (session_.trades_today >= limits_.max_trades_per_day) {
            halt_trading("Daily trade limit reached (" + 
                        std::to_string(limits_.max_trades_per_day) + " trades)");
            log_risk_event(IntradayRiskEvent::TRADE_REJECTED_DAILY_LIMIT, 
                          "Max trades per day reached");
            return false;
        }
        
        // Daily loss limit
        if (session_.daily_pnl <= -limits_.daily_loss_limit) {
            halt_trading("Daily loss limit reached: $" + 
                        std::to_string(session_.daily_pnl));
            log_risk_event(IntradayRiskEvent::TRADE_REJECTED_DAILY_LIMIT, 
                          "Daily loss limit exceeded", session_.daily_pnl);
            return false;
        }
        
        // Daily profit target (optional stop)
        if (session_.daily_pnl >= limits_.daily_profit_target) {
            halt_trading("Daily profit target reached: $" + 
                        std::to_string(session_.daily_pnl));
            log_risk_event(IntradayRiskEvent::PROFIT_TARGET_REACHED, 
                          "Daily profit target achieved", session_.daily_pnl);
            return false;
        }
        
        // Consecutive losses limit
        if (session_.consecutive_losses >= limits_.max_consecutive_losses) {
            halt_trading("Maximum consecutive losses reached (" + 
                        std::to_string(limits_.max_consecutive_losses) + ")");
            log_risk_event(IntradayRiskEvent::TRADE_REJECTED_CONSECUTIVE_LOSSES, 
                          "Too many consecutive losses");
            return false;
        }
        
        // Time between trades
        uint64_t current_time = get_timestamp_ns();
        if (session_.last_trade_time > 0) {
            uint64_t time_since_last = (current_time - session_.last_trade_time) / 1000000000ULL;
            if (time_since_last < limits_.min_seconds_between_trades) {
                log_risk_event(IntradayRiskEvent::TRADE_REJECTED_TRADE_FREQUENCY, 
                              "Trade frequency limit exceeded");
                return false;
            }
        }
        
        // Session timeout check
        uint64_t session_duration = (current_time - session_.session_start_time) / 3600000000000ULL; // hours
        if (session_duration >= limits_.session_timeout_hours) {
            halt_trading("Session timeout reached (" + 
                        std::to_string(limits_.session_timeout_hours) + " hours)");
            log_risk_event(IntradayRiskEvent::SESSION_TIMEOUT, "Session duration exceeded");
            return false;
        }
        
        return true;
    }
    
    // Calculate position size based on risk parameters
    PositionSize calculate_position_size(const Signal& signal) {
        PositionSize size;
        
        if (!signal.is_valid) {
            size.reason = "Invalid signal";
            return size;
        }
        
        // Get strategy-specific risk amount
        double risk_amount = get_strategy_risk_amount(signal.strategy_type);
        
        // Calculate position size based on stop distance
        if (signal.stop_distance_points <= 0) {
            size.reason = "Invalid stop distance";
            return size;
        }
        
        // Basic position sizing: Risk Amount / Stop Distance
        size.shares = risk_amount / signal.stop_distance_points;
        size.risk_amount = risk_amount;
        
        // Calculate position value
        size.max_value = size.shares * signal.entry_price;
        
        // Check position size limits
        if (size.max_value > limits_.max_position_value) {
            // Scale down to max position value
            size.shares = limits_.max_position_value / signal.entry_price;
            size.max_value = limits_.max_position_value;
            size.risk_amount = size.shares * signal.stop_distance_points;
        }
        
        // Final validation
        if (size.shares <= 0) {
            size.reason = "Calculated position size too small";
            return size;
        }
        
        if (size.max_value > limits_.capital * (limits_.max_single_position_percent / 100.0)) {
            size.reason = "Position exceeds maximum position percentage";
            return size;
        }
        
        size.is_valid = true;
        return size;
    }
    
    // Update risk manager after trade execution
    void update_trade_result(const Signal& signal, const PositionSize& size, 
                           double actual_pnl, double fees = 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        session_.trades_today++;
        session_.daily_pnl += actual_pnl - fees;
        session_.total_fees += fees;
        session_.last_trade_time = get_timestamp_ns();
        
        // Track win/loss streaks
        if (actual_pnl > 0) {
            session_.winning_trades++;
            session_.consecutive_losses = 0; // Reset loss streak
            session_.largest_win = std::max(session_.largest_win, actual_pnl);
        } else if (actual_pnl < 0) {
            session_.losing_trades++;
            session_.consecutive_losses++;
            session_.largest_loss = std::min(session_.largest_loss, actual_pnl);
        }
        
        log_trade(signal, size, true);
        log_risk_event(IntradayRiskEvent::TRADE_EXECUTED, 
                      "Trade completed: " + signal.symbol, actual_pnl);
        
        // Check if we should halt trading after this trade
        if (session_.daily_pnl <= -limits_.daily_loss_limit) {
            halt_trading("Daily loss limit reached after trade");
        } else if (session_.daily_pnl >= limits_.daily_profit_target) {
            halt_trading("Daily profit target reached after trade");
        }
    }
    
    // Manual controls
    void halt_trading(const std::string& reason) {
        session_.trading_halted = true;
        session_.halt_reason = reason;
        session_.halt_time = get_timestamp_ns();
        
        printf("🛑 TRADING HALTED: %s\n", reason.c_str());
        printf("   Session P&L: $%.2f\n", session_.daily_pnl);
        printf("   Trades Today: %u\n", session_.trades_today);
    }
    
    void resume_trading() {
        std::lock_guard<std::mutex> lock(mutex_);
        session_.trading_halted = false;
        session_.halt_reason.clear();
        printf("✅ Trading resumed\n");
    }
    
    void emergency_halt(const std::string& reason) {
        emergency_halt_ = true;
        halt_trading("EMERGENCY: " + reason);
        log_risk_event(IntradayRiskEvent::EMERGENCY_HALT, reason);
        printf("🚨 EMERGENCY HALT: %s\n", reason.c_str());
    }
    
    void reset_emergency_halt() {
        emergency_halt_ = false;
        resume_trading();
    }
    
    // Getters
    bool is_trading_halted() const {
        return session_.trading_halted || emergency_halt_;
    }
    
    const SessionRisk& get_session_stats() const {
        return session_;
    }
    
    double get_daily_pnl() const {
        return session_.daily_pnl;
    }
    
    uint32_t get_trades_today() const {
        return session_.trades_today;
    }
    
    double get_remaining_daily_risk() const {
        return std::max(0.0, limits_.daily_loss_limit + session_.daily_pnl);
    }
    
    // Configuration updates
    void update_limits(const IntradayRiskLimits& new_limits) {
        std::lock_guard<std::mutex> lock(mutex_);
        limits_ = new_limits;
        printf("🔧 Intraday risk limits updated\n");
    }
    
    const IntradayRiskLimits& get_limits() const {
        return limits_;
    }
    
    // Status reporting
    void print_risk_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n🛡️ === INTRADAY RISK STATUS ===\n");
        printf("🚨 Trading Halted: %s\n", session_.trading_halted ? "YES" : "NO");
        if (session_.trading_halted) {
            printf("   Reason: %s\n", session_.halt_reason.c_str());
        }
        
        printf("💰 Daily P&L: $%.2f (Target: $%.0f, Limit: -$%.0f)\n", 
               session_.daily_pnl, limits_.daily_profit_target, limits_.daily_loss_limit);
        
        printf("📊 Trades: %u/%u (%.1f%% win rate)\n", 
               session_.trades_today, limits_.max_trades_per_day, 
               session_.get_win_rate() * 100.0);
        
        printf("🔥 Consecutive Losses: %u/%u\n", 
               session_.consecutive_losses, limits_.max_consecutive_losses);
        
        printf("⏱️  Session Duration: %.1f hours\n", 
               (get_timestamp_ns() - session_.session_start_time) / 3600000000000.0);
        
        printf("💸 Total Fees: $%.2f\n", session_.total_fees);
        printf("📈 Largest Win: $%.2f\n", session_.largest_win);
        printf("📉 Largest Loss: $%.2f\n", session_.largest_loss);
        printf("===============================\n\n");
    }
    
private:
    double get_strategy_risk_amount(const std::string& strategy_type) {
        if (strategy_type == "OPENING_DRIVE") {
            return limits_.opening_drive_risk;
        } else if (strategy_type == "VWAP_REVERSION") {
            return limits_.vwap_reversion_risk;
        } else if (strategy_type == "PAIR_DIVERGENCE") {
            return limits_.pair_divergence_risk;
        }
        return limits_.max_loss_per_trade; // Default
    }
};

} // namespace hft::strategies
