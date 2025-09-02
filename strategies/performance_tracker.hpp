#pragma once

#include <vector>
#include <fstream>
#include <string>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include "intraday_strategies.hpp"
#include "simple_executor.hpp"

namespace hft::strategies {

// Individual trade record
struct TradeRecord {
    uint64_t timestamp;
    std::string strategy;
    std::string symbol;
    Signal::Direction direction;
    double entry_price;
    double exit_price;
    double quantity;
    double gross_pnl;
    double commission;
    double net_pnl;
    double hold_time_seconds;
    double slippage_bps;
    std::string notes;
    
    // Entry and exit order details
    std::string entry_order_id;
    std::string exit_order_id;
    uint64_t entry_time;
    uint64_t exit_time;
    
    TradeRecord() = default;
    
    TradeRecord(const Signal& signal, const OrderResult& entry_order, const OrderResult& exit_order)
        : timestamp(entry_order.fill_time)
        , strategy(signal.strategy_type)
        , symbol(signal.symbol)
        , direction(signal.direction)
        , entry_price(entry_order.fill_price)
        , exit_price(exit_order.fill_price)
        , quantity(entry_order.filled_quantity)
        , commission(entry_order.commission + exit_order.commission)
        , slippage_bps(entry_order.slippage_bps + exit_order.slippage_bps)
        , entry_order_id(entry_order.order_id)
        , exit_order_id(exit_order.order_id)
        , entry_time(entry_order.fill_time)
        , exit_time(exit_order.fill_time) {
        
        // Calculate P&L
        if (direction == Signal::LONG) {
            gross_pnl = (exit_price - entry_price) * quantity;
        } else {
            gross_pnl = (entry_price - exit_price) * quantity;
        }
        net_pnl = gross_pnl - commission;
        
        // Calculate hold time
        hold_time_seconds = static_cast<double>(exit_time - entry_time) / 1000000000.0;
    }
    
    bool is_winner() const { return net_pnl > 0; }
    bool is_loser() const { return net_pnl < 0; }
};

// Daily performance statistics
struct DailyStats {
    std::string date;
    double starting_capital;
    double ending_capital;
    uint32_t trades_taken;
    uint32_t winning_trades;
    uint32_t losing_trades;
    double gross_pnl;
    double commissions;
    double net_pnl;
    double max_drawdown;
    double largest_win;
    double largest_loss;
    double avg_win;
    double avg_loss;
    double win_rate;
    double profit_factor;
    double sharpe_ratio;
    double total_slippage_bps;
    
    // Strategy breakdown
    std::map<std::string, uint32_t> trades_by_strategy;
    std::map<std::string, double> pnl_by_strategy;
    
    DailyStats() : starting_capital(5000.0), ending_capital(5000.0), trades_taken(0),
                   winning_trades(0), losing_trades(0), gross_pnl(0.0), commissions(0.0),
                   net_pnl(0.0), max_drawdown(0.0), largest_win(0.0), largest_loss(0.0),
                   avg_win(0.0), avg_loss(0.0), win_rate(0.0), profit_factor(0.0),
                   sharpe_ratio(0.0), total_slippage_bps(0.0) {}
};

class PerformanceTracker {
private:
    std::vector<TradeRecord> trades_;
    std::ofstream trade_csv_file_;
    std::ofstream daily_csv_file_;
    
    // Running statistics
    double starting_capital_;
    double current_capital_;
    double peak_capital_;
    double max_drawdown_;
    std::vector<double> daily_returns_;
    
    // Configuration
    bool logging_enabled_ = true;
    std::string log_directory_ = "./";
    
public:
    explicit PerformanceTracker(double starting_capital = 5000.0) 
        : starting_capital_(starting_capital)
        , current_capital_(starting_capital)
        , peak_capital_(starting_capital)
        , max_drawdown_(0.0) {
        
        initialize_logging();
        
        printf("📊 Performance Tracker initialized\n");
        printf("   Starting Capital: $%.2f\n", starting_capital_);
        printf("   Trade Log: trades.csv\n");
        printf("   Daily Log: daily_performance.csv\n");
    }
    
    ~PerformanceTracker() {
        if (trade_csv_file_.is_open()) trade_csv_file_.close();
        if (daily_csv_file_.is_open()) daily_csv_file_.close();
    }
    
    // Record a completed trade
    void record_trade(const TradeRecord& trade) {
        trades_.push_back(trade);
        
        // Update running capital
        current_capital_ += trade.net_pnl;
        
        // Update peak capital and drawdown
        if (current_capital_ > peak_capital_) {
            peak_capital_ = current_capital_;
        } else {
            double current_drawdown = (peak_capital_ - current_capital_) / peak_capital_;
            max_drawdown_ = std::max(max_drawdown_, current_drawdown);
        }
        
        // Log to CSV
        if (logging_enabled_) {
            write_trade_to_csv(trade);
        }
        
        printf("📝 Trade recorded: %s %s %.2f shares @ $%.2f -> $%.2f (P&L: $%.2f)\n",
               trade.strategy.c_str(), trade.symbol.c_str(), trade.quantity,
               trade.entry_price, trade.exit_price, trade.net_pnl);
    }
    
