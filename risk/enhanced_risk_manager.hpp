#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <string>
#include <mutex>
#include <fstream>
#include <vector>
#include "../position_tracker.hpp"
#include "../messages.hpp"

enum class RiskViolationType {
    POSITION_LIMIT_EXCEEDED,
    DRAWDOWN_LIMIT_EXCEEDED,
    DAILY_LOSS_LIMIT_EXCEEDED,
    ORDER_SIZE_LIMIT_EXCEEDED,
    INSUFFICIENT_SPREAD,
    EXCHANGE_DISCONNECTED,
    EMERGENCY_STOP_TRIGGERED,
    RAPID_FIRE_PREVENTION,
    CORRELATION_RISK
};

struct RiskLimits {
    // Position limits per symbol
    double max_btc_position = 0.05;        // Max 0.05 BTC position per exchange
    double max_eth_position = 0.5;         // Max 0.5 ETH position per exchange
    double max_usd_exposure = 10000.0;     // Max $10k USD exposure per exchange
    
    // P&L and drawdown limits
    double max_daily_loss = 2000.0;        // Max $2k daily loss
    double max_drawdown = 5000.0;          // Max $5k total drawdown
    double min_account_equity = 45000.0;   // Stop trading below $45k
    
    // Order size limits
    double max_order_size_btc = 0.01;      // Max 0.01 BTC per order
    double max_order_size_eth = 0.1;       // Max 0.1 ETH per order
    double max_order_notional = 1000.0;    // Max $1k per order
    
    // Strategy limits
    double min_spread_bps = 25.0;          // Minimum 25 bps spread
    double max_spread_bps = 1000.0;        // Reject if spread too wide (likely error)
    
    // Timing controls
    uint32_t min_ms_between_orders = 100;  // Min 100ms between orders
    uint32_t max_orders_per_minute = 20;   // Max 20 orders per minute
    
    // Circuit breaker settings
    bool circuit_breaker_enabled = true;
    double circuit_breaker_loss_threshold = 1000.0; // Trip at $1k loss
    uint32_t circuit_breaker_cooldown_seconds = 300; // 5 minute cooldown
};

struct RiskEvent {
    uint64_t timestamp_ns;
    RiskViolationType type;
    std::string exchange;
    std::string symbol;
    std::string description;
    double value;
    bool trading_halted;
};

class EnhancedRiskManager {
private:
    RiskLimits limits_;
    PositionTracker* position_tracker_;
    
    // Circuit breaker state
    mutable std::atomic<bool> circuit_breaker_active_{false};
    mutable std::atomic<uint64_t> circuit_breaker_trigger_time_{0};
    std::atomic<bool> emergency_stop_active_{false};
    
    // Rate limiting
    std::unordered_map<std::string, uint64_t> last_order_time_;  // [exchange] -> timestamp
    std::unordered_map<std::string, std::vector<uint64_t>> recent_orders_; // [exchange] -> timestamps
    
    // Risk monitoring
    std::vector<RiskEvent> risk_events_;
    std::atomic<uint32_t> risk_violations_today_{0};
    
    mutable std::mutex mutex_;
    std::atomic<bool> logging_enabled_{true};
    
    void log_risk_event(const RiskEvent& event) {
        if (!logging_enabled_) return;
        
        std::ofstream log_file("risk_events.csv", std::ios::app);
        if (log_file.is_open()) {
            log_file << event.timestamp_ns << ","
                    << static_cast<int>(event.type) << ","
                    << event.exchange << ","
                    << event.symbol << ","
                    << event.description << ","
                    << event.value << ","
                    << (event.trading_halted ? "1" : "0") << "\n";
        }
    }
    
    void trigger_circuit_breaker(const std::string& reason) {
        circuit_breaker_active_ = true;
        circuit_breaker_trigger_time_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        printf("🚨 CIRCUIT BREAKER TRIGGERED: %s\n", reason.c_str());
        printf("⏰ Trading halted for %d seconds\n", limits_.circuit_breaker_cooldown_seconds);
        
        RiskEvent event{
            circuit_breaker_trigger_time_,
            RiskViolationType::EMERGENCY_STOP_TRIGGERED,
            "SYSTEM",
            "ALL",
            reason,
            0.0,
            true
        };
        
        std::lock_guard<std::mutex> lock(mutex_);
        risk_events_.push_back(event);
        log_risk_event(event);
    }
    
    bool is_circuit_breaker_cooling_down() const {
        if (!circuit_breaker_active_.load(std::memory_order_acquire)) {
            return false;
        }
        
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        auto elapsed_ns = current_time - circuit_breaker_trigger_time_.load(std::memory_order_acquire);
        auto elapsed_seconds = elapsed_ns / 1000000000ULL;
        
        if (elapsed_seconds >= limits_.circuit_breaker_cooldown_seconds) {
            // Properly reset using atomic store
            circuit_breaker_active_.store(false, std::memory_order_release);
            printf("✅ Circuit breaker reset - trading resumed\n");
            return false;
        }
        
        return true;
    }
    
