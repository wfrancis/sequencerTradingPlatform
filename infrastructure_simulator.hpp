#pragma once

#include <vector>
#include <unordered_map>
#include <random>
#include <queue>
#include <functional>
#include <string>
#include <atomic>
#include <chrono>
#include "messages.hpp"

namespace hft::simulator {

enum class WeatherCondition : uint8_t {
    CLEAR = 0,
    LIGHT_RAIN = 1,
    HEAVY_RAIN = 2,
    SNOW = 3,
    FOG = 4,
    STORM = 5
};

enum class ConnectionType : uint8_t {
    FIBER = 0,
    MICROWAVE = 1,
    SATELLITE = 2,
    CROSS_CONNECT = 3
};

class InfrastructureSimulator {
public:
    struct NetworkNode {
        std::string name;
        std::string location;
        ConnectionType type;
        double base_latency_us;
        double reliability = 0.999;  // 99.9% uptime
        bool is_active = true;
        uint64_t last_failure_time = 0;
        
        NetworkNode(const std::string& n, const std::string& loc, ConnectionType t, double lat)
            : name(n), location(loc), type(t), base_latency_us(lat) {}
    };
    
    struct NetworkPath {
        std::vector<NetworkNode> nodes;
        double base_latency_us;
        double jitter_us;
        double bandwidth_mbps;
        double congestion_factor = 1.0;
        uint32_t mtu = 1500;  // Maximum transmission unit
        
        double calculate_total_latency() const {
            double total = base_latency_us;
            for (const auto& node : nodes) {
                if (node.is_active) {
                    total += node.base_latency_us;
                } else {
                    total += node.base_latency_us * 10; // 10x penalty for degraded nodes
                }
            }
            return total * congestion_factor;
        }
        
        bool is_path_available() const {
            return std::all_of(nodes.begin(), nodes.end(),
                [](const NetworkNode& node) { return node.is_active; });
        }
    };
    
    struct FailureScenario {
        enum Type {
            PACKET_LOSS,
            CONNECTION_DROP,
            GATEWAY_OVERLOAD,
            RATE_LIMIT,
            AUTHENTICATION_FAILURE,
            MARKET_DATA_GAP,
            ORDER_REJECT_STORM,
            PARTIAL_OUTAGE,
            DNS_FAILURE,
            TIME_SYNC_DRIFT,
            POWER_OUTAGE,
            DDOS_ATTACK
        };
        Type type;
        uint64_t start_time;
        uint64_t duration;
        double severity;  // 0.0 to 1.0
        std::string affected_component;
        bool is_active = false;
        
        FailureScenario(Type t, uint64_t start, uint64_t dur, double sev)
            : type(t), start_time(start), duration(dur), severity(sev) {}
    };
    
    struct Message {
        uint64_t id;
        std::vector<uint8_t> payload;
        uint64_t timestamp;
        std::string source;
        std::string destination;
        uint32_t sequence_number;
        bool is_duplicate = false;
        uint32_t attempts = 0;
        
        Message(uint64_t msg_id, std::vector<uint8_t> data, const std::string& src, const std::string& dst)
            : id(msg_id), payload(std::move(data)), timestamp(get_timestamp_ns()), 
              source(src), destination(dst), sequence_number(0) {}
    };
    
private:
    std::mt19937_64 rng_;
    std::unordered_map<std::string, NetworkPath> network_paths_;
    std::queue<FailureScenario> active_failures_;
    std::vector<FailureScenario> scheduled_failures_;
    
    // Network statistics
    struct NetworkStats {
        std::atomic<uint64_t> messages_sent{0};
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> messages_lost{0};
        std::atomic<uint64_t> messages_delayed{0};
        std::atomic<uint64_t> messages_duplicated{0};
        std::atomic<uint64_t> messages_reordered{0};
        std::atomic<double> average_latency_us{0.0};
        std::atomic<uint64_t> bytes_transmitted{0};
        
        void reset() {
            messages_sent = 0;
            messages_received = 0;
            messages_lost = 0;
            messages_delayed = 0;
            messages_duplicated = 0;
            messages_reordered = 0;
            average_latency_us = 0.0;
            bytes_transmitted = 0;
        }
    } network_stats_;
    
    // Message queues for latency simulation
    struct DelayedMessage {
        Message message;
        uint64_t delivery_time;
        std::string path_name;
        
        DelayedMessage(Message msg, uint64_t time, const std::string& path)
            : message(std::move(msg)), delivery_time(time), path_name(path) {}
        
