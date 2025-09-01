#pragma once

#include <chrono>
#include <atomic>
#include <string>
#include <fstream>
#include <mutex>
#include <thread>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include "position_tracker.hpp"

namespace hft {

/**
 * Session Configuration
 */
struct SessionConfig {
    // Timer settings
    double max_session_hours = 4.0;           // Auto-shutdown after X hours
    
    // Risk limits
    double daily_loss_limit = 500.0;          // Emergency stop at -$X loss
    double daily_profit_target = 1000.0;      // Optional auto-stop at +$X profit
    bool enable_profit_target = false;
    
    // Battery settings
    double battery_pause_threshold = 20.0;    // Pause trading below X% battery
    bool enable_battery_monitoring = true;
    
    // Session logging
    std::string session_log_file = "session_logs.csv";
    bool enable_session_logging = true;
    
    // Emergency settings
    bool enable_emergency_stops = true;
    double max_drawdown_percent = 5.0;        // Emergency stop at X% account drawdown
};

/**
 * Session Statistics
 */
struct SessionStats {
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    double starting_balance = 0.0;
    double ending_balance = 0.0;
    double session_pnl = 0.0;
    double max_profit = 0.0;
    double max_drawdown = 0.0;
    
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    
    double total_fees = 0.0;
    double gross_profit = 0.0;
    double gross_loss = 0.0;
    
    std::string stop_reason = "Unknown";
    bool emergency_stop = false;
};

/**
 * System Resource Monitor
 */
class SystemMonitor {
private:
    std::atomic<double> battery_level_{100.0};
    std::atomic<bool> on_battery_power_{false};
    std::thread monitor_thread_;
    std::atomic<bool> monitoring_active_{false};
    
public:
    SystemMonitor() = default;
    ~SystemMonitor() { stop_monitoring(); }
    
    void start_monitoring() {
        monitoring_active_ = true;
        monitor_thread_ = std::thread(&SystemMonitor::monitor_loop, this);
    }
    
    void stop_monitoring() {
        monitoring_active_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    double get_battery_level() const { return battery_level_.load(); }
    bool is_on_battery() const { return on_battery_power_.load(); }
    
private:
    void monitor_loop() {
        while (monitoring_active_) {
            update_battery_status();
            std::this_thread::sleep_for(std::chrono::seconds(30)); // Check every 30 seconds
        }
    }
    
    void update_battery_status() {
        // macOS battery check using system command
        #ifdef __APPLE__
        FILE* pipe = popen("pmset -g batt | grep -o '[0-9]*%' | head -1 | tr -d '%'", "r");
        if (pipe) {
            char buffer[16];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                battery_level_ = std::atof(buffer);
            }
            pclose(pipe);
        }
        
        // Check if on battery power
        pipe = popen("pmset -g batt | grep 'Battery Power'", "r");
        if (pipe) {
            char buffer[256];
            on_battery_power_ = (fgets(buffer, sizeof(buffer), pipe) != nullptr);
            pclose(pipe);
        }
        #else
        // Linux fallback - simplified
        battery_level_ = 50.0; // Default for non-macOS systems
        on_battery_power_ = false;
        #endif
    }
};

/**
 * Session Manager - Comprehensive session control and risk management
 */
class SessionManager {
private:
    SessionConfig config_;
    SessionStats current_session_;
    PositionTracker* position_tracker_;
    
    std::atomic<bool> session_active_{false};
    std::atomic<bool> trading_paused_{false};
    std::atomic<bool> emergency_stop_triggered_{false};
    
    SystemMonitor system_monitor_;
    
    mutable std::mutex mutex_;
    std::ofstream session_log_;
    
    std::chrono::steady_clock::time_point session_start_;
    std::chrono::steady_clock::time_point last_status_check_;
    
public:
    SessionManager(PositionTracker* position_tracker, const SessionConfig& config = SessionConfig{})
        : config_(config), position_tracker_(position_tracker) {
        
        if (config_.enable_session_logging) {
            init_session_logging();
        }
        
        if (config_.enable_battery_monitoring) {
            system_monitor_.start_monitoring();
        }
        
        printf("🎯 Session Manager initialized\n");
        printf("   Max session: %.1f hours\n", config_.max_session_hours);
        printf("   Daily loss limit: $%.2f\n", config_.daily_loss_limit);
        printf("   Battery monitoring: %s\n", config_.enable_battery_monitoring ? "ENABLED" : "DISABLED");
        printf("   Session logging: %s\n", config_.enable_session_logging ? "ENABLED" : "DISABLED");
    }
    
    ~SessionManager() {
        if (session_active_) {
            end_session("Destructor called");
        }
        system_monitor_.stop_monitoring();
        if (session_log_.is_open()) {
            session_log_.close();
        }
    }
    
    /**
     * Start a new trading session
     */
    bool start_session() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (session_active_) {
            printf("⚠️ Session already active\n");
            return false;
        }
        
        // Initialize session
        session_start_ = std::chrono::steady_clock::now();
        last_status_check_ = session_start_;
        
