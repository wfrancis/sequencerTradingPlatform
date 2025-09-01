#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <array>
#include <memory>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <functional>
#include "../messages.hpp"
#include "../sequencer.hpp"
#include "../logger.hpp"
#include <ctime>
#include <iomanip>

namespace hft::monitoring {

// Performance targets and thresholds
struct MonitoringThresholds {
    uint64_t max_latency_ns = 50000000ULL;     // 50ms target
    double max_spread_bps = 1000.0;            // 100bps max spread alert
    double min_profit_threshold = -1000.0;     // -$1000 max loss
    uint32_t max_error_rate_pct = 10;          // 10% max error rate
    uint64_t heartbeat_timeout_ns = 5000000000ULL; // 5s component timeout
    uint32_t max_queue_depth = 1000;           // Max queue depth before alert
};

struct LatencyMetrics {
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0;
    uint64_t avg_ns = 0;
    uint64_t p50_ns = 0;
    uint64_t p95_ns = 0;
    uint64_t p99_ns = 0;
    uint64_t sample_count = 0;
    
    // Ring buffer for percentile calculations
    static constexpr size_t SAMPLE_BUFFER_SIZE = 1000;
    std::array<uint64_t, SAMPLE_BUFFER_SIZE> samples{};
    size_t sample_index = 0;
    
    void add_sample(uint64_t latency_ns) {
        min_ns = std::min(min_ns, latency_ns);
        max_ns = std::max(max_ns, latency_ns);
        
        samples[sample_index] = latency_ns;
        sample_index = (sample_index + 1) % SAMPLE_BUFFER_SIZE;
        sample_count++;
        
        // Update percentiles every 100 samples for efficiency
        if (sample_count % 100 == 0) {
            calculate_percentiles();
        }
    }
    
private:
    void calculate_percentiles() {
        auto working_samples = samples;
        size_t valid_samples = std::min(static_cast<size_t>(sample_count), SAMPLE_BUFFER_SIZE);
        std::sort(working_samples.begin(), working_samples.begin() + valid_samples);
        
        if (valid_samples > 0) {
            avg_ns = std::accumulate(working_samples.begin(), 
                                   working_samples.begin() + valid_samples, 0ULL) / valid_samples;
            p50_ns = working_samples[valid_samples * 50 / 100];
            p95_ns = working_samples[valid_samples * 95 / 100];
            p99_ns = working_samples[valid_samples * 99 / 100];
        }
    }
};

struct SpreadMetrics {
    double current_spread_bps = 0.0;
    double avg_spread_bps = 0.0;
    double max_spread_bps = 0.0;
    double min_spread_bps = 10000.0;
    uint64_t spread_samples = 0;
    
    void update_spread(double spread_bps) {
        current_spread_bps = spread_bps;
        max_spread_bps = std::max(max_spread_bps, spread_bps);
        min_spread_bps = std::min(min_spread_bps, spread_bps);
        
        // Running average
        avg_spread_bps = (avg_spread_bps * spread_samples + spread_bps) / (spread_samples + 1);
        spread_samples++;
    }
};

struct ProfitabilityMetrics {
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double total_fees = 0.0;
    double gross_profit = 0.0;
    double net_profit = 0.0;
    uint64_t profitable_trades = 0;
    uint64_t total_trades = 0;
    
    // Risk metrics
    double max_drawdown = 0.0;
    double current_drawdown = 0.0;
    double peak_pnl = 0.0;
    
    void add_trade(double trade_pnl, double fee) {
        total_trades++;
        total_fees += fee;
        gross_profit += trade_pnl;
        net_profit = gross_profit - total_fees;
        
        if (trade_pnl > 0) {
            profitable_trades++;
        }
        
        // Update drawdown tracking
        if (net_profit > peak_pnl) {
            peak_pnl = net_profit;
        }
        
        current_drawdown = peak_pnl - net_profit;
        max_drawdown = std::max(max_drawdown, current_drawdown);
    }
    