        bool operator>(const DelayedMessage& other) const {
            return delivery_time > other.delivery_time;
        }
    };
    
    std::priority_queue<DelayedMessage, std::vector<DelayedMessage>, std::greater<DelayedMessage>> delayed_messages_;
    
    // Weather impact on microwave links
    WeatherCondition current_weather_ = WeatherCondition::CLEAR;
    std::uniform_real_distribution<double> weather_change_prob_{0.0, 1.0};
    
    // Colocation advantages
    std::unordered_map<std::string, double> colocation_advantages_;
    
    // Rate limiting state
    struct RateLimiter {
        uint64_t messages_per_second = 1000;
        uint64_t current_count = 0;
        uint64_t window_start = 0;
        const uint64_t window_duration_ns = 1000000000ULL; // 1 second
        
        bool is_rate_limited(uint64_t current_time) {
            if (current_time - window_start >= window_duration_ns) {
                window_start = current_time;
                current_count = 0;
            }
            
            if (current_count >= messages_per_second) {
                return true;
            }
            
            current_count++;
            return false;
        }
    };
    
    std::unordered_map<std::string, RateLimiter> rate_limiters_;

public:
    explicit InfrastructureSimulator(uint64_t seed = std::random_device{}())
        : rng_(seed) {
        initialize_default_topology();
    }
    
    void add_network_path(const std::string& name, const NetworkPath& path) {
        network_paths_[name] = path;
    }
    
    void simulate_network_conditions(
        const NetworkPath& path,
        Message& msg
    ) {
        uint64_t current_time = get_timestamp_ns();
        std::string path_name = "default_path";
        
        // Check for active failures affecting this path
        double failure_impact = calculate_failure_impact(path_name);
        
        // Apply packet loss
        double packet_loss_rate = calculate_packet_loss_rate(path, failure_impact);
        if (std::uniform_real_distribution<double>(0, 1)(rng_) < packet_loss_rate) {
            network_stats_.messages_lost.fetch_add(1);
            return; // Message lost
        }
        
        // Apply latency with jitter
        double base_latency = path.calculate_total_latency();
        double jitter = std::normal_distribution<double>(0, path.jitter_us)(rng_);
        double total_latency_us = std::max(0.0, base_latency + jitter);
        
        // Weather impact on microwave links
        total_latency_us *= calculate_weather_impact(path);
        
        // Congestion impact
        total_latency_us *= (1.0 + path.congestion_factor * failure_impact);
        
        // Apply bandwidth constraints
        double transmission_time_us = calculate_transmission_time(msg.payload.size(), path.bandwidth_mbps);
        total_latency_us += transmission_time_us;
        
        // Schedule message delivery
        uint64_t delivery_time = current_time + static_cast<uint64_t>(total_latency_us * 1000);
        
        // Simulate packet reordering
        if (std::uniform_real_distribution<double>(0, 1)(rng_) < 0.0001 * failure_impact) {
            delivery_time += static_cast<uint64_t>(std::exponential_distribution<double>(1000)(rng_)); // Random additional delay
            network_stats_.messages_reordered.fetch_add(1);
        }
        
        // Simulate packet duplication
        if (std::uniform_real_distribution<double>(0, 1)(rng_) < 0.00001 * failure_impact) {
            Message dup_msg = msg;
            dup_msg.is_duplicate = true;
            dup_msg.id = generate_message_id();
            delayed_messages_.emplace(std::move(dup_msg), delivery_time + 1000000, path_name); // 1ms later
            network_stats_.messages_duplicated.fetch_add(1);
        }
        
        delayed_messages_.emplace(std::move(msg), delivery_time, path_name);
        network_stats_.messages_sent.fetch_add(1);
        network_stats_.bytes_transmitted.fetch_add(msg.payload.size());
        
        // Update average latency
        double current_avg = network_stats_.average_latency_us.load();
        double new_avg = (current_avg * 0.99) + (total_latency_us * 0.01); // Exponential moving average
        network_stats_.average_latency_us.store(new_avg);
    }
    
    void inject_failure(const FailureScenario& scenario) {
        FailureScenario active_scenario = scenario;
        active_scenario.start_time = get_timestamp_ns();
        active_scenario.is_active = true;
        
        switch (scenario.type) {
            case FailureScenario::CONNECTION_DROP:
                simulate_connection_drop(scenario);
                break;
            case FailureScenario::GATEWAY_OVERLOAD:
                simulate_gateway_overload(scenario);
                break;
            case FailureScenario::PACKET_LOSS:
                simulate_increased_packet_loss(scenario);
                break;
            case FailureScenario::RATE_LIMIT:
                simulate_rate_limiting(scenario);
                break;
            case FailureScenario::MARKET_DATA_GAP:
                simulate_market_data_gap(scenario);
                break;
            case FailureScenario::DDOS_ATTACK:
                simulate_ddos_attack(scenario);
                break;
            case FailureScenario::TIME_SYNC_DRIFT:
                simulate_time_sync_drift(scenario);
                break;
            default:
                break;
        }
        
        active_failures_.push(active_scenario);
    }
    