    bool check_rate_limits(const std::string& exchange) {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // Check minimum time between orders
        if (last_order_time_.count(exchange)) {
            auto time_since_last = (current_time - last_order_time_[exchange]) / 1000000; // Convert to ms
            if (time_since_last < limits_.min_ms_between_orders) {
                return false; // Too soon since last order
            }
        }
        
        // Check orders per minute limit
        auto& recent = recent_orders_[exchange];
        auto minute_ago = current_time - 60000000000ULL; // 60 seconds in nanoseconds
        
        // Remove old entries
        recent.erase(std::remove_if(recent.begin(), recent.end(),
                    [minute_ago](uint64_t timestamp) { return timestamp < minute_ago; }),
                    recent.end());
        
        if (recent.size() >= limits_.max_orders_per_minute) {
            return false; // Too many orders in the last minute
        }
        
        return true;
    }

public:
    EnhancedRiskManager(PositionTracker* position_tracker) 
        : position_tracker_(position_tracker) {
        
        // Create risk event log header
        std::ofstream log_file("risk_events.csv");
        if (log_file.is_open()) {
            log_file << "timestamp_ns,risk_type,exchange,symbol,description,value,trading_halted\n";
        }
        
        printf("🛡️ Enhanced Risk Manager initialized\n");
        printf("   Max Daily Loss: $%.0f\n", limits_.max_daily_loss);
        printf("   Max Drawdown: $%.0f\n", limits_.max_drawdown);
        printf("   Circuit Breaker: %s\n", limits_.circuit_breaker_enabled ? "ENABLED" : "DISABLED");
    }
    
    // Core risk check - called before every trade
    bool can_execute_trade(const std::string& exchange, const std::string& symbol,
                          double quantity, double price, double spread_bps = 0.0) {
        
        // 1. Emergency stop check
        if (emergency_stop_active_) {
            printf("🛑 EMERGENCY STOP ACTIVE - All trading halted\n");
            return false;
        }
        
        // 2. Circuit breaker check
        if (limits_.circuit_breaker_enabled && is_circuit_breaker_cooling_down()) {
            printf("🚨 Circuit breaker active - Trading halted\n");
            return false;
        }
        
        // 3. Rate limiting check
        if (!check_rate_limits(exchange)) {
            create_risk_event(RiskViolationType::RAPID_FIRE_PREVENTION, exchange, symbol,
                            "Rate limit exceeded", 0.0);
            return false;
        }
        
        // 4. Spread check
        if (spread_bps < limits_.min_spread_bps) {
            create_risk_event(RiskViolationType::INSUFFICIENT_SPREAD, exchange, symbol,
                            "Spread below minimum threshold", spread_bps);
            return false;
        }
        
        if (spread_bps > limits_.max_spread_bps) {
            create_risk_event(RiskViolationType::INSUFFICIENT_SPREAD, exchange, symbol,
                            "Spread suspiciously high - possible error", spread_bps);
            return false;
        }
        
        // 5. Order size check
        double notional = std::abs(quantity * price);
        if (notional > limits_.max_order_notional) {
            create_risk_event(RiskViolationType::ORDER_SIZE_LIMIT_EXCEEDED, exchange, symbol,
                            "Order notional exceeds limit", notional);
            return false;
        }
        
        if (symbol.find("BTC") != std::string::npos && std::abs(quantity) > limits_.max_order_size_btc) {
            create_risk_event(RiskViolationType::ORDER_SIZE_LIMIT_EXCEEDED, exchange, symbol,
                            "BTC order size exceeds limit", std::abs(quantity));
            return false;
        }
        
        if (symbol.find("ETH") != std::string::npos && std::abs(quantity) > limits_.max_order_size_eth) {
            create_risk_event(RiskViolationType::ORDER_SIZE_LIMIT_EXCEEDED, exchange, symbol,
                            "ETH order size exceeds limit", std::abs(quantity));
            return false;
        }
        
        // 6. Position limit check
        auto current_position = position_tracker_->get_position(exchange, symbol);
        double new_position = current_position.quantity + quantity;
        
        if (symbol.find("BTC") != std::string::npos && std::abs(new_position) > limits_.max_btc_position) {
            create_risk_event(RiskViolationType::POSITION_LIMIT_EXCEEDED, exchange, symbol,
                            "BTC position limit would be exceeded", std::abs(new_position));
            return false;
        }
        
        if (symbol.find("ETH") != std::string::npos && std::abs(new_position) > limits_.max_eth_position) {
            create_risk_event(RiskViolationType::POSITION_LIMIT_EXCEEDED, exchange, symbol,
                            "ETH position limit would be exceeded", std::abs(new_position));
            return false;
        }
        
        // 7. Balance check
        if (!position_tracker_->can_trade(exchange, symbol, quantity, price)) {
            create_risk_event(RiskViolationType::POSITION_LIMIT_EXCEEDED, exchange, symbol,
                            "Insufficient balance for trade", notional);
            return false;
        }
        
        // 8. Equity and drawdown check
        double current_equity = position_tracker_->get_total_equity();
        if (current_equity < limits_.min_account_equity) {
            trigger_circuit_breaker("Account equity below minimum threshold");
            return false;
        }
        
        double daily_pnl = position_tracker_->get_daily_pnl();
        if (daily_pnl < -limits_.max_daily_loss) {
            trigger_circuit_breaker("Daily loss limit exceeded");
            return false;
        }
        
        double max_drawdown = position_tracker_->get_max_drawdown();
        if (max_drawdown > limits_.max_drawdown) {
            trigger_circuit_breaker("Maximum drawdown limit exceeded");
            return false;
        }
        
        // All checks passed - update rate limiting
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        std::lock_guard<std::mutex> lock(mutex_);
        last_order_time_[exchange] = current_time;
        recent_orders_[exchange].push_back(current_time);
        
        return true;
    }
    
