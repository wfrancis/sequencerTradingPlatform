#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "messages.hpp"
#include "ring_buffer.hpp"
#include "sequencer.hpp"
#include "crypto_config.hpp"
#include "feed_handler.hpp"
#include "position_tracker.hpp"
#include "risk/enhanced_risk_manager.hpp"
#include "logger.hpp"
#include "connection_manager.hpp"
#include "sim/network_simulator.hpp"
#include "monitor/real_time_monitor.hpp"
#include "monitor/monitoring_integration.hpp"
#include "session_manager.hpp"

using namespace hft;
using namespace hft::monitoring;

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", shutting down gracefully...\n";
    running.store(false);
}

int main() {
    std::cout << "🚀 HFT Crypto Arbitrage MVP Starting...\n";
    std::cout << "📊 Monitoring BTC/ETH on Coinbase & Binance\n";
    std::cout << "💡 Press Ctrl+C to stop\n\n";
    
    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize logging
        hft::Logger::instance().init();
        LOG_INFO("HFT System starting");
        
        // Calibrate TSC
        hft::TSCTimer::calibrate();
        LOG_INFO("TSC calibration completed");
        
        // Initialize configuration
        CryptoConfig config;
        std::cout << "⚙️  Configuration loaded:\n";
        std::cout << "   - Paper trading: " << (config.strategy.paper_trading ? "ON" : "OFF") << "\n";
        std::cout << "   - Arbitrage threshold: " << (config.strategy.arbitrage_threshold * 10000) << " bps\n";
        std::cout << "   - Max position size: " << config.risk.max_position_size << "\n\n";
        
        // Create ring buffers for message passing
        SPSCRingBuffer<1024> market_data_buffer;
        SPSCRingBuffer<1024> signal_buffer;
        
        // Create sequencer for message ordering
        SPSCSequencer sequencer;
        
        // Initialize connection manager
        hft::ConnectionManager conn_manager;
        conn_manager.start();
        LOG_INFO("Connection manager started");
        
        // Connect to exchanges
        conn_manager.connect(hft::Venue::COINBASE);
        conn_manager.connect(hft::Venue::BINANCE);
        LOG_INFO("Connected to exchanges");
        
        // Initialize network simulator for testing
        hft::NetworkSimulator network_sim(100.0, 20.0, 0.001);
        LOG_INFO("Network simulator initialized (100us latency, 20us jitter, 0.1% loss)");
        
        // Initialize monitoring system
        MonitoringThresholds thresholds;
        thresholds.max_latency_ns = 50000000ULL;  // 50ms target
        thresholds.max_spread_bps = 500.0;        // 50bps alert threshold
        thresholds.min_profit_threshold = -500.0; // $500 max loss before emergency stop
        
        RealTimeMonitor monitor(thresholds);
        MonitoringIntegration monitor_integration(monitor);
        
        // Set up alert callback
        monitor.set_alert_callback([](const Alert& alert) {
            std::cout << "🚨 " << alert.to_string() << "\n";
        });
        
        // Initialize position tracker and risk manager
        PositionTracker position_tracker;
        EnhancedRiskManager risk_manager(&position_tracker);
        
        // Initialize session manager with laptop-optimized settings
        SessionConfig session_config;
        session_config.max_session_hours = 4.0;        // 4-hour sessions
        session_config.daily_loss_limit = 500.0;       // Stop at -$500
        session_config.daily_profit_target = 1000.0;   // Optional stop at +$1000
        session_config.enable_profit_target = false;   // Disabled by default
        session_config.battery_pause_threshold = 20.0; // Pause at 20% battery
        session_config.enable_battery_monitoring = true;
        session_config.enable_emergency_stops = true;
        session_config.max_drawdown_percent = 5.0;     // 5% max drawdown
        
        SessionManager session_manager(&position_tracker, session_config);
        
        // Create monitored wrappers
        MonitoredPositionTracker monitored_position_tracker(position_tracker, monitor_integration);
        MonitoredRiskManager monitored_risk_manager(risk_manager, monitor_integration);
        
        std::cout << "🛡️ Enhanced risk management initialized\n";
        std::cout << "📊 Position tracking enabled\n";
        std::cout << "🎯 Session management configured\n";
        std::cout << "💾 Trade logging: trade_log.csv\n";
        std::cout << "📋 Risk events: risk_events.csv\n";
        std::cout << "📝 Session logs: session_logs.csv\n\n";
        
        // Initialize market data feed handler (simulated)
        MockFeedHandler feed_handler(market_data_buffer, sequencer);
        MonitoredFeedHandler monitored_feed_handler(feed_handler, monitor_integration);
        
        // Initialize enhanced arbitrage strategy
        ArbitrageStrategy strategy(market_data_buffer, signal_buffer, config, 
                                 &position_tracker, &risk_manager);
        MonitoredArbitrageStrategy monitored_strategy(strategy, monitor_integration, monitor);
        
        // Start market data feed and monitoring
        feed_handler.start();
        monitor.start_monitoring();
        
        // Start trading session
        if (!session_manager.start_session()) {
            std::cout << "❌ Failed to start trading session\n";
            return 1;
        }
        
        // Main trading loop
        auto start_time = std::chrono::steady_clock::now();
        auto last_status_print = start_time;
        
        std::cout << "✅ Trading loop started\n";
        std::cout << "📊 Real-time monitoring active\n";
        std::cout << "🎯 Latency target: <50ms\n";
        std::cout << "🛑 Emergency stop: Ctrl+C, session limits, or automatic triggers\n\n";
        
        while (running.load() && session_manager.should_continue_session()) {
            // Check for emergency stop conditions
            if (monitor.is_emergency_stop()) {
                std::cout << "🛑 EMERGENCY STOP TRIGGERED: " << monitor.get_emergency_reason() << "\n";
                break;
            }
            
            // Process market data and look for arbitrage opportunities with monitoring
            {
                auto latency_tracker = monitor_integration.track_latency("main_loop_iteration");
                monitored_strategy.process_market_data();
            }
            
            // Process any generated signals with monitoring
            SequencedMessage signal;
            while (signal_buffer.read(signal)) {
                if (signal.type == MessageType::SIGNAL) {
                    // Record arbitrage signal metrics
                    monitored_strategy.record_arbitrage_signal(signal.signal.expected_edge);
                    
                    std::cout << "🎯 Arbitrage Signal: " 
                              << std::fixed << std::setprecision(1)
                              << signal.signal.expected_edge << " bps edge, "
                              << "strength: " << signal.signal.strength << "\n";
                    
                    if (config.strategy.paper_trading) {
                        std::cout << "   📝 Paper trade executed (simulated)\n";
                        // Simulate realistic P&L for monitoring and session tracking
                        double simulated_pnl = (signal.signal.expected_edge / 10000.0) * 1000.0; // $1000 notional
                        double estimated_fees = 2.0; // $2 fee estimate
                        
                        monitor.record_trade(simulated_pnl, estimated_fees);
                        session_manager.record_trade(simulated_pnl, estimated_fees);
                    }
                }
                
                // Update monitoring with message processing
                monitor.component_message_processed("signal_processing");
            }
            
            // Update component heartbeats
            monitor.component_heartbeat("main_loop");
            
            // Print status every 10 seconds (monitoring dashboard updates every 1s)
            auto now = std::chrono::steady_clock::now();
            if (now - last_status_print >= std::chrono::seconds(10)) {
                // Print traditional status (monitoring dashboard shows real-time metrics)
                std::cout << "\n📊 TRADITIONAL STATUS UPDATE:\n";
                monitored_strategy.print_status();
                monitored_position_tracker.print_status();
                monitored_risk_manager.print_risk_status();
                conn_manager.print_status();
                session_manager.print_session_status(); // Add session status
                last_status_print = now;
                
                // Print performance stats
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
                std::cout << "   ⏱️  Runtime: " << elapsed.count() << "s\n";
                std::cout << "   📊 Market data available: " << market_data_buffer.available() << "\n";
                std::cout << "   📈 Signals generated: " << signal_buffer.available() << "\n";
                
                // Show key monitoring metrics
                const auto* latency_metrics = monitor.get_latency_metrics("main_loop_iteration");
                if (latency_metrics && latency_metrics->sample_count > 0) {
                    std::cout << "   ⚡ Main loop latency: " << (latency_metrics->avg_ns / 1000000.0) 
                              << "ms avg, " << (latency_metrics->p95_ns / 1000000.0) << "ms p95\n";
                }
                
                const auto& pnl_metrics = monitor.get_profitability_metrics();
                std::cout << "   💰 P&L: $" << pnl_metrics.net_profit 
                          << " (" << (pnl_metrics.get_win_rate() * 100) << "% win rate)\n\n";
                
                LOG_INFO("Status update - Runtime: {}s, Market data: {}, Signals: {}, P&L: ${:.2f}", 
                        elapsed.count(), market_data_buffer.available(), 
                        signal_buffer.available(), pnl_metrics.net_profit);
            }
            
            // Small sleep to prevent excessive CPU usage on laptop
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Cleanup
        std::cout << "\n🛑 Shutting down systems...\n";
        
        // End session if still active
        if (session_manager.is_session_active()) {
            if (session_manager.is_emergency_stop()) {
                session_manager.end_session("Emergency stop triggered");
            } else {
                session_manager.end_session("User requested shutdown");
            }
        }
        
        monitor.trigger_emergency_stop("Graceful shutdown requested");
        monitor.stop_monitoring();
        feed_handler.stop();
        conn_manager.stop();
        
        auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
        
        LOG_INFO("HFT system shutdown complete - Runtime: {}s", total_runtime.count());
        
        // Final monitoring report
        const auto& final_pnl = monitor.get_profitability_metrics();
        auto recent_alerts = monitor.get_recent_alerts(5);
        
        std::cout << "\n✅ HFT MVP stopped gracefully\n";
        std::cout << "📊 Total runtime: " << total_runtime.count() << " seconds\n";
        std::cout << "💰 Final P&L: $" << final_pnl.net_profit 
                  << " (" << final_pnl.total_trades << " trades, " 
                  << (final_pnl.get_win_rate() * 100) << "% win rate)\n";
        std::cout << "📈 Max drawdown: $" << final_pnl.max_drawdown << "\n";
        
        if (!recent_alerts.empty()) {
            std::cout << "🚨 Final alerts (" << recent_alerts.size() << "):" << "\n";
            for (const auto& alert : recent_alerts) {
                std::cout << "   " << alert.to_string() << "\n";
            }
        }
        
        std::cout << "🎯 Thank you for testing the HFT monitoring system!\n";
        
        // Shutdown logger last
        hft::Logger::instance().shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}