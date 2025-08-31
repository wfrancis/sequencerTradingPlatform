#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <cstdio>
#include <sys/stat.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#elif __linux__
#include <sys/statfs.h>
#include <unistd.h>
#endif

namespace risk {

/**
 * System Performance Metrics
 */
struct SystemPerformanceMetrics {
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    double disk_usage_percent = 0.0;
    double network_latency_us = 0.0;
    uint64_t bytes_sent_per_sec = 0;
    uint64_t bytes_received_per_sec = 0;
    uint32_t thread_count = 0;
    uint32_t file_descriptor_count = 0;
    uint64_t last_update_ns = 0;
};

/**
 * Exchange Connectivity Status
 */
struct ExchangeConnectivityStatus {
    std::string exchange_name;
    bool is_connected = false;
    uint64_t last_heartbeat_ns = 0;
    uint64_t connection_uptime_ms = 0;
    uint32_t reconnection_attempts = 0;
    double latency_ms = 0.0;
    uint32_t messages_per_second = 0;
    bool order_entry_available = false;
    bool market_data_available = false;
    std::string last_error;
};

/**
 * Application Health Metrics
 */
struct ApplicationHealthMetrics {
    uint32_t order_processing_queue_depth = 0;
    uint32_t market_data_queue_depth = 0;
    uint32_t risk_event_queue_depth = 0;
    double avg_order_processing_latency_us = 0.0;
    double avg_market_data_latency_us = 0.0;
    uint32_t failed_orders_last_minute = 0;
    uint32_t risk_violations_last_hour = 0;
    bool sequencer_healthy = true;
    bool position_tracker_healthy = true;
    bool risk_manager_healthy = true;
    uint64_t last_health_check_ns = 0;
};

/**
 * System Health Monitor
 * 
 * Monitors system performance, connectivity, and application health
 * to detect operational risks before they impact trading
 */
class SystemHealthMonitor {
private:
    SystemPerformanceMetrics performance_metrics_;
    std::unordered_map<std::string, ExchangeConnectivityStatus> connectivity_status_;
    ApplicationHealthMetrics app_health_metrics_;
    
    std::atomic<bool> monitoring_active_{true};
    std::atomic<bool> critical_alert_active_{false};
    std::thread monitoring_thread_;
    
    mutable std::mutex mutex_;
    std::atomic<uint32_t> health_check_interval_ms_{1000}; // 1 second default
    
    // Alert thresholds
    struct AlertThresholds {
        double cpu_critical = 95.0;           // 95% CPU usage
        double cpu_warning = 80.0;            // 80% CPU usage
        double memory_critical = 90.0;        // 90% memory usage
        double memory_warning = 75.0;         // 75% memory usage
        double disk_critical = 95.0;          // 95% disk usage
        uint32_t queue_depth_critical = 1000; // 1000 messages in queue
        double latency_critical_us = 10000.0; // 10ms latency
        uint32_t max_reconnection_attempts = 5;
        uint64_t max_heartbeat_gap_ms = 5000; // 5 seconds without heartbeat
    } thresholds_;
    
    // Performance history for trend analysis
    static constexpr size_t HISTORY_SIZE = 300; // 5 minutes of data at 1s intervals
    std::vector<double> cpu_history_;
    std::vector<double> memory_history_;
    std::vector<double> latency_history_;
    
    void monitoring_loop() {
        while (monitoring_active_) {
            update_system_metrics();
            update_connectivity_status();
            update_application_health();
            check_alert_conditions();
            
            std::this_thread::sleep_for(
                std::chrono::milliseconds(health_check_interval_ms_.load()));
        }
    }
    
    void update_system_metrics() {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // Update CPU usage
        performance_metrics_.cpu_usage_percent = get_cpu_usage();
        
        // Update memory usage
        performance_metrics_.memory_usage_percent = get_memory_usage();
        
        // Update disk usage
        performance_metrics_.disk_usage_percent = get_disk_usage();
        
        // Update network statistics
        update_network_stats();
        
        // Update thread and file descriptor counts
        performance_metrics_.thread_count = get_thread_count();
        performance_metrics_.file_descriptor_count = get_fd_count();
        
        performance_metrics_.last_update_ns = current_time;
        
        // Update history for trend analysis
        std::lock_guard<std::mutex> lock(mutex_);
        cpu_history_.push_back(performance_metrics_.cpu_usage_percent);
        memory_history_.push_back(performance_metrics_.memory_usage_percent);
        
        if (cpu_history_.size() > HISTORY_SIZE) {
            cpu_history_.erase(cpu_history_.begin());
            memory_history_.erase(memory_history_.begin());
        }
    }
    