    void simulate_colocation_advantage(
        double advantage_us,
        const std::string& advantaged_trader
    ) {
        colocation_advantages_[advantaged_trader] = advantage_us;
    }
    
    void simulate_exchange_outage(
        Venue venue,
        uint64_t duration_ms,
        bool gradual_degradation
    ) {
        FailureScenario outage(FailureScenario::PARTIAL_OUTAGE, 
                              get_timestamp_ns(), 
                              duration_ms * 1000000ULL, 
                              gradual_degradation ? 0.5 : 1.0);
        outage.affected_component = "venue_" + std::to_string(static_cast<int>(venue));
        
        if (gradual_degradation) {
            // Gradually increase failure impact over time
            scheduled_failures_.push_back(outage);
        } else {
            // Immediate full outage
            inject_failure(outage);
        }
    }
    
    // Process delayed messages and return ready ones
    std::vector<Message> process_delayed_messages() {
        std::vector<Message> ready_messages;
        uint64_t current_time = get_timestamp_ns();
        
        while (!delayed_messages_.empty() && delayed_messages_.top().delivery_time <= current_time) {
            auto delayed_msg = delayed_messages_.top();
            delayed_messages_.pop();
            
            // Apply final processing (e.g., rate limiting, additional failures)
            if (!should_drop_message(delayed_msg.message, delayed_msg.path_name)) {
                ready_messages.push_back(std::move(delayed_msg.message));
                network_stats_.messages_received.fetch_add(1);
            } else {
                network_stats_.messages_lost.fetch_add(1);
            }
        }
        
        // Process scheduled failures
        process_scheduled_failures();
        
        // Update active failures
        process_active_failures();
        
        return ready_messages;
    }
    
    // Statistics and monitoring
    struct InfrastructureStats {
        uint64_t messages_sent;
        uint64_t messages_received;
        uint64_t messages_lost;
        double loss_rate;
        double average_latency_us;
        uint64_t bytes_transmitted;
        double throughput_mbps;
        uint32_t active_failures;
        std::unordered_map<std::string, bool> path_status;
    };
    
    InfrastructureStats get_statistics() const {
        InfrastructureStats stats;
        stats.messages_sent = network_stats_.messages_sent.load();
        stats.messages_received = network_stats_.messages_received.load();
        stats.messages_lost = network_stats_.messages_lost.load();
        stats.loss_rate = stats.messages_sent > 0 ? 
            static_cast<double>(stats.messages_lost) / stats.messages_sent : 0.0;
        stats.average_latency_us = network_stats_.average_latency_us.load();
        stats.bytes_transmitted = network_stats_.bytes_transmitted.load();
        stats.active_failures = static_cast<uint32_t>(active_failures_.size());
        
        // Calculate throughput (bytes per second to Mbps)
        double elapsed_seconds = 1.0; // Simplified
        stats.throughput_mbps = (stats.bytes_transmitted * 8.0) / (elapsed_seconds * 1000000.0);
        
        for (const auto& [name, path] : network_paths_) {
            stats.path_status[name] = path.is_path_available();
        }
        
        return stats;
    }
    
    void print_status() const {
        auto stats = get_statistics();
        printf("🌐 Infrastructure Status:\n");
        printf("   Messages: sent=%lu, received=%lu, lost=%lu (%.2f%%)\n", 
               stats.messages_sent, stats.messages_received, 
               stats.messages_lost, stats.loss_rate * 100.0);
        printf("   Average latency: %.1f μs\n", stats.average_latency_us);
        printf("   Throughput: %.1f Mbps\n", stats.throughput_mbps);
        printf("   Active failures: %u\n", stats.active_failures);
        printf("   Network paths: %zu\n", stats.path_status.size());
        
        for (const auto& [path_name, is_available] : stats.path_status) {
            printf("   Path %s: %s\n", path_name.c_str(), is_available ? "UP" : "DOWN");
        }
    }
    
    void set_weather_condition(WeatherCondition weather) {
        current_weather_ = weather;
    }
    
private:
    std::atomic<uint64_t> message_id_counter_{1};
    
