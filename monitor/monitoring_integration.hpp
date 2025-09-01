#pragma once

#include "real_time_monitor.hpp"
#include "../messages.hpp"
#include "../sequencer.hpp"
#include "../feed_handler.hpp"
#include "../position_tracker.hpp"
#include "../risk/enhanced_risk_manager.hpp"

namespace hft::monitoring {

/**
 * Integration layer for real-time monitoring
 * Provides instrumentation wrappers and automatic metric collection
 */
class MonitoringIntegration {
public:
    RealTimeMonitor& monitor_;
private:
    
    // Timing helpers for latency measurement
    struct TimingContext {
        uint64_t start_time;
        std::string operation_name;
    };
    
    thread_local static std::unordered_map<std::string, TimingContext> active_timers_;

public:
    explicit MonitoringIntegration(RealTimeMonitor& monitor) : monitor_(monitor) {}
    
    // RAII latency measurement helper
    class LatencyTracker {
    private:
        RealTimeMonitor& monitor_;
        std::string operation_;
        uint64_t start_time_;
        
    public:
        LatencyTracker(RealTimeMonitor& monitor, const std::string& operation)
            : monitor_(monitor), operation_(operation), start_time_(::hft::rdtsc()) {}
        
        ~LatencyTracker() {
            monitor_.record_latency(operation_, start_time_);
        }
    };
    
    // Market data processing instrumentation
    void instrument_market_data_processing(const ::hft::SequencedMessage& msg) {
        monitor_.component_heartbeat("feed_handler");
        monitor_.component_message_processed("feed_handler");
        
        // Calculate and track spread if it's market data
        if (msg.type == ::hft::MessageType::MARKET_DATA_TICK) {
            double bid = ::hft::to_float_price(msg.market_data.bid_price);
            double ask = ::hft::to_float_price(msg.market_data.ask_price);
            
            if (bid > 0 && ask > 0 && ask > bid) {
                double mid = (bid + ask) / 2.0;
                double spread_bps = ((ask - bid) / mid) * 10000.0;
                
                std::string symbol = (msg.symbol_id == 1) ? "BTC" : "ETH";
                monitor_.update_spread(symbol, msg.venue, spread_bps);
            }
        }
    }
    
    // Order processing instrumentation
    void instrument_order_processing(const std::string& operation_type) {
        monitor_.component_heartbeat("sequencer");
        monitor_.component_message_processed("sequencer");
    }
    
    // Risk management instrumentation
    void instrument_risk_check(const std::string& check_type, bool passed, 
                              const std::string& details = "") {
        monitor_.component_heartbeat("risk_manager");
        monitor_.component_message_processed("risk_manager");
        
        if (!passed) {
            monitor_.component_error("risk_manager", "Risk check failed: " + check_type + " - " + details);
        }
    }
    
    // Position tracking instrumentation
    void instrument_position_update(const std::string& exchange, const std::string& symbol,
                                   double quantity, double price) {
        monitor_.component_heartbeat("position_tracker");
        monitor_.component_message_processed("position_tracker");
        
        // Calculate trade P&L (simplified - in reality would need more context)
        double notional = quantity * price;
        double estimated_fee = std::abs(notional) * 0.001; // 0.1% fee assumption
        
        // For monitoring purposes, treat each trade as break-even except for fees
        monitor_.record_trade(0.0, estimated_fee, 
                             exchange + " " + symbol + " " + std::to_string(quantity));
    }
    
    // Connection health instrumentation
    void instrument_connection_event(::hft::Venue venue, const std::string& event_type, bool success) {
        monitor_.component_heartbeat("connection_manager");
        
        if (success) {
            monitor_.component_message_processed("connection_manager");
        } else {
            monitor_.component_error("connection_manager", 
                venue_name(venue) + " " + event_type + " failed");
        }
    }
    
    // Emergency stop integration
    bool check_emergency_conditions() {
        if (monitor_.is_emergency_stop()) {
            return true;
        }
        
        // Additional emergency conditions can be checked here
        // For example, checking if critical components are unhealthy
        
        return false;
    }
    
    // Create latency tracker for automatic RAII timing
    LatencyTracker track_latency(const std::string& operation) {
        return LatencyTracker(monitor_, operation);
    }

private:
    std::string venue_name(::hft::Venue venue) const {
        switch (venue) {
            case ::hft::Venue::COINBASE: return "Coinbase";
            case ::hft::Venue::BINANCE: return "Binance";
            default: return "Unknown";
        }
    }
};

// Thread-local storage definition
thread_local std::unordered_map<std::string, MonitoringIntegration::TimingContext> 
    MonitoringIntegration::active_timers_;

/**
 * Enhanced feed handler with monitoring integration
 */
class MonitoredFeedHandler {
private:
    ::hft::MockFeedHandler& base_handler_;
    MonitoringIntegration& monitor_integration_;
    
public:
    MonitoredFeedHandler(::hft::MockFeedHandler& handler, MonitoringIntegration& monitor)
        : base_handler_(handler), monitor_integration_(monitor) {}
    
