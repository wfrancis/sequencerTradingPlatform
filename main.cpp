#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include "messages.hpp"
#include "ring_buffer.hpp"
#include "sequencer.hpp"
#include "position_tracker.hpp"
#include "logger.hpp"
#include "connection_manager.hpp"
#include "strategies/intraday_strategies.hpp"
#include "strategies/intraday_risk_manager.hpp"
#include "strategies/enhanced_feed_handler.hpp"
#include "strategies/simple_executor.hpp"
#include "strategies/performance_tracker.hpp"

using namespace hft;
using namespace hft::strategies;

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signal_handler(int signum) {
    std::cout << "\n🛑 Received signal " << signum << ", shutting down gracefully...\n";
    running.store(false);
}

int main() {
    std::cout << "🚀 Intraday Trading System Starting...\n";
    std::cout << "📊 Trading SPY, QQQ, AAPL on NYSE\n";
    std::cout << "🎯 Strategies: VWAP Reversion, Opening Drive, Pair Divergence\n";
    std::cout << "💡 Press Ctrl+C to stop\n\n";
    
    // Set up signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize logging
        hft::Logger::instance().init();
        LOG_INFO("Intraday Trading System starting");
        
        // Initialize position tracker and risk manager
        PositionTracker position_tracker;
        IntradayRiskManager risk_manager(&position_tracker);
        
        std::cout << "⚙️  Intraday Configuration:\n";
        std::cout << "   - Starting Capital: $" << risk_manager.get_limits().capital << "\n";
        std::cout << "   - Daily Loss Limit: $" << risk_manager.get_limits().daily_loss_limit << "\n";
        std::cout << "   - Max Trades/Day: " << risk_manager.get_limits().max_trades_per_day << "\n";
        std::cout << "   - Symbols: SPY, QQQ, AAPL\n\n";
        
        // Initialize core components
        IntradayStrategyEngine strategy_engine;
        SimpleExecutor executor;
        PerformanceTracker tracker(risk_manager.get_limits().capital);
        EnhancedMarketDataFeed feed_handler;
        
        LOG_INFO("Core components initialized");
        
        // Set up market data callback
        std::vector<Signal> pending_signals;
        feed_handler.set_data_callback([&](const EnhancedMarketData& data) {
            // Process market data through strategy engine
            auto signal = strategy_engine.process_market_data(data);
            if (signal.is_valid && signal.confidence > 0.7) {
                pending_signals.push_back(signal);
            }
            
            // Update executor with current market data
            executor.update_market_data(data.symbol, data.bid, data.ask);
        });
        
        std::cout << "🛡️ Intraday risk management initialized\n";
        std::cout << "📊 Position tracking enabled\n";
        std::cout << "📈 Performance tracking enabled\n";
        std::cout << "💾 Trade logging: trades.csv\n";
        std::cout << "📋 Risk events: intraday_risk.csv\n";
        std::cout << "📝 Daily performance: daily_performance.csv\n\n";
        
        // Start market data feed
        feed_handler.start();
        
        std::cout << "✅ Market data feed started\n";
        std::cout << "✅ Strategy engine ready\n";
        std::cout << "✅ Risk management active\n";
        
        // Main intraday trading loop
        auto start_time = std::chrono::steady_clock::now();
        auto last_status_print = start_time;
        auto last_report_time = start_time;
        
        std::cout << "✅ Intraday trading loop started\n";
        std::cout << "🎯 Target: $60 daily profit, $100 max loss\n";
        std::cout << "⏰ Session timeout: 6 hours\n";
        std::cout << "🛑 Emergency stop: Ctrl+C or risk limits\n\n";
        
        while (running.load() && risk_manager.can_trade()) {
            // Process any pending signals from market data callback
            for (auto it = pending_signals.begin(); it != pending_signals.end();) {
                const auto& signal = *it;
                
                // Risk check before execution
                if (risk_manager.can_trade(signal)) {
                    // Calculate position size
                    auto position_size = risk_manager.calculate_position_size(signal);
                    
                    if (position_size.is_valid) {
                        std::cout << "🎯 " << signal.strategy_type << " Signal: " 
                                  << signal.symbol << " " 
                                  << (signal.direction == Signal::LONG ? "LONG" : "SHORT")
                                  << " @ $" << std::fixed << std::setprecision(2) << signal.entry_price
                                  << " (confidence: " << signal.confidence << ")\n";
                        
                        // Execute entry order
                        auto entry_order = executor.execute_signal(signal, position_size.shares);
                        
                        if (entry_order.is_filled()) {
                            std::cout << "   ✅ Entry filled: " << entry_order.filled_quantity 
                                      << " shares @ $" << entry_order.fill_price 
                                      << " (slippage: " << entry_order.slippage_bps << " bps)\n";
                            
                            // Simulate holding period (in real system, this would be handled differently)
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            
                            // Create exit signal (simplified - would use exit conditions)
                            Signal exit_signal = signal;
                            exit_signal.direction = (signal.direction == Signal::LONG) ? Signal::SHORT : Signal::LONG;
                            exit_signal.entry_price = signal.target_price;
                            
                            // Execute exit order
                            auto exit_order = executor.execute_signal(exit_signal, position_size.shares);
                            
                            if (exit_order.is_filled()) {
                                std::cout << "   ✅ Exit filled: " << exit_order.filled_quantity 
                                          << " shares @ $" << exit_order.fill_price << "\n";
                                
                                // Calculate P&L and update tracking
                                double pnl = executor.calculate_pnl(entry_order, exit_order);
                                std::cout << "   💰 Trade P&L: $" << pnl << "\n";
                                
                                // Record trade
                                risk_manager.update_trade_result(signal, position_size, pnl, 
                                                                entry_order.commission + exit_order.commission);
                                tracker.record_trade(signal, entry_order, exit_order);
                                
                                // Update position tracker
                                position_tracker.add_trade("NYSE", signal.symbol, 
                                                          entry_order.filled_quantity * 
                                                          (signal.direction == Signal::LONG ? 1 : -1), 
                                                          entry_order.fill_price);
                                position_tracker.add_trade("NYSE", signal.symbol, 
                                                          exit_order.filled_quantity * 
                                                          (exit_signal.direction == Signal::LONG ? 1 : -1), 
                                                          exit_order.fill_price);
                            }
                        } else {
                            std::cout << "   ❌ Entry order rejected: " << entry_order.rejection_reason << "\n";
                        }
                    } else {
                        std::cout << "   ❌ Position sizing failed: " << position_size.reason << "\n";
                    }
                } else {
                    std::cout << "   ❌ Trade blocked by risk manager\n";
                }
                
                it = pending_signals.erase(it);
            }
            
            // Print status every 30 seconds
            auto now = std::chrono::steady_clock::now();
            if (now - last_status_print >= std::chrono::seconds(30)) {
                std::cout << "\n📊 === INTRADAY STATUS UPDATE ===\n";
                
                // Print performance stats
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
                std::cout << "⏱️  Runtime: " << elapsed.count() << "s\n";
                std::cout << "💰 Current P&L: $" << risk_manager.get_daily_pnl() << "\n";
                std::cout << "📊 Trades Today: " << risk_manager.get_trades_today() << "/" 
                         << risk_manager.get_limits().max_trades_per_day << "\n";
                std::cout << "💼 Current Capital: $" << tracker.get_current_capital() << "\n";
                
                // Show feed status
                feed_handler.print_status();
                executor.print_status();
                risk_manager.print_risk_status();
                
                last_status_print = now;
                
                LOG_INFO("Status update - Runtime: {}s, P&L: ${:.2f}, Trades: {}", 
                        elapsed.count(), risk_manager.get_daily_pnl(), risk_manager.get_trades_today());
            }
            
            // Generate daily report every hour
            if (now - last_report_time >= std::chrono::seconds(3600)) {
                tracker.generate_daily_report();
                last_report_time = now;
            }
            
            // Sleep for 1 second (intraday trading doesn't need HFT speed)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Cleanup and final reporting
        std::cout << "\n🛑 Shutting down intraday trading system...\n";
        
        feed_handler.stop();
        
        auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time);
        
        LOG_INFO("Intraday trading system shutdown complete - Runtime: {}s", total_runtime.count());
        
        // Generate final performance report
        tracker.generate_daily_report();
        
        std::cout << "\n✅ Intraday Trading System stopped gracefully\n";
        std::cout << "📊 Total runtime: " << total_runtime.count() << " seconds\n";
        std::cout << "💰 Final P&L: $" << risk_manager.get_daily_pnl() << "\n";
        std::cout << "📊 Total trades: " << risk_manager.get_trades_today() << "\n";
        std::cout << "💼 Final capital: $" << tracker.get_current_capital() << "\n";
        std::cout << "📈 Total return: " << tracker.get_total_return_percent() << "%\n";
        std::cout << "🎯 Thank you for testing the intraday trading system!\n";
        
        // Shutdown logger last
        hft::Logger::instance().shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}