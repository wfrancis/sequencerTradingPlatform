#pragma once

#include <atomic>
#include <random>
#include <chrono>
#include <thread>
#include "shared_types.hpp"

namespace hft {

// Base class for exchange simulation across all simulators
class BaseExchangeSimulator {
protected:
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<uint64_t> orders_processed_{0};
    std::atomic<OrderId> next_order_id_{1};
    mutable std::mt19937_64 rng_;

public:
    BaseExchangeSimulator(uint64_t seed = std::random_device{}()) 
        : rng_(seed) {}

    virtual ~BaseExchangeSimulator() = default;

    // Pure virtual methods that each implementation must provide
    virtual OrderId submit_order(Side side, Price price, Quantity quantity, const std::string& trader = "trader") = 0;
    
    // Common getters
    uint64_t get_total_trades() const { 
        return total_trades_.load(); 
    }
    
    uint64_t get_orders_processed() const { 
        return orders_processed_.load(); 
    }
    
    void reset_stats() { 
        total_trades_.store(0);
        orders_processed_.store(0);
    }

protected:
    // Common order ID generation
    OrderId generate_order_id() {
        return next_order_id_.fetch_add(1);
    }

    // Common trade execution logic
    bool should_fill_order(double fill_probability = 0.7) {
        return std::uniform_real_distribution<>(0, 1)(rng_) > (1.0 - fill_probability);
    }

    void record_trade() {
        total_trades_.fetch_add(1);
    }

    void record_order() {
        orders_processed_.fetch_add(1);
    }
};

// Simple exchange simulator implementation
class SimpleExchangeSimulator : public BaseExchangeSimulator {
public:
    SimpleExchangeSimulator(uint64_t seed = std::random_device{}()) 
        : BaseExchangeSimulator(seed) {}

    OrderId submit_order(Side side, Price price, Quantity quantity, const std::string& trader = "trader") override {
        OrderId id = generate_order_id();
        record_order();
        
        // Simulate processing latency
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        printf("   📋 Order ACK: ID=%llu, Side=%s, Price=$%.2f, Qty=%.3f\n", 
               static_cast<unsigned long long>(id), 
               side_to_string(side),
               to_float_price(price), to_float_quantity(quantity));
        
        // Simulate fill probability (70% default)
        if (should_fill_order(0.7)) {
            printf("   💰 FILL: ID=%llu, Price=$%.2f, Qty=%.3f\n",
                   static_cast<unsigned long long>(id),
                   to_float_price(price), to_float_quantity(quantity));
            record_trade();
        }
        
        return id;
    }
};

// High-speed exchange simulator for performance testing
class HighSpeedExchangeSimulator : public BaseExchangeSimulator {
private:
    std::atomic<uint64_t> fills_executed_{0};

public:
    HighSpeedExchangeSimulator(uint64_t seed = std::random_device{}()) 
        : BaseExchangeSimulator(seed) {}

    OrderId submit_order(Side side, Price price, Quantity quantity, const std::string& trader = "trader") override {
        OrderId id = generate_order_id();
        record_order();
        
        // No artificial latency for ultra-fast simulation
        // Real HFT systems process in nanoseconds!
        
        // High fill rate for demo (90%)
        if (should_fill_order(0.9)) {
            fills_executed_.fetch_add(1);
            record_trade();
        }
        
        return id;
    }

    uint64_t get_fills_executed() const { 
        return fills_executed_.load(); 
    }
    
    void reset_stats() {
        BaseExchangeSimulator::reset_stats();
        fills_executed_.store(0);
    }
};

// Infrastructure simulator for network conditions
class BaseInfrastructureSimulator {
protected:
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_lost_{0};
    double packet_loss_rate_ = 0.0001;  // Very low under normal conditions
    double latency_multiplier_ = 1.0;

public:
    virtual ~BaseInfrastructureSimulator() = default;

    virtual bool send_message(const std::string& message = "ORDER") {
        messages_sent_.fetch_add(1);
        
        // Simulate packet loss
        std::random_device rd;
        std::mt19937 gen(rd());
        if (std::uniform_real_distribution<>(0, 1)(gen) < packet_loss_rate_) {
            messages_lost_.fetch_add(1);
            return false;
        }
        
        // Simulate latency (can be overridden by subclasses)
        simulate_latency();
        
        return true;
    }

    virtual void simulate_network_issues() {
        printf("🌐 Network issues injected - increased latency and packet loss\n");
        packet_loss_rate_ = 0.01;    // 1% packet loss
        latency_multiplier_ = 5.0;   // 5x latency
    }

    virtual void simulate_exchange_outage() {
        printf("🔌 Exchange outage - all messages will be lost\n");
        packet_loss_rate_ = 1.0;     // 100% packet loss
    }

    virtual void restore_network() {
        packet_loss_rate_ = 0.0001;
        latency_multiplier_ = 1.0;
    }

    // Getters
    double get_packet_loss_rate() const { 
        return packet_loss_rate_; 
    }
    
    uint64_t get_messages_sent() const { 
        return messages_sent_.load(); 
    }
    
    uint64_t get_messages_lost() const { 
        return messages_lost_.load(); 
    }

    void reset_stats() {
        messages_sent_.store(0);
        messages_lost_.store(0);
    }

protected:
    virtual void simulate_latency() {
        // Default latency simulation
        auto latency_us = static_cast<int>(100 * latency_multiplier_);
        std::this_thread::sleep_for(std::chrono::microseconds(latency_us));
    }
};

// Simple infrastructure simulator implementation
class SimpleInfrastructureSimulator : public BaseInfrastructureSimulator {
public:
    bool send_message(const std::string& message = "ORDER") override {
        return BaseInfrastructureSimulator::send_message(message);
    }
};

// High-speed infrastructure simulator
class HighSpeedInfrastructureSimulator : public BaseInfrastructureSimulator {
public:
    HighSpeedInfrastructureSimulator() {
        packet_loss_rate_ = 0.00001;  // Ultra-low loss rate
    }

    bool send_message(const std::string& message = "ORDER") override {
        messages_sent_.fetch_add(1);
        
        // Ultra-fast message processing - no artificial delays
        std::random_device rd;
        std::mt19937 gen(rd());
        if (std::uniform_real_distribution<>(0, 1)(gen) < packet_loss_rate_) {
            messages_lost_.fetch_add(1);
            return false;
        }
        return true;
    }

    void simulate_network_stress() {
        packet_loss_rate_ = 0.001;  // 0.1% loss during stress
    }

protected:
    void simulate_latency() override {
        // No artificial latency for high-speed simulation
    }
};

} // namespace hft
