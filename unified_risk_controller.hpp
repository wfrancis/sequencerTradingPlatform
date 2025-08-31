#pragma once

#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <mutex>
#include <vector>
#include <functional>
#include "comprehensive_risk_manager.hpp"
#include "system_health_monitor.hpp"
#include "enhanced_risk_manager.hpp"
#include "position_tracker.hpp"
#include "messages.hpp"

namespace risk {

/**
 * Risk Control Status
 */
enum class RiskControlStatus {
    NORMAL,           // All systems normal
    WARNING,          // Warning conditions detected
    CRITICAL,         // Critical conditions, trading restricted
    EMERGENCY_STOP    // Emergency stop, all trading halted
};

/**
 * Risk Control Event
 */
struct RiskControlEvent {
    uint64_t timestamp_ns;
    RiskControlStatus status;
    std::string component;
    std::string description;
    std::string action_taken;
    double severity_score;
};

/**
 * Trading Authorization Response
 */
struct TradingAuthorizationResponse {
    bool authorized = false;
    std::string rejection_reason;
    RiskControlStatus system_status = RiskControlStatus::NORMAL;
    std::vector<std::string> warnings;
    double confidence_score = 1.0;  // 0.0 to 1.0, where 1.0 is full confidence
};

/**
 * Unified Risk Controller
 * 
 * Central coordination point for all risk management components.
 * Provides a single interface for trading systems to check risk controls
 * and get comprehensive risk assessment before executing trades.
 */
class UnifiedRiskController {
private:
    // Core risk management components
    std::unique_ptr<ComprehensiveRiskManager> comprehensive_risk_manager_;
    std::unique_ptr<SystemHealthMonitor> system_health_monitor_;
    PositionTracker* position_tracker_;
    
    // Risk control state
    std::atomic<RiskControlStatus> current_status_{RiskControlStatus::NORMAL};
    std::atomic<bool> trading_enabled_{true};
    std::atomic<bool> emergency_override_active_{false};
    
    // Event logging
    std::vector<RiskControlEvent> recent_events_;
    mutable std::mutex events_mutex_;
    static constexpr size_t MAX_RECENT_EVENTS = 1000;
    
    // Monitoring thread
    std::atomic<bool> monitoring_active_{false};
    std::thread monitoring_thread_;
    std::atomic<uint32_t> monitoring_interval_ms_{500}; // 500ms monitoring interval
    
    // Risk scoring weights
    struct RiskWeights {
        double market_risk_weight = 0.3;
        double liquidity_risk_weight = 0.2;
        double system_risk_weight = 0.25;
        double position_risk_weight = 0.15;
        double operational_risk_weight = 0.1;
    } risk_weights_;
    
    // Callback functions for risk events
    std::vector<std::function<void(const RiskControlEvent&)>> event_callbacks_;
    std::mutex callbacks_mutex_;

public:
    UnifiedRiskController(PositionTracker* position_tracker) 
        : position_tracker_(position_tracker),
          comprehensive_risk_manager_(std::make_unique<ComprehensiveRiskManager>(position_tracker)),
          system_health_monitor_(std::make_unique<SystemHealthMonitor>()) {
        
        printf("🎛️ Unified Risk Controller initialized\n");
        printf("   Monitoring interval: %dms\n", monitoring_interval_ms_.load());
        printf("   Trading status: %s\n", trading_enabled_ ? "ENABLED" : "DISABLED");
        
        // Start monitoring threads
        start_monitoring();
    }
    
    ~UnifiedRiskController() {
        stop_monitoring();
    }
    
