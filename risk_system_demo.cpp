#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include "unified_risk_controller.hpp"
#include "position_tracker.hpp"
#include "messages.hpp"

/**
 * Risk System Integration Demo
 * 
 * Demonstrates how to integrate and use the comprehensive risk control system
 * in a high-frequency trading application.
 */

void print_separator(const std::string& title) {
    std::cout << "\n" << std::string(50, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(50, '=') << "\n";
}

void simulate_market_data_feed(risk::UnifiedRiskController& risk_controller) {
    // Simulate realistic market data updates
    std::vector<std::tuple<std::string, std::string, double, double, double, double>> market_data = {
        {"COINBASE", "BTC/USD", 45000.0, 44995.0, 45005.0, 1.5},
        {"COINBASE", "ETH/USD", 3200.0, 3198.0, 3202.0, 15.0},
        {"BINANCE", "BTC/USD", 45001.0, 44996.0, 45006.0, 1.8},
        {"BINANCE", "ETH/USD", 3201.0, 3199.0, 3203.0, 12.0}
    };
    
    for (const auto& [exchange, symbol, price, bid, ask, volume] : market_data) {
        risk_controller.update_market_data(exchange, symbol, price, bid, ask, volume);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void demonstrate_trade_authorization(risk::UnifiedRiskController& risk_controller) {
    print_separator("TRADE AUTHORIZATION DEMONSTRATION");
    
    // Test various trade scenarios
    std::vector<std::tuple<std::string, std::string, double, double, double>> test_trades = {
        {"COINBASE", "BTC/USD", 0.001, 45000.0, 2.5},  // Small BTC trade
        {"COINBASE", "ETH/USD", 0.1, 3200.0, 3.0},     // Small ETH trade
        {"BINANCE", "BTC/USD", 0.1, 45000.0, 2.0},     // Larger BTC trade
        {"COINBASE", "BTC/USD", 1.0, 45000.0, 50.0},   // Very large trade (should be rejected)
        {"BINANCE", "ETH/USD", 10.0, 3200.0, 100.0}    // Massive trade (should be rejected)
    };
    
    for (const auto& [exchange, symbol, quantity, price, spread] : test_trades) {
        std::cout << "\n🔍 Testing trade: " << exchange << " " << symbol 
                  << " qty=" << quantity << " price=" << price << "\n";
        
        auto response = risk_controller.authorize_trade(exchange, symbol, quantity, price, spread);
        
        std::cout << "  Result: " << (response.authorized ? "✅ AUTHORIZED" : "❌ REJECTED") << "\n";
        std::cout << "  Confidence: " << (response.confidence_score * 100) << "%\n";
        
        if (!response.authorized) {
            std::cout << "  Reason: " << response.rejection_reason << "\n";
        }
        
        if (!response.warnings.empty()) {
            std::cout << "  Warnings:\n";
            for (const auto& warning : response.warnings) {
                std::cout << "    ⚠️ " << warning << "\n";
            }
        }
        
        // If authorized, simulate trade execution
        if (response.authorized) {
            risk_controller.report_trade_execution(exchange, symbol, quantity, price);
            std::cout << "  ✅ Trade executed and reported\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void demonstrate_system_monitoring(risk::UnifiedRiskController& risk_controller) {
    print_separator("SYSTEM MONITORING DEMONSTRATION");
    
    auto* health_monitor = risk_controller.get_system_health_monitor();
    
    // Simulate some system activity
    std::cout << "🔄 Simulating system activity...\n";
    
    // Update exchange connectivity
    health_monitor->update_exchange_connectivity("COINBASE", true, true, true);
    health_monitor->update_exchange_connectivity("BINANCE", true, true, true);
    
    // Update application metrics
    health_monitor->update_queue_depth("order_processing", 50);
    health_monitor->update_queue_depth("market_data", 120);
    health_monitor->update_processing_latency("order_processing", 85.5);
    health_monitor->update_processing_latency("market_data", 45.2);
    
    // Report component health
    health_monitor->report_component_health("sequencer", true);
    health_monitor->report_component_health("position_tracker", true);
    health_monitor->report_component_health("risk_manager", true);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    std::cout << "📊 Current system health:\n";
    std::cout << "  System Healthy: " << (health_monitor->is_system_healthy() ? "YES" : "NO") << "\n";
    std::cout << "  Critical Alerts: " << (health_monitor->is_critical_alert_active() ? "ACTIVE" : "NONE") << "\n";
}

void demonstrate_emergency_controls(risk::UnifiedRiskController& risk_controller) {
    print_separator("EMERGENCY CONTROLS DEMONSTRATION");
    
    std::cout << "🚨 Testing emergency stop functionality...\n";
    
    // Test emergency stop
    risk_controller.emergency_stop("Demo emergency stop test");
    
    std::cout << "📊 Status after emergency stop:\n";
    std::cout << "  Trading Enabled: " << (risk_controller.is_trading_enabled() ? "YES" : "NO") << "\n";
    std::cout << "  Current Status: " << static_cast<int>(risk_controller.get_current_status()) << "\n";
    
    // Try to execute a trade during emergency stop
    std::cout << "\n🔍 Attempting trade during emergency stop...\n";
    auto response = risk_controller.authorize_trade("COINBASE", "BTC/USD", 0.001, 45000.0, 2.5);
    std::cout << "  Result: " << (response.authorized ? "✅ AUTHORIZED" : "❌ REJECTED") << "\n";
    std::cout << "  Reason: " << response.rejection_reason << "\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Reset emergency stop
    std::cout << "\n🔄 Resetting emergency stop...\n";
    risk_controller.reset_emergency_stop();
    
    std::cout << "📊 Status after reset:\n";
    std::cout << "  Trading Enabled: " << (risk_controller.is_trading_enabled() ? "YES" : "NO") << "\n";
    std::cout << "  Current Status: " << static_cast<int>(risk_controller.get_current_status()) << "\n";
}

void demonstrate_dynamic_risk_adjustment(risk::UnifiedRiskController& risk_controller) {
    print_separator("DYNAMIC RISK ADJUSTMENT DEMONSTRATION");
    
    auto* comprehensive_manager = risk_controller.get_comprehensive_risk_manager();
    
    std::cout << "🔄 Simulating volatile market conditions...\n";
    
    // Simulate high volatility market data
    for (int i = 0; i < 20; ++i) {
        double volatility_factor = 1.0 + (i * 0.02); // Increasing volatility
        double btc_price = 45000.0 * (1.0 + (rand() % 200 - 100) / 10000.0 * volatility_factor);
        double eth_price = 3200.0 * (1.0 + (rand() % 200 - 100) / 10000.0 * volatility_factor);
        
        risk_controller.update_market_data("COINBASE", "BTC/USD", btc_price, 
                                         btc_price - 5, btc_price + 5, 1.0);
        risk_controller.update_market_data("COINBASE", "ETH/USD", eth_price, 
                                         eth_price - 2, eth_price + 2, 10.0);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "📊 Dynamic limits after volatility simulation:\n";
    auto dynamic_limits = comprehensive_manager->get_dynamic_limits();
    std::cout << "  Volatility Multiplier: " << dynamic_limits.volatility_multiplier << "\n";
    std::cout << "  Liquidity Multiplier: " << dynamic_limits.liquidity_multiplier << "\n";
    std::cout << "  Effective BTC Limit: " << dynamic_limits.effective_max_position_btc << "\n";
    std::cout << "  Effective ETH Limit: " << dynamic_limits.effective_max_position_eth << "\n";
}

void setup_risk_event_callbacks(risk::UnifiedRiskController& risk_controller) {
    // Register callback for risk events
    risk_controller.register_event_callback([](const risk::RiskControlEvent& event) {
        std::cout << "🔔 Risk Event Callback: " << event.component 
                  << " - " << event.description 
                  << " (severity: " << event.severity_score << ")\n";
    });
}

int main() {
    std::cout << "🚀 Starting Risk System Integration Demo\n";
    std::cout << "This demo showcases the comprehensive risk control system\n";
    
    try {
        // Initialize core components
        auto position_tracker = std::make_unique<PositionTracker>();
        auto risk_controller = std::make_unique<risk::UnifiedRiskController>(position_tracker.get());
        
        // Setup event callbacks
        setup_risk_event_callbacks(*risk_controller);
        
        print_separator("SYSTEM INITIALIZATION");
        std::cout << "✅ Position Tracker initialized\n";
        std::cout << "✅ Unified Risk Controller initialized\n";
        std::cout << "✅ Event callbacks registered\n";
        
        // Allow system to initialize
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Demonstrate various aspects of the risk system
        simulate_market_data_feed(*risk_controller);
        demonstrate_system_monitoring(*risk_controller);
        demonstrate_trade_authorization(*risk_controller);
        demonstrate_dynamic_risk_adjustment(*risk_controller);
        demonstrate_emergency_controls(*risk_controller);
        
        // Generate comprehensive risk report
        print_separator("COMPREHENSIVE RISK REPORT");
        risk_controller->generate_unified_risk_report();
        
        print_separator("DEMO COMPLETED SUCCESSFULLY");
        std::cout << "🎉 All risk control features demonstrated successfully!\n";
        std::cout << "📋 Key Features Showcased:\n";
        std::cout << "  ✅ Multi-layered trade authorization\n";
        std::cout << "  ✅ Real-time system health monitoring\n";
        std::cout << "  ✅ Dynamic risk limit adjustment\n";
        std::cout << "  ✅ Emergency stop controls\n";
        std::cout << "  ✅ Comprehensive risk reporting\n";
        std::cout << "  ✅ Event-driven risk management\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error during demo: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