        current_session_.start_time = session_start_;
        current_session_.starting_balance = position_tracker_->get_total_equity();
        current_session_.session_pnl = 0.0;
        current_session_.max_profit = 0.0;
        current_session_.max_drawdown = 0.0;
        current_session_.total_trades = 0;
        current_session_.winning_trades = 0;
        current_session_.losing_trades = 0;
        current_session_.total_fees = 0.0;
        current_session_.gross_profit = 0.0;
        current_session_.gross_loss = 0.0;
        current_session_.emergency_stop = false;
        
        session_active_ = true;
        trading_paused_ = false;
        emergency_stop_triggered_ = false;
        
        printf("🚀 Trading session started\n");
        printf("   Start time: %s", get_current_time_string().c_str());
        printf("   Starting balance: $%.2f\n", current_session_.starting_balance);
        printf("   Session will auto-stop after %.1f hours\n", config_.max_session_hours);
        
        return true;
    }
    
    /**
     * End the current trading session
     */
    void end_session(const std::string& reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!session_active_) return;
        
        current_session_.end_time = std::chrono::steady_clock::now();
        current_session_.ending_balance = position_tracker_->get_total_equity();
        current_session_.session_pnl = current_session_.ending_balance - current_session_.starting_balance;
        current_session_.stop_reason = reason;
        
        session_active_ = false;
        
        // Calculate session duration
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(
            current_session_.end_time - current_session_.start_time);
        
        printf("\n🏁 Trading session ended: %s\n", reason.c_str());
        printf("   Duration: %ld minutes (%.1f hours)\n", 
               duration.count(), duration.count() / 60.0);
        printf("   Session P&L: $%.2f\n", current_session_.session_pnl);
        printf("   Total trades: %d\n", current_session_.total_trades);
        
        if (current_session_.total_trades > 0) {
            double win_rate = (double)current_session_.winning_trades / current_session_.total_trades * 100;
            printf("   Win rate: %.1f%%\n", win_rate);
            printf("   Max profit: $%.2f\n", current_session_.max_profit);
            printf("   Max drawdown: $%.2f\n", current_session_.max_drawdown);
        }
        
        // Log session summary
        if (config_.enable_session_logging) {
            log_session_summary();
        }
    }
    
    /**
     * Check if session should continue (main loop call)
     */
    bool should_continue_session() {
        if (!session_active_) return false;
        if (emergency_stop_triggered_) return false;
        
        auto now = std::chrono::steady_clock::now();
        
        // Only check conditions every 10 seconds to avoid excessive system calls
        if (now - last_status_check_ < std::chrono::seconds(10)) {
            return !trading_paused_;
        }
        
        last_status_check_ = now;
        
        // 1. Check session time limit
        auto elapsed = std::chrono::duration<double, std::ratio<3600>>(now - session_start_);
        if (elapsed.count() >= config_.max_session_hours) {
            end_session("Session time limit reached");
            return false;
        }
        
        // 2. Check daily loss limit
        double current_pnl = position_tracker_->get_total_equity() - current_session_.starting_balance;
        if (config_.enable_emergency_stops && current_pnl <= -config_.daily_loss_limit) {
            current_session_.emergency_stop = true;
            emergency_stop_triggered_ = true;
            end_session("Daily loss limit exceeded");
            return false;
        }
        
        // 3. Check profit target (if enabled)
        if (config_.enable_profit_target && current_pnl >= config_.daily_profit_target) {
            end_session("Daily profit target reached");
            return false;
        }
        
        // 4. Check maximum drawdown
        double equity = position_tracker_->get_total_equity();
        double drawdown_pct = (current_session_.starting_balance - equity) / current_session_.starting_balance * 100;
        if (config_.enable_emergency_stops && drawdown_pct >= config_.max_drawdown_percent) {
            current_session_.emergency_stop = true;
            emergency_stop_triggered_ = true;
            end_session("Maximum drawdown exceeded");
            return false;
        }
        
        // 5. Check battery level
        if (config_.enable_battery_monitoring) {
            double battery = system_monitor_.get_battery_level();
            bool on_battery = system_monitor_.is_on_battery();
            
            if (on_battery && battery < config_.battery_pause_threshold) {
                if (!trading_paused_) {
                    trading_paused_ = true;
                    printf("🔋 Trading paused: Battery level %.1f%% (threshold: %.1f%%)\n", 
                           battery, config_.battery_pause_threshold);
                    printf("   Connect to power to resume trading\n");
                }
                return false; // Pause trading but don't end session
            } else if (trading_paused_ && (!on_battery || battery >= config_.battery_pause_threshold + 5)) {
                // Resume trading with 5% hysteresis
                trading_paused_ = false;
                printf("🔋 Trading resumed: Battery level %.1f%%\n", battery);
            }
        }
        
        return !trading_paused_;
    }
    
    /**
     * Update session statistics after a trade
     */
    void record_trade(double pnl, double fees = 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!session_active_) return;
        
        current_session_.total_trades++;
        current_session_.total_fees += fees;
        
        if (pnl > 0) {
            current_session_.winning_trades++;
            current_session_.gross_profit += pnl;
            current_session_.max_profit = std::max(current_session_.max_profit, 
                                                   current_session_.session_pnl + pnl);
        } else {
            current_session_.losing_trades++;
            current_session_.gross_loss += std::abs(pnl);
        }
        
        // Update drawdown
        double current_equity = position_tracker_->get_total_equity();
        double drawdown = current_session_.starting_balance - current_equity;
        current_session_.max_drawdown = std::max(current_session_.max_drawdown, drawdown);
    }
    
    /**
     * Get current session status
     */
    void print_session_status() const {
        if (!session_active_) {
            printf("📊 No active session\n");
            return;
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - session_start_);
        double elapsed_hours = elapsed.count() / 60.0;
        double remaining_hours = config_.max_session_hours - elapsed_hours;
        
        double current_pnl = position_tracker_->get_total_equity() - current_session_.starting_balance;
        
        printf("\n📊 SESSION STATUS:\n");
        printf("   Runtime: %.1f/%.1f hours (%.1f hours remaining)\n", 
               elapsed_hours, config_.max_session_hours, remaining_hours);
        printf("   Session P&L: $%.2f", current_pnl);
        
        if (current_pnl >= 0) {
            printf(" 📈\n");
        } else {
            printf(" 📉 (Loss limit: $%.2f)\n", config_.daily_loss_limit);
        }
        
        printf("   Trades: %d (W:%d L:%d)\n", 
               current_session_.total_trades, current_session_.winning_trades, current_session_.losing_trades);
        
        if (config_.enable_battery_monitoring) {
            double battery = system_monitor_.get_battery_level();
            printf("   Battery: %.1f%% %s\n", battery, 
                   system_monitor_.is_on_battery() ? "🔋" : "🔌");
        }
        
        if (trading_paused_) {
            printf("   Status: ⏸️  PAUSED\n");
        } else {
            printf("   Status: ✅ ACTIVE\n");
        }
        
        printf("   Emergency stops: %s\n", config_.enable_emergency_stops ? "ENABLED" : "DISABLED");
    }
    
    // Accessors
    bool is_session_active() const { return session_active_; }
    bool is_trading_paused() const { return trading_paused_; }
    bool is_emergency_stop() const { return emergency_stop_triggered_; }
    const SessionStats& get_current_session() const { return current_session_; }
    const SessionConfig& get_config() const { return config_; }
    