    uint64_t generate_message_id() {
        return message_id_counter_.fetch_add(1);
    }
    
    void initialize_default_topology() {
        // Create typical HFT network topology
        NetworkPath coinbase_path;
        coinbase_path.nodes.emplace_back("client_gateway", "NYC", ConnectionType::CROSS_CONNECT, 10);
        coinbase_path.nodes.emplace_back("coinbase_gateway", "NYC", ConnectionType::FIBER, 50);
        coinbase_path.base_latency_us = 100;
        coinbase_path.jitter_us = 20;
        coinbase_path.bandwidth_mbps = 1000;
        
        NetworkPath binance_path;
        binance_path.nodes.emplace_back("client_gateway", "NYC", ConnectionType::CROSS_CONNECT, 10);
        binance_path.nodes.emplace_back("transatlantic_fiber", "NYC-LON", ConnectionType::FIBER, 2000);
        binance_path.nodes.emplace_back("binance_gateway", "LON", ConnectionType::FIBER, 50);
        binance_path.base_latency_us = 2500;
        binance_path.jitter_us = 100;
        binance_path.bandwidth_mbps = 10000;
        
        // Microwave path as backup
        NetworkPath microwave_path;
        microwave_path.nodes.emplace_back("client_gateway", "NYC", ConnectionType::CROSS_CONNECT, 10);
        microwave_path.nodes.emplace_back("microwave_tower_1", "NYC", ConnectionType::MICROWAVE, 800);
        microwave_path.nodes.emplace_back("microwave_tower_2", "CHI", ConnectionType::MICROWAVE, 600);
        microwave_path.nodes.emplace_back("exchange_gateway", "CHI", ConnectionType::MICROWAVE, 50);
        microwave_path.base_latency_us = 1800; // Faster than fiber for some routes
        microwave_path.jitter_us = 200;
        microwave_path.bandwidth_mbps = 100;
        
        network_paths_["coinbase"] = coinbase_path;
        network_paths_["binance"] = binance_path;
        network_paths_["microwave_backup"] = microwave_path;
    }
    
    double calculate_failure_impact(const std::string& path_name) const {
        double total_impact = 1.0;
        
        // Calculate combined impact of all active failures
        std::queue<FailureScenario> temp_queue = active_failures_;
        while (!temp_queue.empty()) {
            const auto& failure = temp_queue.front();
            temp_queue.pop();
            
            if (failure.affected_component.empty() || 
                failure.affected_component == path_name) {
                total_impact *= (1.0 + failure.severity);
            }
        }
        
        return total_impact;
    }
    
    double calculate_packet_loss_rate(const NetworkPath& path, double failure_impact) const {
        double base_loss_rate = 0.0001; // 0.01% base packet loss
        
        // Increase loss rate based on failure impact
        double adjusted_loss_rate = base_loss_rate * failure_impact;
        
        // Weather impact on microwave links
        for (const auto& node : path.nodes) {
            if (node.type == ConnectionType::MICROWAVE) {
                switch (current_weather_) {
                    case WeatherCondition::HEAVY_RAIN:
                        adjusted_loss_rate *= 10;
                        break;
                    case WeatherCondition::SNOW:
                        adjusted_loss_rate *= 5;
                        break;
                    case WeatherCondition::STORM:
                        adjusted_loss_rate *= 20;
                        break;
                    default:
                        break;
                }
                break;
            }
        }
        
        return std::min(adjusted_loss_rate, 0.5); // Cap at 50% loss rate
    }
    
    double calculate_weather_impact(const NetworkPath& path) const {
        double impact = 1.0;
        
        for (const auto& node : path.nodes) {
            if (node.type == ConnectionType::MICROWAVE) {
                switch (current_weather_) {
                    case WeatherCondition::LIGHT_RAIN:
                        impact *= 1.1;
                        break;
                    case WeatherCondition::HEAVY_RAIN:
                        impact *= 1.5;
                        break;
                    case WeatherCondition::SNOW:
                        impact *= 1.3;
                        break;
                    case WeatherCondition::FOG:
                        impact *= 1.2;
                        break;
                    case WeatherCondition::STORM:
                        impact *= 2.0;
                        break;
                    default:
                        break;
                }
                break; // Only apply weather impact once per path
            }
        }
        
        return impact;
    }
    
    double calculate_transmission_time(size_t bytes, double bandwidth_mbps) const {
        // Convert bytes to bits, bandwidth to bits per microsecond
        double bits = bytes * 8.0;
        double bandwidth_bps_us = bandwidth_mbps * 1000000.0 / 1000000.0; // bits per microsecond
        return bits / bandwidth_bps_us;
    }
    