    double get_win_rate() const {
        return total_trades > 0 ? (double)profitable_trades / total_trades : 0.0;
    }
};

struct ComponentHealth {
    std::atomic<uint64_t> last_heartbeat_ns{0};
    std::atomic<uint64_t> message_count{0};
    std::atomic<uint64_t> error_count{0};
    std::atomic<bool> is_healthy{true};
    std::string component_name;
    
    ComponentHealth(const std::string& name) : component_name(name) {
        heartbeat();
    }
    
    void heartbeat() {
        last_heartbeat_ns.store(hft::rdtsc(), std::memory_order_relaxed);
    }
    
    void increment_messages() {
        message_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    void increment_errors() {
        error_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    bool check_health(uint64_t timeout_ns, uint32_t max_error_rate_pct) {
        uint64_t current_time = hft::rdtsc();
        uint64_t last_hb = last_heartbeat_ns.load(std::memory_order_relaxed);
        
        // Convert TSC to nanoseconds (approximate)
        bool heartbeat_ok = (current_time - last_hb) < (timeout_ns * 2400); // Assume 2.4GHz TSC
        
        uint64_t msgs = message_count.load(std::memory_order_relaxed);
        uint64_t errs = error_count.load(std::memory_order_relaxed);
        bool error_rate_ok = (msgs == 0) || ((errs * 100 / msgs) <= max_error_rate_pct);
        
        bool healthy = heartbeat_ok && error_rate_ok;
        is_healthy.store(healthy, std::memory_order_relaxed);
        return healthy;
    }
};

enum class AlertLevel {
    INFO = 0,
    WARNING = 1,
    CRITICAL = 2,
    FATAL = 3
};

struct Alert {
    AlertLevel level;
    std::string component;
    std::string message;
    uint64_t timestamp_ns;
    
    std::string to_string() const {
        const char* level_str[] = {"INFO", "WARN", "CRIT", "FATAL"};
        return std::string(level_str[static_cast<int>(level)]) + " [" + component + "] " + message;
    }
};

/**
 * Real-Time HFT Monitoring System
 * 
 * Provides comprehensive monitoring including:
 * - End-to-end latency tracking (<50ms target)
 * - Spread monitoring and alerts
 * - Profitability metrics and drawdown tracking
 * - Component health monitoring
 * - Emergency kill switch
 * - Real-time alerting system
 */
class RealTimeMonitor {
private:
    MonitoringThresholds thresholds_;
    
    // Metrics storage
    std::unordered_map<std::string, LatencyMetrics> latency_metrics_;
    std::unordered_map<std::string, SpreadMetrics> spread_metrics_;
    ProfitabilityMetrics profitability_;
    
    // Component health tracking
    std::unordered_map<std::string, std::unique_ptr<ComponentHealth>> components_;
    
    // Emergency controls
    std::atomic<bool> emergency_stop_{false};
    std::atomic<bool> trading_halted_{false};
    std::string emergency_reason_;
    
    // Alert system
    std::vector<Alert> recent_alerts_;
    static constexpr size_t MAX_ALERTS = 100;
    std::function<void(const Alert&)> alert_callback_;
    
    // Monitoring thread
    std::atomic<bool> monitoring_active_{false};
    std::thread monitoring_thread_;
    
    // Performance tracking
    uint64_t monitoring_start_time_;
    
    // Dashboard state
    struct DashboardState {
        std::atomic<uint64_t> update_count{0};
        std::atomic<uint64_t> last_update_ns{0};
        std::atomic<bool> dashboard_enabled{false};
        uint64_t refresh_interval_ms = 1000; // 1 second default
    } dashboard_;

public:
    RealTimeMonitor(const MonitoringThresholds& thresholds = MonitoringThresholds{})
        : thresholds_(thresholds), monitoring_start_time_(hft::rdtsc()) {
        
        // Initialize core component monitors
        register_component("sequencer");
        register_component("feed_handler"); 
        register_component("risk_manager");
        register_component("position_tracker");
        register_component("connection_manager");
        
        hft::Logger::instance().log(hft::LogLevel::INFO, "monitoring", "Real-time monitor initialized with " + std::to_string(components_.size()) + " components");
    }
    
    ~RealTimeMonitor() {
        stop_monitoring();
    }
    
    // Component registration and health monitoring
    void register_component(const std::string& name) {
        components_[name] = std::make_unique<ComponentHealth>(name);
        hft::Logger::instance().log(hft::LogLevel::INFO, "monitoring", "Registered component for monitoring: " + name);
    }
    
    void component_heartbeat(const std::string& component) {
        auto it = components_.find(component);
        if (it != components_.end()) {
            it->second->heartbeat();
        }
    }
    
    void component_message_processed(const std::string& component) {
        auto it = components_.find(component);
        if (it != components_.end()) {
            it->second->increment_messages();
        }
    }
    
    void component_error(const std::string& component, const std::string& error_msg) {
        auto it = components_.find(component);
        if (it != components_.end()) {
            it->second->increment_errors();
        }
        
        raise_alert(AlertLevel::WARNING, component, "Error: " + error_msg);
    }
    
    // Latency monitoring
    void start_latency_measurement(const std::string& operation_name) {
        // Store start timestamp for the operation
        // This could be enhanced with thread-local storage for multi-threaded scenarios
    }
    
    void record_latency(const std::string& operation_name, uint64_t start_time_ns) {
        uint64_t end_time = hft::rdtsc();
        uint64_t latency_ns = (end_time - start_time_ns) * 1000000000ULL / 2400000000ULL; // Approx conversion
        
        latency_metrics_[operation_name].add_sample(latency_ns);
        
        // Check against threshold
        if (latency_ns > thresholds_.max_latency_ns) {
            raise_alert(AlertLevel::WARNING, "latency", 
                       "High latency detected in " + operation_name + ": " + 
                       std::to_string(latency_ns / 1000000.0) + "ms");
        }
    }
    
    // Spread monitoring
    void update_spread(const std::string& symbol, Venue venue, double spread_bps) {
        std::string key = symbol + "_" + std::to_string(static_cast<int>(venue));
        spread_metrics_[key].update_spread(spread_bps);
        
        if (spread_bps > thresholds_.max_spread_bps) {
            raise_alert(AlertLevel::CRITICAL, "spread", 
                       "Excessive spread on " + key + ": " + std::to_string(spread_bps) + " bps");
        }
    }
    
    // Profitability monitoring
    void record_trade(double pnl, double fee, const std::string& details = "") {
        profitability_.add_trade(pnl, fee);
        
        // Check drawdown limits
        if (profitability_.current_drawdown > std::abs(thresholds_.min_profit_threshold)) {
            raise_alert(AlertLevel::CRITICAL, "risk", 
                       "Drawdown limit exceeded: $" + std::to_string(profitability_.current_drawdown));
        }
        
        // Check daily loss limits
        if (profitability_.net_profit < thresholds_.min_profit_threshold) {
            raise_alert(AlertLevel::FATAL, "risk", 
                       "Daily loss limit exceeded: $" + std::to_string(profitability_.net_profit));
            trigger_emergency_stop("Daily loss limit exceeded");
        }
    }
    
    // Emergency controls
    void trigger_emergency_stop(const std::string& reason) {
        emergency_stop_.store(true, std::memory_order_release);
        trading_halted_.store(true, std::memory_order_release);
        emergency_reason_ = reason;
        
        raise_alert(AlertLevel::FATAL, "emergency", "EMERGENCY STOP: " + reason);
        hft::Logger::instance().log(hft::LogLevel::CRITICAL, "emergency", "EMERGENCY STOP TRIGGERED: " + reason);
        
        // Immediately notify all components
        for (auto& [name, health] : components_) {
            health->is_healthy.store(false, std::memory_order_release);
        }
    }
    
    bool is_emergency_stop() const {
        return emergency_stop_.load(std::memory_order_acquire);
    }
    
    bool is_trading_halted() const {
        return trading_halted_.load(std::memory_order_acquire);
    }
    
    void resume_trading() {
        if (!emergency_stop_.load()) {
            trading_halted_.store(false, std::memory_order_release);
            raise_alert(AlertLevel::INFO, "control", "Trading resumed");
        }
    }
    
    std::string get_emergency_reason() const {
        return emergency_reason_;
    }
    
    // Alert system
    void set_alert_callback(std::function<void(const Alert&)> callback) {
        alert_callback_ = callback;
    }
    
    void raise_alert(AlertLevel level, const std::string& component, const std::string& message) {
        Alert alert{level, component, message, hft::rdtsc()};
        
        // Store recent alerts
        recent_alerts_.push_back(alert);
        if (recent_alerts_.size() > MAX_ALERTS) {
            recent_alerts_.erase(recent_alerts_.begin());
        }
        
        // Trigger callback if set
        if (alert_callback_) {
            alert_callback_(alert);
        }
        
        // Log based on severity
        switch (level) {
            case AlertLevel::INFO:
                hft::Logger::instance().log(hft::LogLevel::INFO, "alerts", "ALERT: " + alert.to_string());
                break;
            case AlertLevel::WARNING:
                hft::Logger::instance().log(hft::LogLevel::WARNING, "alerts", "ALERT: " + alert.to_string());
                break;
            case AlertLevel::CRITICAL:
                hft::Logger::instance().log(hft::LogLevel::ERROR, "alerts", "ALERT: " + alert.to_string());
                break;
            case AlertLevel::FATAL:
                hft::Logger::instance().log(hft::LogLevel::CRITICAL, "alerts", "ALERT: " + alert.to_string());
                break;
        }
    }
    
    // Monitoring control
    void start_monitoring() {
        if (monitoring_active_.load()) {
            return;
        }
        
        monitoring_active_.store(true);
        monitoring_thread_ = std::thread(&RealTimeMonitor::monitoring_loop, this);
        dashboard_.dashboard_enabled.store(true);
        
        hft::Logger::instance().log(hft::LogLevel::INFO, "monitoring", "Real-time monitoring started");
        raise_alert(AlertLevel::INFO, "monitor", "Real-time monitoring started");
    }
    
    void stop_monitoring() {
        monitoring_active_.store(false);
        dashboard_.dashboard_enabled.store(false);
        
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
        
        hft::Logger::instance().log(hft::LogLevel::INFO, "monitoring", "Real-time monitoring stopped");
    }
    
    // Status and reporting
    void print_dashboard() const {
        if (!dashboard_.dashboard_enabled.load()) return;
        
        system("clear"); // Clear console for real-time dashboard
        
        std::cout << "\n🔥 HFT REAL-TIME MONITORING DASHBOARD 🔥\n";
        std::cout << "==========================================\n";
        
        // System status
        std::cout << "\n📊 SYSTEM STATUS\n";
        std::cout << "Emergency Stop: " << (emergency_stop_.load() ? "🔴 ACTIVE" : "🟢 OK") << "\n";
        std::cout << "Trading Halted: " << (trading_halted_.load() ? "🔴 YES" : "🟢 NO") << "\n";
        
        if (emergency_stop_.load()) {
            std::cout << "Emergency Reason: " << emergency_reason_ << "\n";
        }
        
        // Component health
        std::cout << "\n🏥 COMPONENT HEALTH\n";
        for (const auto& [name, health] : components_) {
            bool is_healthy = health->check_health(thresholds_.heartbeat_timeout_ns, 
                                                 thresholds_.max_error_rate_pct);
            std::cout << name << ": " << (is_healthy ? "🟢" : "🔴") 
                      << " (Msgs: " << health->message_count.load()
                      << ", Errs: " << health->error_count.load() << ")\n";
        }
        
        // Latency metrics
        std::cout << "\n⚡ LATENCY METRICS (Target: <50ms)\n";
        for (const auto& [operation, metrics] : latency_metrics_) {
            if (metrics.sample_count > 0) {
                std::cout << operation << ": ";
                std::cout << "Avg=" << (metrics.avg_ns / 1000000.0) << "ms ";
                std::cout << "P95=" << (metrics.p95_ns / 1000000.0) << "ms ";
                std::cout << "P99=" << (metrics.p99_ns / 1000000.0) << "ms ";
                std::cout << (metrics.p95_ns > thresholds_.max_latency_ns ? "🔴" : "🟢") << "\n";
            }
        }
        
        // Spread metrics
        std::cout << "\n📈 SPREAD METRICS\n";
        for (const auto& [symbol, metrics] : spread_metrics_) {
            std::cout << symbol << ": " << std::fixed << std::setprecision(1) 
                      << metrics.current_spread_bps << " bps ";
            std::cout << "(Avg: " << metrics.avg_spread_bps << " bps) ";
            std::cout << (metrics.current_spread_bps > thresholds_.max_spread_bps ? "🔴" : "🟢") << "\n";
        }
        
        // Profitability
        std::cout << "\n💰 PROFITABILITY\n";
        std::cout << "Net P&L: $" << std::fixed << std::setprecision(2) << profitability_.net_profit;
        std::cout << (profitability_.net_profit >= 0 ? "🟢" : "🔴") << "\n";
        std::cout << "Win Rate: " << (profitability_.get_win_rate() * 100) << "% ";
        std::cout << "(" << profitability_.profitable_trades << "/" << profitability_.total_trades << ")\n";
        std::cout << "Max Drawdown: $" << profitability_.max_drawdown << "\n";
        std::cout << "Current Drawdown: $" << profitability_.current_drawdown << "\n";
        
        // Recent alerts
        std::cout << "\n🚨 RECENT ALERTS (Last " << std::min(recent_alerts_.size(), size_t(5)) << ")\n";
        size_t start_idx = recent_alerts_.size() > 5 ? recent_alerts_.size() - 5 : 0;
        for (size_t i = start_idx; i < recent_alerts_.size(); ++i) {
            const char* emoji[] = {"ℹ️", "⚠️", "🔴", "💀"};
            std::cout << emoji[static_cast<int>(recent_alerts_[i].level)] << " " 
                      << recent_alerts_[i].to_string() << "\n";
        }
        
        std::cout << "\n⏰ Last Update: " << get_current_time_string() << "\n";
        std::cout << "Press Ctrl+C to stop monitoring\n";
    }
    
    // Getters for current metrics
    const LatencyMetrics* get_latency_metrics(const std::string& operation) const {
        auto it = latency_metrics_.find(operation);
        return it != latency_metrics_.end() ? &it->second : nullptr;
    }
    
    const ProfitabilityMetrics& get_profitability_metrics() const {
        return profitability_;
    }
    
    std::vector<Alert> get_recent_alerts(size_t count = 10) const {
        size_t start = recent_alerts_.size() > count ? recent_alerts_.size() - count : 0;
        return std::vector<Alert>(recent_alerts_.begin() + start, recent_alerts_.end());
    }
    
private:
    void monitoring_loop() {
        while (monitoring_active_.load()) {
            // Check component health
            for (auto& [name, health] : components_) {
                if (!health->check_health(thresholds_.heartbeat_timeout_ns, 
                                        thresholds_.max_error_rate_pct)) {
                    raise_alert(AlertLevel::CRITICAL, name, "Component unhealthy");
                }
            }
            
            // Update dashboard
            if (dashboard_.dashboard_enabled.load()) {
                dashboard_.update_count.fetch_add(1);
                dashboard_.last_update_ns.store(hft::rdtsc());
                print_dashboard();
            }
            
            // Sleep for refresh interval
            std::this_thread::sleep_for(std::chrono::milliseconds(dashboard_.refresh_interval_ms));
        }
    }
    
    std::string get_current_time_string() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
};

} // namespace hft::monitoring