    void create_risk_event(RiskViolationType type, const std::string& exchange,
                          const std::string& symbol, const std::string& description,
                          double value, bool halt_trading = false) {
        
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        RiskEvent event{static_cast<uint64_t>(current_time), type, exchange, symbol, description, value, halt_trading};
        
        std::lock_guard<std::mutex> lock(mutex_);
        risk_events_.push_back(event);
        risk_violations_today_++;
        
        log_risk_event(event);
        
        printf("⚠️ RISK EVENT: %s - %s %s (%.2f)\n",
               description.c_str(), exchange.c_str(), symbol.c_str(), value);
    }
    
    void emergency_stop(const std::string& reason) {
        emergency_stop_active_ = true;
        printf("🛑 EMERGENCY STOP ACTIVATED: %s\n", reason.c_str());
        printf("   All trading immediately halted!\n");
        printf("   Manual intervention required to resume.\n");
        
        create_risk_event(RiskViolationType::EMERGENCY_STOP_TRIGGERED, "SYSTEM", "ALL",
                         "Emergency stop: " + reason, 0.0, true);
    }
    
    void reset_emergency_stop() {
        emergency_stop_active_ = false;
        printf("✅ Emergency stop reset - trading can resume\n");
    }
    
    RiskLimits get_limits() const {
        return limits_;
    }
    
    void update_limits(const RiskLimits& new_limits) {
        std::lock_guard<std::mutex> lock(mutex_);
        limits_ = new_limits;
        printf("🔧 Risk limits updated\n");
    }
    
    bool is_trading_halted() const {
        return emergency_stop_active_ || 
               (limits_.circuit_breaker_enabled && is_circuit_breaker_cooling_down());
    }
    
    void print_risk_status() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n🛡️ === RISK MANAGER STATUS ===\n");
        printf("🚨 Emergency Stop: %s\n", emergency_stop_active_ ? "ACTIVE" : "OFF");
        printf("⚡ Circuit Breaker: %s\n", 
               is_circuit_breaker_cooling_down() ? "ACTIVE" : "OFF");
        printf("⚠️ Risk Violations Today: %d\n", risk_violations_today_.load());
        
        double current_equity = position_tracker_->get_total_equity();
        double daily_pnl = position_tracker_->get_daily_pnl();
        double max_drawdown = position_tracker_->get_max_drawdown();
        
        printf("\n📊 Risk Metrics:\n");
        printf("  Current Equity: $%.2f (Min: $%.0f)\n", current_equity, limits_.min_account_equity);
        printf("  Daily P&L: $%.2f (Limit: -$%.0f)\n", daily_pnl, limits_.max_daily_loss);
        printf("  Max Drawdown: $%.2f (Limit: $%.0f)\n", max_drawdown, limits_.max_drawdown);
        
        printf("\n📋 Position Limits:\n");
        printf("  Max BTC Position: %.3f per exchange\n", limits_.max_btc_position);
        printf("  Max ETH Position: %.3f per exchange\n", limits_.max_eth_position);
        printf("  Max Order Size: $%.0f notional\n", limits_.max_order_notional);
        printf("  Min Spread: %.1f bps\n", limits_.min_spread_bps);
        printf("============================\n\n");
    }
};