private:
    void init_session_logging() {
        // Create CSV header if file doesn't exist
        std::ifstream check_file(config_.session_log_file);
        bool file_exists = check_file.good();
        check_file.close();
        
        session_log_.open(config_.session_log_file, std::ios::app);
        
        if (session_log_.is_open() && !file_exists) {
            session_log_ << "timestamp,start_time,end_time,duration_hours,starting_balance,ending_balance,"
                        << "session_pnl,max_profit,max_drawdown,total_trades,winning_trades,losing_trades,"
                        << "win_rate,gross_profit,gross_loss,total_fees,stop_reason,emergency_stop\n";
        }
    }
    
    void log_session_summary() {
        if (!session_log_.is_open()) return;
        
        auto duration = std::chrono::duration<double, std::ratio<3600>>(
            current_session_.end_time - current_session_.start_time);
        
        double win_rate = current_session_.total_trades > 0 ? 
                         (double)current_session_.winning_trades / current_session_.total_trades * 100 : 0.0;
        
        session_log_ << get_timestamp_string() << ","
                    << get_time_string(current_session_.start_time) << ","
                    << get_time_string(current_session_.end_time) << ","
                    << std::fixed << std::setprecision(2) << duration.count() << ","
                    << current_session_.starting_balance << ","
                    << current_session_.ending_balance << ","
                    << current_session_.session_pnl << ","
                    << current_session_.max_profit << ","
                    << current_session_.max_drawdown << ","
                    << current_session_.total_trades << ","
                    << current_session_.winning_trades << ","
                    << current_session_.losing_trades << ","
                    << std::setprecision(1) << win_rate << ","
                    << std::setprecision(2) << current_session_.gross_profit << ","
                    << current_session_.gross_loss << ","
                    << current_session_.total_fees << ","
                    << current_session_.stop_reason << ","
                    << (current_session_.emergency_stop ? "true" : "false") << "\n";
        
        session_log_.flush();
    }
    
    std::string get_current_time_string() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str() + "\n";
    }
    
    std::string get_timestamp_string() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    std::string get_time_string(const std::chrono::steady_clock::time_point& tp) const {
        // Convert steady_clock time_point to system_clock time_point
        auto steady_now = std::chrono::steady_clock::now();
        auto system_now = std::chrono::system_clock::now();
        auto elapsed = steady_now - tp;
        
        // Cast to the proper duration type for system_clock
        auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            system_now - std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed)
        );
        
        auto time_t = std::chrono::system_clock::to_time_t(system_time);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

} // namespace hft