    void start() {
        base_handler_.start();
    }
    
    void stop() {
        base_handler_.stop();
    }
    
    // Override market data processing to add monitoring
    void process_market_data_with_monitoring(const ::hft::SequencedMessage& msg) {
        auto latency_tracker = monitor_integration_.track_latency("market_data_processing");
        monitor_integration_.instrument_market_data_processing(msg);
        
        // Check for emergency stop
        if (monitor_integration_.check_emergency_conditions()) {
            hft::Logger::instance().log(hft::LogLevel::CRITICAL, "feed_handler", "Emergency stop detected in feed handler");
            return;
        }
    }
};

/**
 * Enhanced arbitrage strategy with monitoring integration
 */
class MonitoredArbitrageStrategy {
private:
    ::hft::ArbitrageStrategy& base_strategy_;
    MonitoringIntegration& monitor_integration_;
    RealTimeMonitor& monitor_;
    
    // Performance tracking
    uint64_t last_signal_time_ = 0;
    uint64_t signal_count_ = 0;
    
public:
    MonitoredArbitrageStrategy(::hft::ArbitrageStrategy& strategy, 
                              MonitoringIntegration& monitor_integration,
                              RealTimeMonitor& monitor)
        : base_strategy_(strategy), monitor_integration_(monitor_integration), monitor_(monitor) {}
    
    void process_market_data() {
        // Check emergency conditions first
        if (monitor_integration_.check_emergency_conditions()) {
            hft::Logger::instance().log(hft::LogLevel::WARNING, "strategy", "Trading halted due to emergency conditions");
            return;
        }
        
        auto latency_tracker = monitor_integration_.track_latency("arbitrage_processing");
        
        // Call original processing
        base_strategy_.process_market_data();
        
        // Update monitoring
        monitor_.component_heartbeat("arbitrage_strategy");
        monitor_.component_message_processed("arbitrage_strategy");
    }
    
    void record_arbitrage_signal(double expected_edge_bps, double actual_pnl = 0.0) {
        signal_count_++;
        uint64_t current_time = ::hft::rdtsc();
        
        // Calculate signal frequency
        if (last_signal_time_ > 0) {
            uint64_t signal_interval = current_time - last_signal_time_;
            monitor_.record_latency("signal_generation_interval", last_signal_time_);
        }
        
        last_signal_time_ = current_time;
        
        // Record the trade result
        double estimated_fee = 0.002; // 0.2% round-trip fee
        monitor_.record_trade(actual_pnl, estimated_fee, 
                             "Arbitrage signal #" + std::to_string(signal_count_));
        
        hft::Logger::instance().log(hft::LogLevel::INFO, "strategy", "Arbitrage signal recorded: " + 
                                    std::to_string(expected_edge_bps) + " bps edge, $" + std::to_string(actual_pnl) + " P&L");
    }
    
    void print_status() const {
        base_strategy_.print_status();
    }
};

/**
 * Monitored position tracker
 */
class MonitoredPositionTracker {
private:
    PositionTracker& base_tracker_;
    MonitoringIntegration& monitor_integration_;
    
public:
    MonitoredPositionTracker(PositionTracker& tracker, MonitoringIntegration& monitor)
        : base_tracker_(tracker), monitor_integration_(monitor) {}
    
    void add_trade(const std::string& exchange, const std::string& symbol, 
                   double quantity, double price) {
        auto latency_tracker = monitor_integration_.track_latency("position_update");
        
        base_tracker_.add_trade(exchange, symbol, quantity, price);
        monitor_integration_.instrument_position_update(exchange, symbol, quantity, price);
    }
    
    void update_market_price(const std::string& exchange, const std::string& symbol, double price) {
        base_tracker_.update_market_price(exchange, symbol, price);
        monitor_integration_.monitor_.component_heartbeat("position_tracker");
    }
    
    void print_status() const {
        base_tracker_.print_status();
    }
};

/**
 * Monitored risk manager
 */
class MonitoredRiskManager {
private:
    EnhancedRiskManager& base_manager_;
    MonitoringIntegration& monitor_integration_;
    
public:
    MonitoredRiskManager(EnhancedRiskManager& manager, MonitoringIntegration& monitor)
        : base_manager_(manager), monitor_integration_(monitor) {}
    
    bool can_execute_trade(const std::string& exchange, const std::string& symbol,
                          double quantity, double price, double expected_edge_bps) {
        auto latency_tracker = monitor_integration_.track_latency("risk_check");
        
        bool result = base_manager_.can_execute_trade(exchange, symbol, quantity, price, expected_edge_bps);
        
        monitor_integration_.instrument_risk_check("trade_execution", result,
            exchange + " " + symbol + " qty=" + std::to_string(quantity));
        
        return result;
    }
    
    bool is_trading_halted() const {
        // Check both base manager and monitoring system
        return base_manager_.is_trading_halted() || 
               monitor_integration_.check_emergency_conditions();
    }
    
    void print_risk_status() const {
        base_manager_.print_risk_status();
    }
};

} // namespace hft::monitoring
