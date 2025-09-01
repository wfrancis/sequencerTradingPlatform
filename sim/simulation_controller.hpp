#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "../messages.hpp"
#include "../market_data_generator.hpp"
#include "exchange_simulator.hpp"
#include "infrastructure_simulator.hpp"
#include "../scenario_engine.hpp"

namespace hft::simulator {

struct SimulationConfig {
    // Time settings
    uint64_t start_time_ns = get_timestamp_ns();
    uint64_t tick_interval_ns = 1000000ULL; // 1ms ticks
    double time_acceleration = 1.0;
    
    // Market settings
    std::vector<std::string> symbols = {"BTC/USD", "ETH/USD"};
    std::vector<Venue> venues = {Venue::COINBASE, Venue::BINANCE};
    
    // Infrastructure settings
    bool simulate_network_issues = true;
    bool simulate_exchange_outages = true;
    
    // Validation settings
    bool validate_invariants = true;
    bool check_arbitrage_bounds = true;
    bool detect_manipulation = false;
};

struct SimulationResults {
    uint64_t duration_ms = 0;
    uint64_t total_trades = 0;
    uint64_t total_messages = 0;
    double average_latency_us = 0.0;
    uint32_t network_failures = 0;
    uint32_t exchange_outages = 0;
    
    // Market data statistics
    double btc_price_start = 0.0;
    double btc_price_end = 0.0;
    double eth_price_start = 0.0;
    double eth_price_end = 0.0;
    double max_spread_bps = 0.0;
    
    void print_summary() const {
        printf("\n📊 Simulation Results Summary:\n");
        printf("   Duration: %lu ms\n", duration_ms);
        printf("   Total trades: %lu\n", total_trades);
        printf("   Total messages: %lu\n", total_messages);
        printf("   Average latency: %.1f μs\n", average_latency_us);
        printf("   Network failures: %u\n", network_failures);
        printf("   Exchange outages: %u\n", exchange_outages);
        printf("   BTC: $%.2f → $%.2f (%.2f%%)\n", btc_price_start, btc_price_end,
               btc_price_start > 0 ? ((btc_price_end - btc_price_start) / btc_price_start * 100) : 0);
        printf("   ETH: $%.2f → $%.2f (%.2f%%)\n", eth_price_start, eth_price_end,
               eth_price_start > 0 ? ((eth_price_end - eth_price_start) / eth_price_start * 100) : 0);
        printf("   Max spread: %.1f bps\n", max_spread_bps);
    }
};

class SimulationController {
private:
    SimulationConfig config_;
    std::unique_ptr<MarketDataGenerator> market_data_gen_;
    std::unique_ptr<ExchangeSimulator> exchange_sim_;
    std::unique_ptr<InfrastructureSimulator> infra_sim_;
    std::unique_ptr<ScenarioEngine> scenario_engine_;
    
    std::atomic<bool> running_{false};
    uint64_t simulation_start_time_ = 0;
    SimulationResults results_;

public:
    SimulationController() {
        initialize_components();
    }
    
    void initialize(const SimulationConfig& config) {
        config_ = config;
        
        // Configure market data generator
        market_data_gen_->configure_market_dynamics();
        
        // Set up exchange simulator callbacks
        exchange_sim_->set_callbacks(
            [this](const ExchangeSimulator::Order& order) {
                // Order acknowledgment callback
                printf("   📋 Order ACK: ID=%llu, Price=$%.2f, Qty=%.3f\n", 
                       static_cast<unsigned long long>(order.id), to_float_price(order.price), to_float_price(order.quantity));
            },
            [this](const ExchangeSimulator::Order& order, const std::string& reason) {
                // Order rejection callback
                printf("   ❌ Order REJECT: ID=%llu, Reason=%s\n", static_cast<unsigned long long>(order.id), reason.c_str());
            },
            [this](const ExchangeSimulator::Fill& fill) {
                // Fill callback
                printf("   💰 FILL: ID=%llu, Price=$%.2f, Qty=%.3f, Fee=$%.4f\n",
                       static_cast<unsigned long long>(fill.order_id), to_float_price(fill.price), 
                       to_float_price(fill.quantity), fill.fee);
                results_.total_trades++;
            },
            [this](SymbolId symbol_id, Price bid, Price ask) {
                // Market data callback
                double spread_bps = 0.0;
                if (bid > 0 && ask > 0) {
                    double mid = (to_float_price(bid) + to_float_price(ask)) / 2.0;
                    spread_bps = ((to_float_price(ask) - to_float_price(bid)) / mid) * 10000.0;
                    results_.max_spread_bps = std::max(results_.max_spread_bps, spread_bps);
                }
            }
        );
        
        // Connect scenario engine to components
        scenario_engine_->set_components(market_data_gen_.get(), 
                                       exchange_sim_.get(), 
                                       infra_sim_.get());
        
        printf("🚀 Simulation initialized\n");
        printf("   Symbols: %zu\n", config_.symbols.size());
        printf("   Venues: %zu\n", config_.venues.size());
        printf("   Time acceleration: %.1fx\n", config_.time_acceleration);
    }
    