    double get_cpu_usage() {
#ifdef __APPLE__
        host_cpu_load_info_data_t cpu_info;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                           (host_info_t)&cpu_info, &count) == KERN_SUCCESS) {
            
            natural_t total_ticks = cpu_info.cpu_ticks[CPU_STATE_USER] +
                                   cpu_info.cpu_ticks[CPU_STATE_SYSTEM] +
                                   cpu_info.cpu_ticks[CPU_STATE_IDLE] +
                                   cpu_info.cpu_ticks[CPU_STATE_NICE];
            
            natural_t idle_ticks = cpu_info.cpu_ticks[CPU_STATE_IDLE];
            
            if (total_ticks > 0) {
                return (1.0 - (double)idle_ticks / total_ticks) * 100.0;
            }
        }
#elif __linux__
        std::ifstream stat_file("/proc/stat");
        if (stat_file.is_open()) {
            std::string line;
            std::getline(stat_file, line);
            // Parse /proc/stat to get CPU usage
            // Implementation would parse the first line for CPU totals
        }
#endif
        return 0.0; // Fallback if unable to get CPU usage
    }
    
    double get_memory_usage() {
#ifdef __APPLE__
        vm_size_t page_size;
        vm_statistics64_data_t vm_stat;
        mach_msg_type_number_t count = sizeof(vm_stat) / sizeof(natural_t);
        
        if (host_page_size(mach_host_self(), &page_size) == KERN_SUCCESS &&
            host_statistics64(mach_host_self(), HOST_VM_INFO,
                             (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
            
            uint64_t total_memory = (vm_stat.free_count + vm_stat.active_count +
                                   vm_stat.inactive_count + vm_stat.wire_count) * page_size;
            uint64_t used_memory = (vm_stat.active_count + vm_stat.wire_count) * page_size;
            
            if (total_memory > 0) {
                return (double)used_memory / total_memory * 100.0;
            }
        }
#elif __linux__
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open()) {
            // Parse /proc/meminfo to get memory statistics
            // Implementation would parse MemTotal and MemAvailable
        }
#endif
        return 0.0; // Fallback
    }
    
    double get_disk_usage() {
        struct stat statbuf;
        if (stat(".", &statbuf) == 0) {
#ifdef __APPLE__
            struct statfs fs_stat;
            if (statfs(".", &fs_stat) >= 0) {
                uint64_t total_blocks = fs_stat.f_blocks;
                uint64_t free_blocks = fs_stat.f_bavail;
                uint64_t used_blocks = total_blocks - free_blocks;
                
                if (total_blocks > 0) {
                    return (double)used_blocks / total_blocks * 100.0;
                }
            }
#elif __linux__
            struct statfs fs_stat;
            if (statfs(".", &fs_stat) >= 0) {
                uint64_t total_blocks = fs_stat.f_blocks;
                uint64_t free_blocks = fs_stat.f_bavail;
                uint64_t used_blocks = total_blocks - free_blocks;
                
                if (total_blocks > 0) {
                    return (double)used_blocks / total_blocks * 100.0;
                }
            }
#endif
        }
        return 0.0;
    }
    
    void update_network_stats() {
        // Simplified network statistics
        // In production, would query network interface statistics
        performance_metrics_.network_latency_us = measure_network_latency();
        performance_metrics_.bytes_sent_per_sec = 0; // Would track actual network I/O
        performance_metrics_.bytes_received_per_sec = 0;
    }
    
    double measure_network_latency() {
        // Simplified latency measurement
        // In production, would ping exchanges or measure actual round-trip times
        auto start = std::chrono::high_resolution_clock::now();
        
        // Simulate network operation
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        return static_cast<double>(duration.count());
    }
    
    uint32_t get_thread_count() {
        // Platform-specific thread counting
#ifdef __APPLE__
        return std::thread::hardware_concurrency(); // Simplified
#elif __linux__
        std::ifstream status("/proc/self/status");
        if (status.is_open()) {
            std::string line;
            while (std::getline(status, line)) {
                if (line.find("Threads:") == 0) {
                    return std::stoi(line.substr(8));
                }
            }
        }
#endif
        return 0;
    }
    
    uint32_t get_fd_count() {
        // Platform-specific file descriptor counting
#ifdef __linux__
        // Count files in /proc/self/fd
        // Implementation would list directory contents
#endif
        return 0; // Simplified
    }
    
    void update_connectivity_status() {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // Update connectivity for known exchanges
        for (auto& [exchange, status] : connectivity_status_) {
            // Check if heartbeat is recent
            uint64_t heartbeat_gap = (current_time - status.last_heartbeat_ns) / 1000000; // Convert to ms
            
            if (heartbeat_gap > thresholds_.max_heartbeat_gap_ms) {
                status.is_connected = false;
                status.order_entry_available = false;
                status.market_data_available = false;
                status.last_error = "Heartbeat timeout";
            }
        }
    }
    
    void update_application_health() {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        // These would be updated by the actual application components
        // For now, we'll simulate health checks
        app_health_metrics_.sequencer_healthy = true;
        app_health_metrics_.position_tracker_healthy = true;
        app_health_metrics_.risk_manager_healthy = true;
        
        app_health_metrics_.last_health_check_ns = current_time;
    }
    
    void check_alert_conditions() {
        bool critical_condition = false;
        std::vector<std::string> alerts;
        
        // Check CPU usage
        if (performance_metrics_.cpu_usage_percent > thresholds_.cpu_critical) {
            critical_condition = true;
            alerts.push_back("Critical CPU usage: " + 
                           std::to_string(performance_metrics_.cpu_usage_percent) + "%");
        } else if (performance_metrics_.cpu_usage_percent > thresholds_.cpu_warning) {
            alerts.push_back("High CPU usage: " + 
                           std::to_string(performance_metrics_.cpu_usage_percent) + "%");
        }
        
        // Check memory usage
        if (performance_metrics_.memory_usage_percent > thresholds_.memory_critical) {
            critical_condition = true;
            alerts.push_back("Critical memory usage: " + 
                           std::to_string(performance_metrics_.memory_usage_percent) + "%");
        }
        
        // Check exchange connectivity
        for (const auto& [exchange, status] : connectivity_status_) {
            if (!status.is_connected) {
                critical_condition = true;
                alerts.push_back("Exchange disconnected: " + exchange);
            }
        }
        
        // Check queue depths
        if (app_health_metrics_.order_processing_queue_depth > thresholds_.queue_depth_critical) {
            critical_condition = true;
            alerts.push_back("Critical order queue depth: " + 
                           std::to_string(app_health_metrics_.order_processing_queue_depth));
        }
        
        // Update critical alert state
        bool previous_critical = critical_alert_active_.exchange(critical_condition);
        
        if (critical_condition && !previous_critical) {
            printf("🚨 CRITICAL SYSTEM ALERT ACTIVATED\n");
            for (const auto& alert : alerts) {
                printf("   ⚠️ %s\n", alert.c_str());
            }
            log_critical_alert(alerts);
        } else if (!critical_condition && previous_critical) {
            printf("✅ Critical system alerts cleared\n");
        }
        
        // Log all alerts
        if (!alerts.empty()) {
            log_health_alerts(alerts);
        }
    }
    
    void log_critical_alert(const std::vector<std::string>& alerts) {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        std::ofstream alert_file("critical_system_alerts.log", std::ios::app);
        if (alert_file.is_open()) {
            alert_file << current_time << ",CRITICAL_ALERT_START\n";
            for (const auto& alert : alerts) {
                alert_file << current_time << "," << alert << "\n";
            }
        }
    }
    
    void log_health_alerts(const std::vector<std::string>& alerts) {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        std::ofstream health_file("system_health.log", std::ios::app);
        if (health_file.is_open()) {
            for (const auto& alert : alerts) {
                health_file << current_time << "," << alert << "\n";
            }
        }
    }

public:
    SystemHealthMonitor() {
        // Initialize known exchanges
        connectivity_status_["COINBASE"] = ExchangeConnectivityStatus{"COINBASE"};
        connectivity_status_["BINANCE"] = ExchangeConnectivityStatus{"BINANCE"};
        
        printf("🔍 System Health Monitor initialized\n");
        printf("   Monitoring interval: %dms\n", health_check_interval_ms_.load());
        printf("   CPU critical threshold: %.1f%%\n", thresholds_.cpu_critical);
        printf("   Memory critical threshold: %.1f%%\n", thresholds_.memory_critical);
    }
    
    ~SystemHealthMonitor() {
        stop_monitoring();
    }
    
    void start_monitoring() {
        if (!monitoring_active_) {
            monitoring_active_ = true;
            monitoring_thread_ = std::thread(&SystemHealthMonitor::monitoring_loop, this);
            printf("✅ System health monitoring started\n");
        }
    }
    
    void stop_monitoring() {
        if (monitoring_active_) {
            monitoring_active_ = false;
            if (monitoring_thread_.joinable()) {
                monitoring_thread_.join();
            }
            printf("🛑 System health monitoring stopped\n");
        }
    }
    
    // Update exchange connectivity
    void update_exchange_heartbeat(const std::string& exchange) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connectivity_status_.find(exchange);
        if (it != connectivity_status_.end()) {
            it->second.last_heartbeat_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            it->second.is_connected = true;
            it->second.order_entry_available = true;
            it->second.market_data_available = true;
        }
    }
    
    void update_exchange_connectivity(const std::string& exchange, bool connected,
                                    bool order_entry, bool market_data,
                                    const std::string& error = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connectivity_status_.find(exchange);
        if (it != connectivity_status_.end()) {
            it->second.is_connected = connected;
            it->second.order_entry_available = order_entry;
            it->second.market_data_available = market_data;
            if (!error.empty()) {
                it->second.last_error = error;
            }
        }
    }
    
    // Update application metrics
    void update_queue_depth(const std::string& queue_type, uint32_t depth) {
        if (queue_type == "order_processing") {
            app_health_metrics_.order_processing_queue_depth = depth;
        } else if (queue_type == "market_data") {
            app_health_metrics_.market_data_queue_depth = depth;
        } else if (queue_type == "risk_events") {
            app_health_metrics_.risk_event_queue_depth = depth;
        }
    }
    
    void update_processing_latency(const std::string& process_type, double latency_us) {
        if (process_type == "order_processing") {
            app_health_metrics_.avg_order_processing_latency_us = latency_us;
        } else if (process_type == "market_data") {
            app_health_metrics_.avg_market_data_latency_us = latency_us;
        }
    }
    
    void report_component_health(const std::string& component, bool healthy) {
        if (component == "sequencer") {
            app_health_metrics_.sequencer_healthy = healthy;
        } else if (component == "position_tracker") {
            app_health_metrics_.position_tracker_healthy = healthy;
        } else if (component == "risk_manager") {
            app_health_metrics_.risk_manager_healthy = healthy;
        }
    }
    
    // Accessors
    SystemPerformanceMetrics get_performance_metrics() const {
        return performance_metrics_;
    }
    
    ExchangeConnectivityStatus get_exchange_status(const std::string& exchange) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connectivity_status_.find(exchange);
        return (it != connectivity_status_.end()) ? it->second : ExchangeConnectivityStatus{};
    }
    
    ApplicationHealthMetrics get_application_health() const {
        return app_health_metrics_;
    }
    
    bool is_system_healthy() const {
        return !critical_alert_active_ &&
               performance_metrics_.cpu_usage_percent < thresholds_.cpu_critical &&
               performance_metrics_.memory_usage_percent < thresholds_.memory_critical;
    }
    
    bool is_critical_alert_active() const {
        return critical_alert_active_;
    }
    
    void print_health_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        printf("\n🔍 === SYSTEM HEALTH SUMMARY ===\n");
        printf("🚨 Critical Alerts: %s\n", critical_alert_active_ ? "ACTIVE" : "NONE");
        
        printf("\n💻 System Performance:\n");
        printf("  CPU Usage: %.1f%%\n", performance_metrics_.cpu_usage_percent);
        printf("  Memory Usage: %.1f%%\n", performance_metrics_.memory_usage_percent);
        printf("  Disk Usage: %.1f%%\n", performance_metrics_.disk_usage_percent);
        printf("  Network Latency: %.1fμs\n", performance_metrics_.network_latency_us);
        printf("  Thread Count: %d\n", performance_metrics_.thread_count);
        
        printf("\n🏛️ Exchange Connectivity:\n");
        for (const auto& [exchange, status] : connectivity_status_) {
            printf("  %s: %s (Orders: %s, Data: %s)\n",
                   exchange.c_str(),
                   status.is_connected ? "CONNECTED" : "DISCONNECTED",
                   status.order_entry_available ? "OK" : "DOWN",
                   status.market_data_available ? "OK" : "DOWN");
        }
        
        printf("\n📱 Application Health:\n");
        printf("  Order Queue: %d messages\n", app_health_metrics_.order_processing_queue_depth);
        printf("  Market Data Queue: %d messages\n", app_health_metrics_.market_data_queue_depth);
        printf("  Order Latency: %.1fμs\n", app_health_metrics_.avg_order_processing_latency_us);
        printf("  Sequencer: %s\n", app_health_metrics_.sequencer_healthy ? "HEALTHY" : "UNHEALTHY");
        printf("  Position Tracker: %s\n", app_health_metrics_.position_tracker_healthy ? "HEALTHY" : "UNHEALTHY");
        printf("  Risk Manager: %s\n", app_health_metrics_.risk_manager_healthy ? "HEALTHY" : "UNHEALTHY");
        
        printf("=====================================\n\n");
    }
};

} // namespace risk