    // Record trade from signal and order results
    void record_trade(const Signal& signal, const OrderResult& entry_order, const OrderResult& exit_order) {
        TradeRecord trade(signal, entry_order, exit_order);
        record_trade(trade);
    }
    
    // Generate comprehensive daily report
    void generate_daily_report() {
        DailyStats stats = calculate_daily_stats();
        
        printf("\n=== 📊 DAILY PERFORMANCE REPORT ===\n");
        printf("Date: %s\n", get_current_date().c_str());
        printf("\n💰 P&L Summary:\n");
        printf("  Starting Capital: $%.2f\n", stats.starting_capital);
        printf("  Ending Capital:   $%.2f\n", stats.ending_capital);
        printf("  Net P&L:          $%.2f (%.2f%%)\n", 
               stats.net_pnl, (stats.net_pnl / stats.starting_capital) * 100);
        printf("  Gross P&L:        $%.2f\n", stats.gross_pnl);
        printf("  Commissions:      $%.2f\n", stats.commissions);
        
        printf("\n📈 Trade Statistics:\n");
        printf("  Total Trades:     %u\n", stats.trades_taken);
        printf("  Winning Trades:   %u (%.1f%%)\n", stats.winning_trades, stats.win_rate);
        printf("  Losing Trades:    %u\n", stats.losing_trades);
        printf("  Average Win:      $%.2f\n", stats.avg_win);
        printf("  Average Loss:     $%.2f\n", stats.avg_loss);
        printf("  Largest Win:      $%.2f\n", stats.largest_win);
        printf("  Largest Loss:     $%.2f\n", stats.largest_loss);
        printf("  Profit Factor:    %.2f\n", stats.profit_factor);
        
        printf("\n📊 Risk Metrics:\n");
        printf("  Max Drawdown:     %.2f%%\n", stats.max_drawdown * 100);
        printf("  Sharpe Ratio:     %.2f\n", stats.sharpe_ratio);
        printf("  Avg Slippage:     %.1f bps\n", 
               stats.trades_taken > 0 ? stats.total_slippage_bps / stats.trades_taken : 0.0);
        
        if (!stats.trades_by_strategy.empty()) {
            printf("\n🎯 Strategy Breakdown:\n");
            for (const auto& [strategy, count] : stats.trades_by_strategy) {
                double strategy_pnl = stats.pnl_by_strategy.at(strategy);
                printf("  %s: %u trades, $%.2f P&L\n", 
                       strategy.c_str(), count, strategy_pnl);
            }
        }
        
        // Alert conditions
        if (stats.net_pnl < -80) {
            printf("\n⚠️  WARNING: Approaching daily loss limit!\n");
        }
        if (stats.max_drawdown > 0.04) { // 4%
            printf("\n⚠️  WARNING: High drawdown detected!\n");
        }
        
        printf("=====================================\n\n");
        
        // Write to daily CSV
        if (logging_enabled_) {
            write_daily_stats_to_csv(stats);
        }
    }
    
    // Get current performance metrics
    DailyStats get_current_stats() const {
        return calculate_daily_stats();
    }
    
    // Get trade history
    const std::vector<TradeRecord>& get_trades() const {
        return trades_;
    }
    
    // Get current capital
    double get_current_capital() const {
        return current_capital_;
    }
    
    // Get return since start
    double get_total_return_percent() const {
        return ((current_capital_ - starting_capital_) / starting_capital_) * 100.0;
    }
    
    // Configuration
    void set_logging_directory(const std::string& directory) {
        log_directory_ = directory;
    }
    
    void enable_logging(bool enabled) {
        logging_enabled_ = enabled;
    }
    
    // Reset for new session
    void reset_session(double new_starting_capital = 0.0) {
        if (new_starting_capital > 0) {
            starting_capital_ = new_starting_capital;
            current_capital_ = new_starting_capital;
        }
        
        trades_.clear();
        peak_capital_ = current_capital_;
        max_drawdown_ = 0.0;
        daily_returns_.clear();
        
        printf("🔄 Performance tracker reset. Starting Capital: $%.2f\n", current_capital_);
    }
    
private:
    void initialize_logging() {
        if (!logging_enabled_) return;
        
        // Initialize trade CSV
        trade_csv_file_.open(log_directory_ + "trades.csv", std::ios::app);
        if (trade_csv_file_.is_open()) {
            // Write header if file is new
            trade_csv_file_ << "timestamp,strategy,symbol,direction,entry_price,exit_price,"
                           << "quantity,gross_pnl,commission,net_pnl,hold_time_seconds,"
                           << "slippage_bps,entry_order_id,exit_order_id,notes\n";
        }
        
        // Initialize daily CSV
        daily_csv_file_.open(log_directory_ + "daily_performance.csv", std::ios::app);
        if (daily_csv_file_.is_open()) {
            daily_csv_file_ << "date,starting_capital,ending_capital,trades_taken,"
                           << "winning_trades,net_pnl,max_drawdown,win_rate,profit_factor,"
                           << "sharpe_ratio,largest_win,largest_loss,avg_slippage_bps\n";
        }
    }
    