    SimulationResults run_simulation(const std::string& scenario_name, uint64_t duration_ms) {
        if (running_.load()) {
            printf("❌ Simulation already running\n");
            return results_;
        }
        
        running_.store(true);
        simulation_start_time_ = get_timestamp_ns();
        results_ = SimulationResults{}; // Reset results
        
        printf("\n🎬 Starting simulation with scenario: %s\n", scenario_name.c_str());
        printf("   Duration: %lu ms\n", duration_ms);
        
        // Capture initial market state
        auto btc_book = market_data_gen_->get_order_book(1);
        auto eth_book = market_data_gen_->get_order_book(2);
        results_.btc_price_start = btc_book.mid_price;
        results_.eth_price_start = eth_book.mid_price;
        
        // Run the scenario
        bool scenario_success = scenario_engine_->run_scenario(scenario_name);
        if (!scenario_success) {
            printf("❌ Failed to run scenario: %s\n", scenario_name.c_str());
            running_.store(false);
            return results_;
        }
        
        // Main simulation loop
        auto loop_start = std::chrono::steady_clock::now();
        auto next_tick = loop_start;
        
        printf("\n🔄 Running simulation loop...\n");
        
        while (running_.load()) {
            auto now = std::chrono::steady_clock::now();
            
            // Check if simulation duration has elapsed
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - loop_start);
            if (elapsed.count() >= static_cast<long>(duration_ms)) {
                printf("⏰ Simulation duration reached: %lld ms\n", static_cast<long long>(elapsed.count()));
                break;
            }
            
            // Process market data updates
            uint64_t current_time = get_timestamp_ns();
            for (SymbolId symbol : {1, 2}) { // BTC and ETH
                for (Venue venue : config_.venues) {
                    auto book = market_data_gen_->get_order_book(symbol);
                    market_data_gen_->generate_order_book_snapshot(symbol, venue, current_time, 
                        const_cast<MarketDataGenerator::OrderBookState&>(book));
                }
            }
            
            // Process exchange matching
            exchange_sim_->process_matching();
            
            // Process network/infrastructure
            auto ready_messages = infra_sim_->process_delayed_messages();
            results_.total_messages += ready_messages.size();
            
            // Update statistics
            auto infra_stats = infra_sim_->get_statistics();
            results_.average_latency_us = infra_stats.average_latency_us;
            
            // Simulation tick timing
            next_tick += std::chrono::nanoseconds(static_cast<uint64_t>(
                config_.tick_interval_ns / config_.time_acceleration));
            
            if (next_tick > now) {
                std::this_thread::sleep_until(next_tick);
            }
            
            // Print progress every 10 seconds
            if (elapsed.count() % 10000 == 0 && elapsed.count() > 0) {
                printf("   📊 Progress: %lld ms (%.1f%%), Trades: %llu\n", 
                       static_cast<long long>(elapsed.count()), (elapsed.count() * 100.0) / duration_ms, 
                       static_cast<unsigned long long>(results_.total_trades));
            }
        }
        
        // Capture final market state
        btc_book = market_data_gen_->get_order_book(1);
        eth_book = market_data_gen_->get_order_book(2);
        results_.btc_price_end = btc_book.mid_price;
        results_.eth_price_end = eth_book.mid_price;
        
        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - loop_start);
        results_.duration_ms = total_elapsed.count();
        
        running_.store(false);
        
        printf("✅ Simulation completed!\n");
        return results_;
    }
    
    void stop_simulation() {
        running_.store(false);
        scenario_engine_->stop_current_scenario();
    }
    
    // Run predefined simulation scenarios
    SimulationResults run_normal_trading_simulation(uint64_t duration_ms = 60000) {
        return run_simulation("normal_trading", duration_ms);
    }
    
    SimulationResults run_stress_test(uint64_t duration_ms = 120000) {
        return run_simulation("stress_test", duration_ms);
    }
    
    SimulationResults run_flash_crash_test(uint64_t duration_ms = 60000) {
        return run_simulation("flash_crash", duration_ms);
    }
    
    // Get current simulation state
    void print_status() const {
        printf("🎮 Simulation Controller Status:\n");
        printf("   Running: %s\n", running_.load() ? "Yes" : "No");
        if (running_.load()) {
            uint64_t elapsed_ns = get_timestamp_ns() - simulation_start_time_;
            printf("   Runtime: %.1f ms\n", elapsed_ns / 1000000.0);
        }
        
        // Print component statuses
        if (exchange_sim_) {
            printf("\n");
            exchange_sim_->print_status();
        }
        if (infra_sim_) {
            printf("\n");
            infra_sim_->print_status();
        }
        if (scenario_engine_) {
            printf("\n");
            scenario_engine_->print_status();
        }
    }
    
    // List available scenarios
    void list_scenarios() const {
        printf("📋 Available Simulation Scenarios:\n");
        const auto& scenarios = scenario_engine_->get_scenarios();
        for (const auto& scenario : scenarios) {
            printf("   • %s: %s (Duration: %llu ms)\n", 
                   scenario.name.c_str(), scenario.description.c_str(), 
                   static_cast<unsigned long long>(scenario.duration_ms));
        }
    }
    
    SimulationResults get_results() const {
        return results_;
    }

private:
    void initialize_components() {
        market_data_gen_ = std::make_unique<MarketDataGenerator>();
        exchange_sim_ = std::make_unique<ExchangeSimulator>();
        infra_sim_ = std::make_unique<InfrastructureSimulator>();
        scenario_engine_ = std::make_unique<ScenarioEngine>();
    }
};

} // namespace hft::simulator
