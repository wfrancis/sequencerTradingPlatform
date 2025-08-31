#pragma once

#include <vector>
#include <map>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <random>
#include "messages.hpp"
#include "market_data_generator.hpp"
#include "exchange_simulator.hpp"
#include "infrastructure_simulator.hpp"

namespace hft::simulator {

enum class ScenarioType : uint8_t {
    NORMAL_TRADING = 0,
    HIGH_VOLATILITY = 1,
    FLASH_CRASH = 2,
    EXCHANGE_OUTAGE = 3,
    NETWORK_ISSUES = 4,
    STRESS_TEST = 5
};

class ScenarioEngine {
public:
    struct Event {
        uint64_t timestamp_ns;
        std::string name;
        std::function<void()> action;
        bool is_executed = false;
        
        Event(uint64_t ts, const std::string& n, std::function<void()> act)
            : timestamp_ns(ts), name(n), action(std::move(act)) {}
    };
    
    struct Scenario {
        std::string name;
        std::string description;
        ScenarioType type;
        uint64_t duration_ms;
        std::vector<Event> events;
        bool is_running = false;
        
        Scenario(const std::string& n, ScenarioType t, uint64_t dur_ms)
            : name(n), type(t), duration_ms(dur_ms) {}
        
        void add_event(uint64_t offset_ms, const std::string& event_name, 
                      std::function<void()> action) {
            events.emplace_back(offset_ms * 1000000ULL, event_name, std::move(action));
        }
    };

private:
    std::vector<Scenario> scenarios_;
    Scenario* current_scenario_ = nullptr;
    std::atomic<uint32_t> events_executed_{0};
    
    // Component references
    MarketDataGenerator* market_data_gen_ = nullptr;
    ExchangeSimulator* exchange_sim_ = nullptr;
    InfrastructureSimulator* infra_sim_ = nullptr;

public:
    ScenarioEngine() {
        create_predefined_scenarios();
    }
    
    void set_components(MarketDataGenerator* mdg, ExchangeSimulator* es, InfrastructureSimulator* is) {
        market_data_gen_ = mdg;
        exchange_sim_ = es;
        infra_sim_ = is;
    }
    
    bool run_scenario(const std::string& scenario_name) {
        auto* scenario = find_scenario(scenario_name);
        if (!scenario) return false;
        
        current_scenario_ = scenario;
        scenario->is_running = true;
        
        printf("🎬 Starting scenario: %s\n", scenario->name.c_str());
        printf("   Duration: %lu ms, Events: %zu\n", scenario->duration_ms, scenario->events.size());
        
        // Execute all events immediately for this demo
        for (auto& event : scenario->events) {
            try {
                printf("   🎯 Executing: %s\n", event.name.c_str());
                event.action();
                event.is_executed = true;
                events_executed_.fetch_add(1);
            } catch (const std::exception& e) {
                printf("   ❌ Event failed: %s - %s\n", event.name.c_str(), e.what());
            }
        }
        
        scenario->is_running = false;
        current_scenario_ = nullptr;
        printf("✅ Scenario completed: %s\n", scenario->name.c_str());
        return true;
    }
    
    const std::vector<Scenario>& get_scenarios() const { return scenarios_; }
    
    void stop_current_scenario() {
        if (current_scenario_) {
            current_scenario_->is_running = false;
        }
    }
    
    void print_status() const {
        printf("🎬 Scenario Engine Status:\n");
        printf("   Available scenarios: %zu\n", scenarios_.size());
        printf("   Events executed: %u\n", events_executed_.load());
        if (current_scenario_) {
            printf("   Current: %s\n", current_scenario_->name.c_str());
        }
    }

private:
    Scenario* find_scenario(const std::string& name) {
        auto it = std::find_if(scenarios_.begin(), scenarios_.end(),
            [&name](const Scenario& s) { return s.name == name; });
        return it != scenarios_.end() ? &(*it) : nullptr;
    }
    
    void create_predefined_scenarios() {
        // Normal Trading Day
        {
            Scenario scenario("normal_trading", ScenarioType::NORMAL_TRADING, 60000); // 1 minute
            scenario.description = "Normal market conditions";
            scenario.add_event(0, "market_init", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->initialize_order_book(1, 50000.0, 10.0);
                    market_data_gen_->initialize_order_book(2, 3000.0, 15.0);
                }
            });
            scenario.add_event(30000, "volume_spike", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->simulate_market_event(EventType::TRADE, 1, Venue::COINBASE);
                }
            });
            scenarios_.push_back(scenario);
        }
        
        // High Volatility
        {
            Scenario scenario("high_volatility", ScenarioType::HIGH_VOLATILITY, 30000); // 30 seconds
            scenario.description = "High volatility market conditions";
            scenario.add_event(0, "volatility_spike", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->inject_anomaly(AnomalyType::NEWS_SHOCK);
                }
            });
            scenarios_.push_back(scenario);
        }
        
        // Flash Crash
        {
            Scenario scenario("flash_crash", ScenarioType::FLASH_CRASH, 60000); // 1 minute
            scenario.description = "Simulated flash crash event";
            scenario.add_event(10000, "crash_trigger", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->inject_anomaly(AnomalyType::FLASH_CRASH);
                }
            });
            scenario.add_event(30000, "liquidity_void", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->inject_anomaly(AnomalyType::LIQUIDITY_VOID);
                }
            });
            scenarios_.push_back(scenario);
        }
        
        // Exchange Outage
        {
            Scenario scenario("exchange_outage", ScenarioType::EXCHANGE_OUTAGE, 45000); // 45 seconds
            scenario.description = "Exchange connectivity issues";
            scenario.add_event(0, "outage_start", [this]() {
                if (infra_sim_) {
                    infra_sim_->simulate_exchange_outage(Venue::COINBASE, 30000, false);
                }
            });
            scenarios_.push_back(scenario);
        }
        
        // Network Issues
        {
            Scenario scenario("network_issues", ScenarioType::NETWORK_ISSUES, 40000); // 40 seconds
            scenario.description = "Network connectivity problems";
            scenario.add_event(0, "network_degradation", [this]() {
                if (infra_sim_) {
                    InfrastructureSimulator::FailureScenario failure(
                        InfrastructureSimulator::FailureScenario::PACKET_LOSS,
                        get_timestamp_ns(), 30000 * 1000000ULL, 0.5);
                    infra_sim_->inject_failure(failure);
                }
            });
            scenarios_.push_back(scenario);
        }
        
        // Stress Test
        {
            Scenario scenario("stress_test", ScenarioType::STRESS_TEST, 120000); // 2 minutes
            scenario.description = "Combined stress test scenario";
            scenario.add_event(0, "market_init", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->initialize_order_book(1, 45000.0, 20.0);
                }
            });
            scenario.add_event(20000, "volatility_injection", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->inject_anomaly(AnomalyType::NEWS_SHOCK);
                }
            });
            scenario.add_event(60000, "network_issues", [this]() {
                if (infra_sim_) {
                    InfrastructureSimulator::FailureScenario failure(
                        InfrastructureSimulator::FailureScenario::CONNECTION_DROP,
                        get_timestamp_ns(), 20000 * 1000000ULL, 0.3);
                    infra_sim_->inject_failure(failure);
                }
            });
            scenario.add_event(90000, "flash_crash", [this]() {
                if (market_data_gen_) {
                    market_data_gen_->inject_anomaly(AnomalyType::FLASH_CRASH);
                }
            });
            scenarios_.push_back(scenario);
        }
    }
};

} // namespace hft::simulator