    void write_trade_to_csv(const TradeRecord& trade) {
        if (!trade_csv_file_.is_open()) return;
        
        trade_csv_file_ << trade.timestamp << ","
                       << trade.strategy << ","
                       << trade.symbol << ","
                       << (trade.direction == Signal::LONG ? "LONG" : "SHORT") << ","
                       << std::fixed << std::setprecision(4) << trade.entry_price << ","
                       << trade.exit_price << ","
                       << trade.quantity << ","
                       << trade.gross_pnl << ","
                       << trade.commission << ","
                       << trade.net_pnl << ","
                       << trade.hold_time_seconds << ","
                       << trade.slippage_bps << ","
                       << trade.entry_order_id << ","
                       << trade.exit_order_id << ","
                       << trade.notes << "\n";
        
        trade_csv_file_.flush();
    }
    
    void write_daily_stats_to_csv(const DailyStats& stats) {
        if (!daily_csv_file_.is_open()) return;
        
        daily_csv_file_ << stats.date << ","
                       << std::fixed << std::setprecision(2) << stats.starting_capital << ","
                       << stats.ending_capital << ","
                       << stats.trades_taken << ","
                       << stats.winning_trades << ","
                       << stats.net_pnl << ","
                       << (stats.max_drawdown * 100) << ","
                       << stats.win_rate << ","
                       << stats.profit_factor << ","
                       << stats.sharpe_ratio << ","
                       << stats.largest_win << ","
                       << stats.largest_loss << ","
                       << (stats.trades_taken > 0 ? stats.total_slippage_bps / stats.trades_taken : 0.0) << "\n";
        
        daily_csv_file_.flush();
    }
    
    DailyStats calculate_daily_stats() const {
        DailyStats stats;
        stats.date = get_current_date();
        stats.starting_capital = starting_capital_;
        stats.ending_capital = current_capital_;
        stats.net_pnl = current_capital_ - starting_capital_;
        stats.max_drawdown = max_drawdown_;
        
        if (trades_.empty()) {
            return stats;
        }
        
        stats.trades_taken = trades_.size();
        
        // Calculate trade statistics
        double total_gross_pnl = 0.0;
        double total_commissions = 0.0;
        double total_slippage = 0.0;
        double total_wins = 0.0;
        double total_losses = 0.0;
        
        for (const auto& trade : trades_) {
            total_gross_pnl += trade.gross_pnl;
            total_commissions += trade.commission;
            total_slippage += trade.slippage_bps;
            
            if (trade.is_winner()) {
                stats.winning_trades++;
                total_wins += trade.net_pnl;
                stats.largest_win = std::max(stats.largest_win, trade.net_pnl);
            } else if (trade.is_loser()) {
                stats.losing_trades++;
                total_losses += std::abs(trade.net_pnl);
                stats.largest_loss = std::min(stats.largest_loss, trade.net_pnl);
            }
            
            // Strategy breakdown
            stats.trades_by_strategy[trade.strategy]++;
            stats.pnl_by_strategy[trade.strategy] += trade.net_pnl;
        }
        
        stats.gross_pnl = total_gross_pnl;
        stats.commissions = total_commissions;
        stats.total_slippage_bps = total_slippage;
        
        // Calculate derived metrics
        if (stats.trades_taken > 0) {
            stats.win_rate = static_cast<double>(stats.winning_trades) / stats.trades_taken * 100.0;
        }
        
        if (stats.winning_trades > 0) {
            stats.avg_win = total_wins / stats.winning_trades;
        }
        
        if (stats.losing_trades > 0) {
            stats.avg_loss = total_losses / stats.losing_trades;
            stats.profit_factor = total_wins / total_losses;
        }
        
        // Calculate Sharpe ratio (simplified)
        if (stats.trades_taken > 1) {
            std::vector<double> returns;
            for (const auto& trade : trades_) {
                double return_pct = trade.net_pnl / starting_capital_;
                returns.push_back(return_pct);
            }
            
            double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double variance = 0.0;
            for (double ret : returns) {
                variance += (ret - mean_return) * (ret - mean_return);
            }
            variance /= returns.size();
            
            double std_dev = std::sqrt(variance);
            stats.sharpe_ratio = std_dev > 0 ? (mean_return / std_dev) * std::sqrt(252) : 0.0; // Annualized
        }
        
        return stats;
    }
    
    std::string get_current_date() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d");
        return ss.str();
    }
};

} // namespace hft::strategies