    /**
     * Primary trading authorization check
     * 
     * This is the main entry point for all trading decisions.
     * Coordinates checks across all risk management components.
     */
    TradingAuthorizationResponse authorize_trade(
        const std::string& exchange,
        const std::string& symbol,
        double quantity,
        double price,
        double current_spread_bps = 0.0) {
        
        TradingAuthorizationResponse response;
        response.system_status = current_status_.load();
        
        // 1. Check if trading is globally enabled
        if (!trading_enabled_) {
            response.authorized = false;
            response.rejection_reason = "Trading globally disabled";
            response.confidence_score = 0.0;
            return response;
        }
        
        // 2. Check emergency override
        if (emergency_override_active_) {
            response.authorized = false;
            response.rejection_reason = "Emergency override active - manual intervention required";
            response.confidence_score = 0.0;
            return response;
        }
        
        // 3. Check system status
        if (current_status_ == RiskControlStatus::EMERGENCY_STOP) {
            response.authorized = false;
            response.rejection_reason = "Emergency stop active";
            response.confidence_score = 0.0;
            return response;
        }
        
        if (current_status_ == RiskControlStatus::CRITICAL) {
            response.authorized = false;
            response.rejection_reason = "Critical risk conditions detected";
            response.confidence_score = 0.0;
            return response;
        }
        
        // 4. Run comprehensive risk checks
        bool comprehensive_check = comprehensive_risk_manager_->can_execute_trade(
            exchange, symbol, quantity, price, current_spread_bps);
        
        if (!comprehensive_check) {
            response.authorized = false;
            response.rejection_reason = "Comprehensive risk manager rejected trade";
            response.confidence_score = 0.2;
            return response;
        }
        
        // 5. Check system health
        if (!system_health_monitor_->is_system_healthy()) {
            response.authorized = false;
            response.rejection_reason = "System health concerns detected";
            response.confidence_score = 0.3;
            return response;
        }
        
        // 6. Calculate risk score and confidence
        double risk_score = calculate_trade_risk_score(exchange, symbol, quantity, price);
        response.confidence_score = std::max(0.0, 1.0 - risk_score);
        
        // 7. Add warnings for elevated risk conditions
        collect_risk_warnings(response.warnings);
        
        // 8. Final authorization decision
        if (risk_score < 0.8 && response.confidence_score > 0.5) {
            response.authorized = true;
            
            // Log successful authorization
            log_risk_event(RiskControlEvent{
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
                current_status_.load(),
                "UNIFIED_CONTROLLER",
                "Trade authorized: " + symbol + " qty=" + std::to_string(quantity),
                "AUTHORIZED",
                risk_score
            });
        } else {
            response.authorized = false;
            response.rejection_reason = "Risk score too high: " + std::to_string(risk_score);
            
            // Log rejection
            log_risk_event(RiskControlEvent{
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
                current_status_.load(),
                "UNIFIED_CONTROLLER",
                "Trade rejected: " + symbol + " risk_score=" + std::to_string(risk_score),
                "REJECTED",
                risk_score
            });
        }
        
        return response;
    }
    
    /**
     * Update market data across all risk components
     */
    void update_market_data(const std::string& exchange, const std::string& symbol,
                           double price, double bid, double ask, double volume) {
        
        // Update comprehensive risk manager
        comprehensive_risk_manager_->update_market_data(exchange, symbol, price, bid, ask, volume);
        
        // Update position tracker
        position_tracker_->update_market_price(exchange, symbol, price);
        
        // Update system health monitor with exchange heartbeat
        system_health_monitor_->update_exchange_heartbeat(exchange);
    }
    
    /**
     * Report a trade execution to all risk components
     */
    void report_trade_execution(const std::string& exchange, const std::string& symbol,
                               double quantity, double price) {
        
        // Update position tracker
        position_tracker_->add_trade(exchange, symbol, quantity, price);
        
        // Log trade event
        log_risk_event(RiskControlEvent{
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
            current_status_.load(),
            "TRADE_EXECUTION",
            "Trade executed: " + symbol + " qty=" + std::to_string(quantity) + " price=" + std::to_string(price),
            "EXECUTED",
            0.0
        });
    }
    
    /**
     * Emergency controls
     */
    void emergency_stop(const std::string& reason) {
        current_status_ = RiskControlStatus::EMERGENCY_STOP;
        trading_enabled_ = false;
        
        // Trigger emergency stop in all components
        comprehensive_risk_manager_->emergency_shutdown(reason);
        
        printf("🚨 UNIFIED RISK CONTROLLER - EMERGENCY STOP: %s\n", reason.c_str());
        
        log_risk_event(RiskControlEvent{
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
            RiskControlStatus::EMERGENCY_STOP,
            "UNIFIED_CONTROLLER",
            "Emergency stop activated: " + reason,
            "EMERGENCY_STOP",
            1.0
        });
        
        // Notify all callbacks
        notify_event_callbacks();
    }
    