    bool should_drop_message(const Message& msg, const std::string& path_name) {
        // Check rate limiting
        auto it = rate_limiters_.find(path_name);
        if (it != rate_limiters_.end()) {
            if (it->second.is_rate_limited(get_timestamp_ns())) {
                return true; // Drop due to rate limiting
            }
        }
        
        // Check for authentication failures (simulate occasionally)
        if (std::uniform_real_distribution<double>(0, 1)(rng_) < 0.00001) {
            return true; // Drop due to auth failure
        }
        
        return false;
    }
    
    void process_scheduled_failures() {
        uint64_t current_time = get_timestamp_ns();
        
        auto it = std::remove_if(scheduled_failures_.begin(), scheduled_failures_.end(),
            [this, current_time](FailureScenario& scenario) {
                if (current_time >= scenario.start_time) {
                    inject_failure(scenario);
                    return true;
                }
                return false;
            });
        
        scheduled_failures_.erase(it, scheduled_failures_.end());
    }
    
    void process_active_failures() {
        uint64_t current_time = get_timestamp_ns();
        std::queue<FailureScenario> remaining_failures;
        
        while (!active_failures_.empty()) {
            auto failure = active_failures_.front();
            active_failures_.pop();
            
            if (current_time < failure.start_time + failure.duration) {
                remaining_failures.push(failure);
            } else {
                // Failure has expired, restore normal operation
                restore_from_failure(failure);
            }
        }
        
        active_failures_ = remaining_failures;
    }
    
    void simulate_connection_drop(const FailureScenario& scenario) {
        // Disable random network paths
        for (auto& [name, path] : network_paths_) {
            if (std::uniform_real_distribution<double>(0, 1)(rng_) < scenario.severity) {
                for (auto& node : path.nodes) {
                    node.is_active = false;
                    node.last_failure_time = get_timestamp_ns();
                }
            }
        }
    }
    
    void simulate_gateway_overload(const FailureScenario& scenario) {
        // Reduce rate limits dramatically
        for (auto& [path_name, rate_limiter] : rate_limiters_) {
            rate_limiter.messages_per_second = static_cast<uint64_t>(
                rate_limiter.messages_per_second * (1.0 - scenario.severity)
            );
        }
    }
    
    void simulate_increased_packet_loss(const FailureScenario& scenario) {
        // This is handled in calculate_packet_loss_rate based on active failures
        // No additional action needed here
    }
    
    void simulate_rate_limiting(const FailureScenario& scenario) {
        // Create or modify rate limiters for affected paths
        std::string affected_path = scenario.affected_component;
        if (affected_path.empty()) {
            affected_path = "default";
        }
        
        auto& limiter = rate_limiters_[affected_path];
        limiter.messages_per_second = static_cast<uint64_t>(100 * (1.0 - scenario.severity));
    }
    
    void simulate_market_data_gap(const FailureScenario& scenario) {
        // This would be handled by the market data generator
        // Mark messages from market data sources as dropped
    }
    
    void simulate_ddos_attack(const FailureScenario& scenario) {
        // Severely impact all network paths
        for (auto& [name, path] : network_paths_) {
            path.congestion_factor *= (1.0 + scenario.severity * 10);
            path.bandwidth_mbps *= (1.0 - scenario.severity * 0.8);
        }
    }
    
    void simulate_time_sync_drift(const FailureScenario& scenario) {
        // This would affect timestamp accuracy
        // Could be implemented by adding random offsets to timestamps
    }
    
    void restore_from_failure(const FailureScenario& scenario) {
        switch (scenario.type) {
            case FailureScenario::CONNECTION_DROP:
                // Restore network paths
                for (auto& [name, path] : network_paths_) {
                    for (auto& node : path.nodes) {
                        if (!node.is_active && node.last_failure_time > 0) {
                            node.is_active = true;
                            node.last_failure_time = 0;
                        }
                    }
                }
                break;
            case FailureScenario::GATEWAY_OVERLOAD:
                // Restore rate limits
                for (auto& [path_name, rate_limiter] : rate_limiters_) {
                    rate_limiter.messages_per_second = 1000; // Default rate
                }
                break;
            case FailureScenario::DDOS_ATTACK:
                // Restore network performance
                for (auto& [name, path] : network_paths_) {
                    path.congestion_factor = 1.0;
                    // Restore original bandwidth (would need to store original values)
                }
                break;
            default:
                break;
        }
    }
};

} // namespace hft::simulator
