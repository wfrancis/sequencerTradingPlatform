#pragma once

#include <random>
#include <queue>
#include <chrono>
#include "../messages.hpp"
#include "../ring_buffer.hpp"

namespace hft {

class NetworkSimulator {
private:
    struct DelayedMessage {
        ::hft::SequencedMessage msg;
        uint64_t delivery_time_ns;
        
        bool operator>(const DelayedMessage& other) const {
            return delivery_time_ns > other.delivery_time_ns;
        }
    };
    
    std::mt19937 rng_;
    std::normal_distribution<double> latency_dist_;
    std::uniform_real_distribution<double> packet_loss_dist_;
    std::priority_queue<DelayedMessage, std::vector<DelayedMessage>, std::greater<>> delayed_queue_;
    
    double base_latency_us_ = 100.0;
    double latency_jitter_us_ = 20.0;
    double packet_loss_rate_ = 0.001;
    bool reorder_enabled_ = true;
    double reorder_probability_ = 0.01;

public:
    NetworkSimulator(double base_latency_us, double jitter_us, double loss_rate)
        : rng_(std::random_device{}()),
          latency_dist_(base_latency_us, jitter_us),
          packet_loss_dist_(0.0, 1.0),
          base_latency_us_(base_latency_us),
          latency_jitter_us_(jitter_us),
          packet_loss_rate_(loss_rate) {}
    
    bool simulate_send(const ::hft::SequencedMessage& msg, ::hft::SPSCRingBuffer<1024>& output_buffer) {
        // Simulate packet loss
        if (packet_loss_dist_(rng_) < packet_loss_rate_) {
            return false; // Message lost
        }
        
        // Calculate delivery time with jitter
        double latency_us = std::max(0.0, latency_dist_(rng_));
        uint64_t current_time = ::hft::get_timestamp_ns();
        uint64_t delivery_time = current_time + static_cast<uint64_t>(latency_us * 1000);
        
        // Simulate reordering
        if (reorder_enabled_ && packet_loss_dist_(rng_) < reorder_probability_) {
            latency_us *= 1.5; // Delayed packet
        }
        
        delayed_queue_.push({msg, delivery_time});
        
        // Process any messages ready for delivery
        process_delayed_messages(output_buffer);
        return true;
    }
    
    void process_delayed_messages(::hft::SPSCRingBuffer<1024>& output_buffer) {
        uint64_t current_time = ::hft::get_timestamp_ns();
        
        while (!delayed_queue_.empty() && 
               delayed_queue_.top().delivery_time_ns <= current_time) {
            output_buffer.write(delayed_queue_.top().msg);
            delayed_queue_.pop();
        }
    }
};

} // namespace hft
