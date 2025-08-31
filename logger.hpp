#pragma once

#include <string>
#include <memory>
#include <fstream>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <condition_variable>

namespace hft {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

struct LogEntry {
    uint64_t timestamp_ns;
    LogLevel level;
    std::string category;
    std::string message;
    std::thread::id thread_id;
};

class Logger {
private:
    struct LoggerConfig {
        std::string system_log_file = "logs/system.log";
        std::string trading_log_file = "logs/trading.log"; 
        std::string risk_log_file = "logs/risk.log";
        size_t max_file_size_mb = 100;
        size_t max_files = 10;
        LogLevel min_level = LogLevel::INFO;
        bool enable_console = true;
        size_t async_buffer_size = 8192;
    };

    LoggerConfig config_;
    
    // Async logging infrastructure
    std::queue<LogEntry> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> shutdown_{false};
    std::thread logging_thread_;
    
    // File handles
    std::ofstream system_log_;
    std::ofstream trading_log_;
    std::ofstream risk_log_;
    std::mutex file_mutex_;
    
    // File rotation tracking
    std::atomic<size_t> system_log_size_{0};
    std::atomic<size_t> trading_log_size_{0};
    std::atomic<size_t> risk_log_size_{0};
    
    Logger() = default;
    
    void logging_worker() {
        while (!shutdown_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !log_queue_.empty() || shutdown_.load(); 
            });
            
            // Process all queued entries
            while (!log_queue_.empty()) {
                LogEntry entry = log_queue_.front();
                log_queue_.pop();
                lock.unlock();
                
                write_log_entry(entry);
                
                lock.lock();
            }
        }
        
        // Flush remaining entries on shutdown
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!log_queue_.empty()) {
            write_log_entry(log_queue_.front());
            log_queue_.pop();
        }
    }
    
    void write_log_entry(const LogEntry& entry) {
        if (entry.level < config_.min_level) return;
        
        std::string formatted = format_log_entry(entry);
        
        // Console output
        if (config_.enable_console) {
            std::cout << formatted << std::endl;
        }
        
        // File output
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        if (entry.category == "SYSTEM" || entry.category == "ERROR") {
            write_to_file(system_log_, formatted, system_log_size_);
        } else if (entry.category == "TRADING" || entry.category == "SIGNAL") {
            write_to_file(trading_log_, formatted, trading_log_size_);
        } else if (entry.category == "RISK") {
            write_to_file(risk_log_, formatted, risk_log_size_);
        } else {
            write_to_file(system_log_, formatted, system_log_size_);
        }
    }
    
    void write_to_file(std::ofstream& file, const std::string& message, 
                       std::atomic<size_t>& file_size) {
        if (!file.is_open()) return;
        
        file << message << std::endl;
        file_size += message.length() + 1;
        
        // Check for rotation
        if (file_size.load() > config_.max_file_size_mb * 1024 * 1024) {
            // File rotation would be implemented here
            file_size = 0;
        }
    }
    
    std::string format_log_entry(const LogEntry& entry) {
        std::stringstream ss;
        
        // Timestamp
        auto time_point = std::chrono::system_clock::time_point(
            std::chrono::nanoseconds(entry.timestamp_ns));
        auto time_t = std::chrono::system_clock::to_time_t(time_point);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::nanoseconds(entry.timestamp_ns) % 1000000000).count();
        
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms;
        
        // Log level
        const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR", "CRIT"};
        ss << " [" << level_str[static_cast<int>(entry.level)] << "]";
        
        // Category
        ss << " [" << entry.category << "]";
        
        // Thread ID
        ss << " [T:" << std::hash<std::thread::id>{}(entry.thread_id) % 10000 << "]";
        
        // Message
        ss << " " << entry.message;
        
        return ss.str();
    }
    
    void ensure_directories() {
        // Create logs directory if it doesn't exist
        std::system("mkdir -p logs");
    }

public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }
    
    void init(const LoggerConfig& config = LoggerConfig{}) {
        config_ = config;
        ensure_directories();
        
        // Open log files
        std::lock_guard<std::mutex> lock(file_mutex_);
        system_log_.open(config_.system_log_file, std::ios::app);
        trading_log_.open(config_.trading_log_file, std::ios::app);
        risk_log_.open(config_.risk_log_file, std::ios::app);
        
        // Start async logging thread
        shutdown_.store(false);
        logging_thread_ = std::thread(&Logger::logging_worker, this);
        
        log(LogLevel::INFO, "SYSTEM", "Logger initialized");
    }
    
    void shutdown() {
        log(LogLevel::INFO, "SYSTEM", "Logger shutting down");
        
        shutdown_.store(true, std::memory_order_release);
        queue_cv_.notify_all();
        
        if (logging_thread_.joinable()) {
            logging_thread_.join();
        }
        
        // Close files
        std::lock_guard<std::mutex> lock(file_mutex_);
        system_log_.close();
        trading_log_.close();
        risk_log_.close();
    }
    
    void log(LogLevel level, const std::string& category, const std::string& message) {
        LogEntry entry{
            .timestamp_ns = static_cast<uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()),
            .level = level,
            .category = category,
            .message = message,
            .thread_id = std::this_thread::get_id()
        };
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        log_queue_.push(entry);
        queue_cv_.notify_one();
    }
    
    // Convenience methods
    template<typename... Args>
    void log_error(const std::string& format, Args&&... args) {
        log(LogLevel::ERROR, "SYSTEM", format_string(format, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    void log_trade(const std::string& format, Args&&... args) {
        log(LogLevel::INFO, "TRADING", format_string(format, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    void log_risk(const std::string& format, Args&&... args) {
        log(LogLevel::WARNING, "RISK", format_string(format, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    void log_info(const std::string& format, Args&&... args) {
        log(LogLevel::INFO, "SYSTEM", format_string(format, std::forward<Args>(args)...));
    }
    
    template<typename... Args>
    void log_debug(const std::string& format, Args&&... args) {
        log(LogLevel::DEBUG, "SYSTEM", format_string(format, std::forward<Args>(args)...));
    }

private:
    template<typename... Args>
    std::string format_string(const std::string& format, Args&&... args) {
        // Simple string formatting - in production would use fmt library
        std::stringstream ss;
        format_impl(ss, format, std::forward<Args>(args)...);
        return ss.str();
    }
    
    void format_impl(std::stringstream& ss, const std::string& format) {
        ss << format;
    }
    
    template<typename T, typename... Args>
    void format_impl(std::stringstream& ss, const std::string& format, T&& value, Args&&... args) {
        size_t pos = format.find("{}");
        if (pos != std::string::npos) {
            ss << format.substr(0, pos) << std::forward<T>(value);
            format_impl(ss, format.substr(pos + 2), std::forward<Args>(args)...);
        } else {
            ss << format;
        }
    }
};

// Global logging macros
#define LOG_ERROR(...) hft::Logger::instance().log_error(__VA_ARGS__)
#define LOG_TRADE(...) hft::Logger::instance().log_trade(__VA_ARGS__)
#define LOG_RISK(...) hft::Logger::instance().log_risk(__VA_ARGS__)
#define LOG_INFO(...) hft::Logger::instance().log_info(__VA_ARGS__)
#define LOG_DEBUG(...) hft::Logger::instance().log_debug(__VA_ARGS__)

} // namespace hft