    void reset_emergency_stop() {
        if (current_status_ == RiskControlStatus::EMERGENCY_STOP) {
            current_status_ = RiskControlStatus::NORMAL;
            trading_enabled_ = true;
            emergency_override_active_ = false;
            
            // Reset in components
            comprehensive_risk_manager_->get_enhanced_risk_manager()->reset_emergency_stop();
            
            printf("✅ Emergency stop reset - trading can resume\n");
            
            log_risk_event(RiskControlEvent{
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
                RiskControlStatus::NORMAL,
                "UNIFIED_CONTROLLER",
                "Emergency stop reset",
                "RESET",
                0.0
            });
        }
    }
    
    void enable_emergency_override() {
        emergency_override_active_ = true;
        printf("🔒 Emergency override activated - manual control required\n");
    }
    
    void disable_emergency_override() {
        emergency_override_active_ = false;
        printf("🔓 Emergency override deactivated\n");
    }
    
    /**
     * System control
     */
    void enable_trading() {
        if (current_status_ != RiskControlStatus::EMERGENCY_STOP) {
            trading_enabled_ = true;
            printf("✅ Trading enabled\n");
        }
    }
    
    void disable_trading() {
        trading_enabled_ = false;
        printf("🛑 Trading disabled\n");
    }
    
    /**
     * Event management
     */
    void register_event_callback(std::function<void(const RiskControlEvent&)> callback) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        event_callbacks_.push_back(callback);
    }
    
    /**
     * Reporting and status
     */
    void generate_unified_risk_report() const {
        printf("\n🎛️ === UNIFIED RISK CONTROL REPORT ===\n");
        
        printf("\n🚦 Overall System Status: ");
        switch (current_status_.load()) {
            case RiskControlStatus::NORMAL:
                printf("NORMAL ✅\n");
                break;
            case RiskControlStatus::WARNING:
                printf("WARNING ⚠️\n");
                break;
            case RiskControlStatus::CRITICAL:
                printf("CRITICAL 🚨\n");
                break;
            case RiskControlStatus::EMERGENCY_STOP:
                printf("EMERGENCY STOP 🛑\n");
                break;
        }
        
        printf("Trading Enabled: %s\n", trading_enabled_ ? "YES" : "NO");
        printf("Emergency Override: %s\n", emergency_override_active_ ? "ACTIVE" : "INACTIVE");
        
        // Component reports
        printf("\n📊 Component Status:\n");
        comprehensive_risk_manager_->generate_risk_report();
        system_health_monitor_->print_health_summary();
        position_tracker_->print_status();
        
        // Recent events
        printf("\n📋 Recent Risk Events (last 10):\n");
        std::lock_guard<std::mutex> lock(events_mutex_);
        size_t start_idx = (recent_events_.size() > 10) ? recent_events_.size() - 10 : 0;
        for (size_t i = start_idx; i < recent_events_.size(); ++i) {
            const auto& event = recent_events_[i];
            printf("  %s: %s (severity: %.2f)\n",
                   event.component.c_str(), event.description.c_str(), event.severity_score);
        }
        
        printf("=========================================\n\n");
    }
    
    // Accessors
    RiskControlStatus get_current_status() const { return current_status_.load(); }
    bool is_trading_enabled() const { return trading_enabled_.load(); }
    bool is_emergency_override_active() const { return emergency_override_active_.load(); }
    
    ComprehensiveRiskManager* get_comprehensive_risk_manager() { 
        return comprehensive_risk_manager_.get(); 
    }
    
    SystemHealthMonitor* get_system_health_monitor() { 
        return system_health_monitor_.get(); 
    }

private:
    void start_monitoring() {
        monitoring_active_ = true;
        system_health_monitor_->start_monitoring();
        
        monitoring_thread_ = std::thread([this]() {
            while (monitoring_active_) {
                update_overall_status();
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(monitoring_interval_ms_.load()));
            }
        });
        
