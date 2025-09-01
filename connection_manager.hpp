#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <string>
#include "messages.hpp"

namespace hft {

class ConnectionManager {
private:
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
        ERROR
    };
    
    struct ExchangeConnection {
        std::atomic<ConnectionState> state{ConnectionState::DISCONNECTED};
        std::atomic<uint64_t> last_heartbeat{0};
        std::atomic<uint32_t> reconnect_attempts{0};
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> messages_received{0};
        
        std::string websocket_url;
        std::string rest_url;
        uint32_t heartbeat_interval_ms = 30000;
        uint32_t max_reconnect_attempts = 10;
        uint32_t reconnect_delay_ms = 1000;
    };
    
    std::unordered_map<::hft::Venue, ExchangeConnection> connections_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};

public:
    void start() {
        running_ = true;
        monitor_thread_ = std::thread(&ConnectionManager::monitor_connections, this);
    }
    
    void stop() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    bool connect(::hft::Venue venue) {
        auto& conn = connections_[venue];
        conn.state = ConnectionState::CONNECTING;
        
        // Actual connection logic would go here
        // For now, simulate connection
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        conn.state = ConnectionState::CONNECTED;
        conn.last_heartbeat = ::hft::get_timestamp_ns();
        conn.reconnect_attempts = 0;
        
        printf("🔗 Connected to %s\n", venue_name(venue).c_str());
        return true;
    }
    
    void reconnect(::hft::Venue venue) {
        auto& conn = connections_[venue];
        
        if (conn.reconnect_attempts >= conn.max_reconnect_attempts) {
            conn.state = ConnectionState::ERROR;
            printf("❌ Max reconnection attempts reached for %s\n", venue_name(venue).c_str());
            return;
        }
        
        conn.state = ConnectionState::RECONNECTING;
        conn.reconnect_attempts++;
        
        // Exponential backoff
        uint32_t delay = conn.reconnect_delay_ms * (1 << std::min(conn.reconnect_attempts.load(), 5u));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        
        if (connect(venue)) {
            printf("🔄 Reconnected to %s after %u attempts\n", 
                    venue_name(venue).c_str(), conn.reconnect_attempts.load());
        }
    }
    
    bool is_connected(::hft::Venue venue) const {
        auto it = connections_.find(venue);
        if (it != connections_.end()) {
            return it->second.state.load() == ConnectionState::CONNECTED;
        }
        return false;
    }
    
    void update_heartbeat(::hft::Venue venue) {
        auto it = connections_.find(venue);
        if (it != connections_.end()) {
            it->second.last_heartbeat = ::hft::get_timestamp_ns();
        }
    }
    
    void increment_message_count(::hft::Venue venue, bool sent) {
        auto it = connections_.find(venue);
        if (it != connections_.end()) {
            if (sent) {
                it->second.messages_sent++;
            } else {
                it->second.messages_received++;
            }
        }
    }
    
    struct ConnectionStats {
        ConnectionState state;
        uint64_t last_heartbeat;
        uint32_t reconnect_attempts;
        uint64_t messages_sent;
        uint64_t messages_received;
    };
    
    ConnectionStats get_stats(::hft::Venue venue) const {
        auto it = connections_.find(venue);
        if (it != connections_.end()) {
            const auto& conn = it->second;
            return {
                conn.state.load(),
                conn.last_heartbeat.load(),
                conn.reconnect_attempts.load(),
                conn.messages_sent.load(),
                conn.messages_received.load()
            };
        }
        return {};
    }
    
    void print_status() const {
        printf("\n🌐 === CONNECTION MANAGER STATUS ===\n");
        for (const auto& [venue, conn] : connections_) {
            const char* state_str = connection_state_string(conn.state.load());
            printf("  %s: %s (Msgs: %llu/%llu, Reconnects: %u)\n",
                   venue_name(venue).c_str(),
                   state_str,
                   static_cast<unsigned long long>(conn.messages_sent.load()),
                   static_cast<unsigned long long>(conn.messages_received.load()),
                   conn.reconnect_attempts.load());
        }
        printf("==================================\n\n");
    }

private:
    void monitor_connections() {
        while (running_) {
            uint64_t current_time = ::hft::get_timestamp_ns();
            
            for (auto& [venue, conn] : connections_) {
                if (conn.state == ConnectionState::CONNECTED) {
                    uint64_t time_since_heartbeat = 
                        (current_time - conn.last_heartbeat) / 1000000; // Convert to ms
                    
                    if (time_since_heartbeat > conn.heartbeat_interval_ms * 2) {
                        printf("⚠️ Heartbeat timeout for %s\n", venue_name(venue).c_str());
                        reconnect(venue);
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    std::string venue_name(::hft::Venue venue) const {
        switch (venue) {
            case ::hft::Venue::BINANCE: return "Binance";
            case ::hft::Venue::COINBASE: return "Coinbase";
            case ::hft::Venue::CME: return "CME";
            case ::hft::Venue::NYSE: return "NYSE";
            case ::hft::Venue::NASDAQ: return "NASDAQ";
            case ::hft::Venue::EUREX: return "Eurex";
            case ::hft::Venue::DERIBIT: return "Deribit";
            case ::hft::Venue::OKX: return "OKX";
            case ::hft::Venue::BATS: return "BATS";
            case ::hft::Venue::ICE: return "ICE";
            default: return "Unknown";
        }
    }
    
    const char* connection_state_string(ConnectionState state) const {
        switch (state) {
            case ConnectionState::DISCONNECTED: return "DISCONNECTED";
            case ConnectionState::CONNECTING: return "CONNECTING";
            case ConnectionState::CONNECTED: return "CONNECTED";
            case ConnectionState::RECONNECTING: return "RECONNECTING";
            case ConnectionState::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

} // namespace hft