        printf("🔄 Unified risk monitoring started\n");
    }
    
    void stop_monitoring() {
        monitoring_active_ = false;
        
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
        
        system_health_monitor_->stop_monitoring();
        printf("🛑 Unified risk monitoring stopped\n");
    }
    
    void update_overall_status() {
        RiskControlStatus new_status = RiskControlStatus::NORMAL;
        
        // Check if we're in emergency stop
        if (current_status_ == RiskControlStatus::EMERGENCY_STOP) {
            return; // Don't change from emergency stop automatically
        }
        
        // Check comprehensive risk manager
        if (comprehensive_risk_manager_->is_emergency_mode()) {
            new_status = RiskControlStatus::EMERGENCY_STOP;
        }
        
        // Check system health
        else if (system_health_monitor_->is_critical_alert_active()) {
            new_status = RiskControlStatus::CRITICAL;
        }
        
        // Check for warning conditions
        else if (!system_health_monitor_->is_system_healthy()) {
            new_status = RiskControlStatus::WARNING;
        }
        
        // Update status if changed
        RiskControlStatus old_status = current_status_.exchange(new_status);
        if (old_status != new_status) {
            printf("🚦 Risk control status changed: %d -> %d\n", 
                   static_cast<int>(old_status), static_cast<int>(new_status));
            
            log_risk_event(RiskControlEvent{
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
                new_status,
                "UNIFIED_CONTROLLER",
                "Status change from " + std::to_string(static_cast<int>(old_status)) + 
                " to " + std::to_string(static_cast<int>(new_status)),
                "STATUS_CHANGE",
                static_cast<double>(new_status) / 3.0
            });
        }
    }
    
    double calculate_trade_risk_score(const std::string& exchange, const std::string& symbol,
                                     double quantity, double price) {
        double total_score = 0.0;
        
        // Market risk component
        auto market_metrics = comprehensive_risk_manager_->get_market_risk_metrics(symbol);
        double market_risk = market_metrics.volatility_1h * risk_weights_.market_risk_weight;
        
        // Position risk component  
        auto position = position_tracker_->get_position(exchange, symbol);
        double position_risk = (std::abs(position.quantity + quantity) / 10.0) * risk_weights_.position_risk_weight;
        
        // System risk component
        auto perf_metrics = system_health_monitor_->get_performance_metrics();
        double system_risk = (perf_metrics.cpu_usage_percent / 100.0) * risk_weights_.system_risk_weight;
        
        total_score = market_risk + position_risk + system_risk;
        
        return std::min(1.0, total_score);
    }
    
    void collect_risk_warnings(std::vector<std::string>& warnings) {
        // Check for warning conditions
        auto perf_metrics = system_health_monitor_->get_performance_metrics();
        
        if (perf_metrics.cpu_usage_percent > 80.0) {
            warnings.push_back("High CPU usage: " + std::to_string(perf_metrics.cpu_usage_percent) + "%");
        }
        
        if (perf_metrics.memory_usage_percent > 75.0) {
            warnings.push_back("High memory usage: " + std::to_string(perf_metrics.memory_usage_percent) + "%");
        }
        
        // Check dynamic limits
        auto dynamic_limits = comprehensive_risk_manager_->get_dynamic_limits();
        if (dynamic_limits.volatility_multiplier < 0.5) {
            warnings.push_back("Risk limits reduced due to high volatility");
        }
        
        if (dynamic_limits.liquidity_multiplier < 0.5) {
            warnings.push_back("Risk limits reduced due to low liquidity");
        }
    }
    
    void log_risk_event(const RiskControlEvent& event) {
        std::lock_guard<std::mutex> lock(events_mutex_);
        
        recent_events_.push_back(event);
        
        // Maintain rolling window
        if (recent_events_.size() > MAX_RECENT_EVENTS) {
            recent_events_.erase(recent_events_.begin());
        }
        
        // Log to file
        std::ofstream log_file("unified_risk_events.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << event.timestamp_ns << ","
                    << static_cast<int>(event.status) << ","
                    << event.component << ","
                    << event.description << ","
                    << event.action_taken << ","
                    << event.severity_score << "\n";
        }
    }
    
    void notify_event_callbacks() {
        std::lock_guard<std::mutex> events_lock(events_mutex_);
        std::lock_guard<std::mutex> callbacks_lock(callbacks_mutex_);
        
        if (!recent_events_.empty()) {
            const auto& latest_event = recent_events_.back();
            for (const auto& callback : event_callbacks_) {
                try {
                    callback(latest_event);
                } catch (const std::exception& e) {
                    printf("Error in risk event callback: %s\n", e.what());
                }
            }
        }
    }
};

} // namespace risk
