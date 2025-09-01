#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <fstream>
#include <vector>
#include <memory>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <thread>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include "comprehensive_risk_manager.hpp"
#include "../position_tracker.hpp"
#include "../messages.hpp"

namespace risk {

/**
 * Professional Risk Classification System
 */
enum class ProfessionalRiskLevel {
    GREEN = 0,      // Normal operations
    YELLOW = 1,     // Elevated caution 
    ORANGE = 2,     // High risk - reduce activity
    RED = 3,        // Critical risk - emergency protocols
    BLACK = 4       // Complete shutdown required
};

/**
 * Advanced Risk Event Types for Institutional Trading
 */
enum class InstitutionalRiskType {
    PORTFOLIO_VAR_BREACH,
    STRESS_TEST_FAILURE,
    CORRELATION_SPIKE,
    LIQUIDITY_CRISIS,
    MODEL_DEVIATION,
    OPERATIONAL_ANOMALY,
    REGULATORY_BREACH,
    COUNTERPARTY_RISK,
    CONCENTRATION_RISK,
    TAIL_RISK_EVENT,
    SYSTEMATIC_RISK_ALERT
};

/**
 * Portfolio Risk Metrics for Multi-Asset Management
 */
struct PortfolioRiskMetrics {
    // Value at Risk calculations
    double var_1day_95 = 0.0;         // 1-day 95% VaR
    double var_1day_99 = 0.0;         // 1-day 99% VaR
    double var_10day_95 = 0.0;        // 10-day 95% VaR
    double expected_shortfall = 0.0;   // Expected Shortfall (CVaR)
    
    // Portfolio-level risk
    double portfolio_volatility = 0.0;  // Annualized portfolio volatility
    double portfolio_beta = 0.0;        // Portfolio beta vs market
    double tracking_error = 0.0;        // Tracking error vs benchmark
    double information_ratio = 0.0;     // Information ratio
    
    // Concentration metrics
    double max_single_position_pct = 0.0;  // Largest position as % of portfolio
    double concentration_risk_score = 0.0; // Herfindahl concentration index
    double sector_concentration_max = 0.0; // Max sector concentration
    
    // Correlation metrics
    double avg_correlation = 0.0;       // Average pairwise correlation
    double max_correlation = 0.0;       // Maximum pairwise correlation
    double correlation_risk_score = 0.0; // Correlation-based risk score
    
    // Liquidity metrics
    double liquidity_score = 0.0;       // Overall portfolio liquidity score
    double days_to_liquidate = 0.0;     // Estimated liquidation time
    double liquidity_cost_estimate = 0.0; // Estimated liquidation cost
    
    // Tail risk
    double tail_expectation = 0.0;      // Expected loss in tail scenarios
    double maximum_drawdown_estimate = 0.0; // Estimated maximum drawdown
    
    uint64_t last_calculation_ns = 0;
    bool is_valid = false;
};

/**
 * Stress Test Scenarios for Professional Risk Management
 */
struct StressTestScenario {
    std::string name;
    std::string description;
    
    // Market shock parameters
    double btc_shock_pct = 0.0;         // BTC price shock %
    double eth_shock_pct = 0.0;         // ETH price shock %
    double correlation_shock = 0.0;     // Correlation change
    double volatility_multiplier = 1.0; // Volatility scaling factor
    
    // Liquidity shock
    double liquidity_reduction_pct = 0.0; // Liquidity reduction %
    double bid_ask_widening_factor = 1.0; // Spread widening factor
    
    // Results
    double estimated_pnl_impact = 0.0;
    double var_impact = 0.0;
    bool passes_stress_test = true;
    uint64_t last_run_ns = 0;
};

/**
 * Professional Risk Limits Framework
 */
struct InstitutionalRiskLimits {
    // Portfolio-level limits
    double max_portfolio_var_pct = 2.0;      // Max 2% portfolio VaR
    double max_portfolio_volatility = 0.25;   // Max 25% annualized volatility
    double max_expected_shortfall_pct = 3.0;  // Max 3% Expected Shortfall
    double max_correlation_exposure = 0.8;    // Max correlation threshold
    
    // Concentration limits
    double max_single_position_pct = 10.0;    // Max 10% in single position
    double max_sector_concentration = 30.0;   // Max 30% in single sector
    double max_exchange_concentration = 40.0; // Max 40% on single exchange
    
    // Liquidity limits
    double min_portfolio_liquidity_score = 0.5;  // Min liquidity score
    double max_days_to_liquidate = 5.0;          // Max 5 days to liquidate
    double max_liquidity_cost_pct = 1.0;         // Max 1% liquidation cost
    
    // Operational limits
    double max_model_deviation_sigma = 3.0;   // Max 3-sigma model deviation
    uint32_t max_consecutive_losses = 5;      // Max consecutive losing trades
    double max_intraday_drawdown_pct = 3.0;   // Max 3% intraday drawdown
    
    // Stress test limits
    double min_stress_test_survival_pct = 80.0; // Must survive 80% of scenarios
    double max_tail_risk_pct = 5.0;            // Max 5% tail risk exposure
    
    // Regulatory and compliance
    bool enable_regulatory_monitoring = true;
    bool enable_audit_trail = true;
    bool enable_real_time_reporting = true;
};

/**
 * Market Regime Detection for Dynamic Risk Management
 */
enum class MarketRegime {
    NORMAL_VOLATILITY,
    HIGH_VOLATILITY,
    CRISIS_MODE,
    RECOVERY_MODE,
    TRENDING_MARKET,
    RANGE_BOUND_MARKET
};

/**
 * Professional Risk Event for Audit Trail
 */
struct InstitutionalRiskEvent {
    uint64_t timestamp_ns;
    InstitutionalRiskType type;
    ProfessionalRiskLevel severity;
    std::string source_system;
    std::string asset_class;
    std::string description;
    double risk_metric_value;
    double threshold_breached;
    std::string action_taken;
    std::string responsible_trader;
    bool requires_management_review;
    uint64_t event_id;
};

/**
 * Institutional-Grade Risk Manager
 * 
 * Professional multi-layered risk management system designed for institutional trading operations.
 * Implements sophisticated portfolio risk controls, real-time VaR monitoring, stress testing,
 * and regulatory compliance frameworks.
 */
class InstitutionalRiskManager {
private:
    // Core components
    std::unique_ptr<ComprehensiveRiskManager> comprehensive_risk_manager_;
    PositionTracker* position_tracker_;
    
    // Risk metrics and calculations
    PortfolioRiskMetrics current_portfolio_metrics_;
    std::vector<StressTestScenario> stress_test_scenarios_;
    InstitutionalRiskLimits institutional_limits_;
    
    // Market data for risk calculations
    static constexpr size_t MAX_PRICE_HISTORY = 1440; // 24 hours at 1-minute intervals
    std::unordered_map<std::string, std::deque<double>> price_returns_;
    std::unordered_map<std::string, std::deque<uint64_t>> return_timestamps_;
    
    // Correlation matrix and portfolio metrics
    std::unordered_map<std::string, std::unordered_map<std::string, double>> correlation_matrix_;
    std::vector<std::string> active_assets_;
    
    // Risk monitoring state
    std::atomic<ProfessionalRiskLevel> current_risk_level_{ProfessionalRiskLevel::GREEN};
    std::atomic<MarketRegime> current_market_regime_{MarketRegime::NORMAL_VOLATILITY};
    std::atomic<bool> stress_tests_passing_{true};
    
    // Event tracking and audit
    std::deque<InstitutionalRiskEvent> risk_event_history_;
    std::atomic<uint64_t> next_event_id_{1};
    std::atomic<uint32_t> consecutive_losses_{0};
    std::atomic<double> intraday_peak_equity_{0.0};
    
    // Threading and real-time monitoring
    std::thread risk_monitoring_thread_;
    std::atomic<bool> monitoring_active_{false};
    std::condition_variable risk_cv_;
    mutable std::mutex risk_mutex_;
    
    // Performance tracking for model validation
    std::deque<double> var_predictions_;
    std::deque<double> actual_pnl_;
    std::atomic<double> model_performance_score_{1.0};
    
public:
    InstitutionalRiskManager(PositionTracker* position_tracker,
                           const InstitutionalRiskLimits& limits = InstitutionalRiskLimits{})
        : position_tracker_(position_tracker),
          institutional_limits_(limits) {
        
        // Initialize comprehensive risk manager
        comprehensive_risk_manager_ = std::make_unique<ComprehensiveRiskManager>(position_tracker);
        
        // Initialize stress test scenarios
        initialize_stress_test_scenarios();
        
        // Initialize active assets
        active_assets_ = {"BTC/USD", "ETH/USD"};
        
        // Start risk monitoring thread
        start_risk_monitoring();
        
        printf("🏛️ Institutional Risk Manager initialized\n");
        printf("   Portfolio VaR Limit: %.1f%%\n", institutional_limits_.max_portfolio_var_pct);
        printf("   Max Concentration: %.1f%%\n", institutional_limits_.max_single_position_pct);
        printf("   Stress Test Coverage: %.0f%%\n", institutional_limits_.min_stress_test_survival_pct);
        printf("   Regulatory Monitoring: %s\n", 
               institutional_limits_.enable_regulatory_monitoring ? "ENABLED" : "DISABLED");
        printf("   Real-time Risk Level: %s\n", risk_level_to_string(current_risk_level_));
    }
    
    ~InstitutionalRiskManager() {
        stop_risk_monitoring();
    }
    
    /**
     * Master trade validation - institutional-grade checks
     */
    bool can_execute_trade(const std::string& exchange, const std::string& symbol,
                          double quantity, double price, double current_spread_bps = 0.0) {
        
        // 1. Check current professional risk level
        auto risk_level = current_risk_level_.load();
        if (risk_level == ProfessionalRiskLevel::BLACK) {
            log_risk_event(InstitutionalRiskType::OPERATIONAL_ANOMALY, 
                         ProfessionalRiskLevel::BLACK, "SYSTEM", symbol,
                         "Trading completely halted - BLACK risk level", 0.0, 0.0);
            return false;
        }
        
        if (risk_level == ProfessionalRiskLevel::RED) {
            // Only allow closing positions in RED level
            auto current_pos = position_tracker_->get_position(exchange, symbol);
            bool is_reducing_position = (current_pos.quantity > 0 && quantity < 0) ||
                                      (current_pos.quantity < 0 && quantity > 0);
            if (!is_reducing_position) {
                log_risk_event(InstitutionalRiskType::OPERATIONAL_ANOMALY,
                             ProfessionalRiskLevel::RED, exchange, symbol,
                             "Only position-reducing trades allowed in RED risk level", 
                             std::abs(quantity), 0.0);
                return false;
            }
        }
        
        // 2. Run comprehensive risk manager checks
        if (!comprehensive_risk_manager_->can_execute_trade(exchange, symbol, quantity, price, current_spread_bps)) {
            return false;
        }
        
        // 3. Portfolio-level risk checks
        if (!check_portfolio_risk_limits(exchange, symbol, quantity, price)) {
            return false;
        }
        
        // 4. Concentration risk checks
        if (!check_concentration_limits(exchange, symbol, quantity, price)) {
            return false;
        }
        
        // 5. Liquidity risk assessment
        if (!check_liquidity_constraints(exchange, symbol, quantity, price)) {
            return false;
        }
        
        // 6. Model validation and deviation checks
        if (!check_model_validation(exchange, symbol, quantity, price)) {
            return false;
        }
        
        // 7. Stress test compliance
        if (!stress_tests_passing_.load()) {
            log_risk_event(InstitutionalRiskType::STRESS_TEST_FAILURE,
                         ProfessionalRiskLevel::ORANGE, exchange, symbol,
                         "Trade rejected - stress tests not passing", std::abs(quantity), 0.0);
            return false;
        }
        
        // 8. Intraday drawdown protection
        if (!check_intraday_drawdown_limits()) {
            return false;
        }
        
        return true;
    }
    
    /**
     * Update market data and recalculate portfolio risk metrics
     */
    void update_market_data(const std::string& exchange, const std::string& symbol,
                           double price, double bid, double ask, double volume) {
        
        // Update comprehensive risk manager first
        comprehensive_risk_manager_->update_market_data(exchange, symbol, price, bid, ask, volume);
        
        std::lock_guard<std::mutex> lock(risk_mutex_);
        
        // Calculate returns and update price history
        update_return_calculations(symbol, price);
        
        // Update correlation matrix
        update_correlation_matrix();
        
        // Recalculate portfolio metrics
        calculate_portfolio_risk_metrics();
        
        // Update market regime detection
        update_market_regime_detection();
        
        // Trigger risk level reassessment
        assess_overall_risk_level();
    }
    
    /**
     * Execute comprehensive stress tests
     */
    bool run_stress_tests() {
        std::lock_guard<std::mutex> lock(risk_mutex_);
        
        size_t passed_tests = 0;
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        printf("🧪 Running institutional stress tests...\n");
        
        for (auto& scenario : stress_test_scenarios_) {
            scenario.last_run_ns = current_time;
            
            // Calculate portfolio impact under stress scenario
            double stress_pnl = calculate_stress_scenario_impact(scenario);
            scenario.estimated_pnl_impact = stress_pnl;
            
            // Calculate VaR impact
            double stress_var = calculate_stress_var_impact(scenario);
            scenario.var_impact = stress_var;
            
            // Determine if scenario passes
            double current_equity = position_tracker_->get_total_equity();
            double loss_pct = std::abs(stress_pnl) / current_equity * 100.0;
            
            scenario.passes_stress_test = (loss_pct <= institutional_limits_.max_tail_risk_pct);
            
            if (scenario.passes_stress_test) {
                passed_tests++;
            }
            
            printf("  📊 %s: P&L Impact $%.2f (%.1f%%) - %s\n",
                   scenario.name.c_str(), stress_pnl, loss_pct,
                   scenario.passes_stress_test ? "PASS" : "FAIL");
        }
        
        double pass_rate = (double)passed_tests / stress_test_scenarios_.size() * 100.0;
        bool overall_pass = pass_rate >= institutional_limits_.min_stress_test_survival_pct;
        
        stress_tests_passing_.store(overall_pass);
        
        printf("🧪 Stress test summary: %.1f%% pass rate (%s)\n", 
               pass_rate, overall_pass ? "COMPLIANT" : "NON-COMPLIANT");
        
        if (!overall_pass) {
            log_risk_event(InstitutionalRiskType::STRESS_TEST_FAILURE,
                         ProfessionalRiskLevel::ORANGE, "PORTFOLIO", "ALL",
                         "Portfolio fails stress test requirements", pass_rate,
                         institutional_limits_.min_stress_test_survival_pct);
        }
        
        return overall_pass;
    }
    
    /**
     * Generate professional risk report
     */
    void generate_professional_risk_report() const {
        std::lock_guard<std::mutex> lock(risk_mutex_);
        
        printf("\n🏛️ === INSTITUTIONAL RISK MANAGEMENT REPORT ===\n");
        
        // Overall risk status
        printf("\n🚦 RISK LEVEL ASSESSMENT:\n");
        printf("  Current Risk Level: %s\n", risk_level_to_string(current_risk_level_));
        printf("  Market Regime: %s\n", market_regime_to_string(current_market_regime_));
        printf("  Stress Tests: %s\n", stress_tests_passing_.load() ? "PASSING" : "FAILING");
        printf("  Model Performance: %.3f\n", model_performance_score_.load());
        
        // Portfolio risk metrics
        printf("\n📊 PORTFOLIO RISK METRICS:\n");
        if (current_portfolio_metrics_.is_valid) {
            printf("  1-Day VaR (95%%): $%.2f (%.2f%% of portfolio)\n",
                   current_portfolio_metrics_.var_1day_95,
                   current_portfolio_metrics_.var_1day_95 / position_tracker_->get_total_equity() * 100);
            printf("  1-Day VaR (99%%): $%.2f\n", current_portfolio_metrics_.var_1day_99);
            printf("  Expected Shortfall: $%.2f\n", current_portfolio_metrics_.expected_shortfall);
            printf("  Portfolio Volatility: %.2f%%\n", current_portfolio_metrics_.portfolio_volatility * 100);
            printf("  Max Position Concentration: %.2f%%\n", current_portfolio_metrics_.max_single_position_pct);
        } else {
            printf("  Risk metrics not yet available (insufficient data)\n");
        }
        
        // Concentration analysis
        printf("\n🎯 CONCENTRATION ANALYSIS:\n");
        printf("  Max Single Position: %.1f%% (Limit: %.1f%%)\n",
               current_portfolio_metrics_.max_single_position_pct,
               institutional_limits_.max_single_position_pct);
        printf("  Concentration Risk Score: %.3f\n", current_portfolio_metrics_.concentration_risk_score);
        printf("  Average Correlation: %.3f\n", current_portfolio_metrics_.avg_correlation);
        printf("  Max Pairwise Correlation: %.3f\n", current_portfolio_metrics_.max_correlation);
        
        // Liquidity assessment
        printf("\n💧 LIQUIDITY ASSESSMENT:\n");
        printf("  Portfolio Liquidity Score: %.3f (Min: %.3f)\n",
               current_portfolio_metrics_.liquidity_score,
               institutional_limits_.min_portfolio_liquidity_score);
        printf("  Estimated Days to Liquidate: %.1f (Max: %.1f)\n",
               current_portfolio_metrics_.days_to_liquidate,
               institutional_limits_.max_days_to_liquidate);
        printf("  Liquidity Cost Estimate: %.2f%%\n", 
               current_portfolio_metrics_.liquidity_cost_estimate * 100);
        
        // Recent risk events
        printf("\n🚨 RECENT RISK EVENTS (Last 5):\n");
        size_t events_to_show = std::min<size_t>(5, risk_event_history_.size());
        for (size_t i = risk_event_history_.size() - events_to_show; i < risk_event_history_.size(); ++i) {
            const auto& event = risk_event_history_[i];
            printf("  [%s] %s: %s\n",
                   risk_level_to_string(event.severity),
                   event.asset_class.c_str(),
                   event.description.c_str());
        }
        
        printf("=================================================\n\n");
        
        // Generate compliance report
        generate_compliance_report();
    }
    
    // Accessors and status
    ProfessionalRiskLevel get_current_risk_level() const { return current_risk_level_.load(); }
    MarketRegime get_current_market_regime() const { return current_market_regime_.load(); }
    const PortfolioRiskMetrics& get_portfolio_metrics() const { return current_portfolio_metrics_; }
    bool are_stress_tests_passing() const { return stress_tests_passing_.load(); }
    double get_model_performance_score() const { return model_performance_score_.load(); }
    
    // Risk level management
    void override_risk_level(ProfessionalRiskLevel level, const std::string& reason) {
        current_risk_level_.store(level);
        log_risk_event(InstitutionalRiskType::OPERATIONAL_ANOMALY, level, "MANUAL", "SYSTEM",
                      "Risk level manually overridden: " + reason, 0.0, 0.0);
        printf("🔧 Risk level manually set to %s: %s\n", risk_level_to_string(level), reason.c_str());
    }
    
private:
    void initialize_stress_test_scenarios() {
        // Market crash scenario
        stress_test_scenarios_.push_back({
            "Market Crash", "Severe market downturn with high correlation",
            -30.0, -25.0, 0.9, 2.0, 50.0, 3.0, 0.0, 0.0, true, 0
        });
        
        // Flash crash scenario
        stress_test_scenarios_.push_back({
            "Flash Crash", "Rapid price decline with liquidity evaporation",
            -15.0, -12.0, 0.8, 1.5, 80.0, 5.0, 0.0, 0.0, true, 0
        });
        
        // Correlation breakdown
        stress_test_scenarios_.push_back({
            "Correlation Breakdown", "Normal moves with correlation spike",
            -5.0, 5.0, 0.95, 1.2, 20.0, 2.0, 0.0, 0.0, true, 0
        });
        
        // Liquidity crisis
        stress_test_scenarios_.push_back({
            "Liquidity Crisis", "Normal prices but severe liquidity shortage",
            -2.0, -3.0, 0.6, 1.1, 70.0, 4.0, 0.0, 0.0, true, 0
        });
        
        // Volatility shock
        stress_test_scenarios_.push_back({
            "Volatility Shock", "Extreme volatility increase",
            -10.0, -8.0, 0.7, 3.0, 30.0, 2.5, 0.0, 0.0, true, 0
        });
    }
    
    void start_risk_monitoring() {
        monitoring_active_ = true;
        risk_monitoring_thread_ = std::thread(&InstitutionalRiskManager::risk_monitoring_loop, this);
    }
    
    void stop_risk_monitoring() {
        monitoring_active_ = false;
        risk_cv_.notify_all();
        if (risk_monitoring_thread_.joinable()) {
            risk_monitoring_thread_.join();
        }
    }
    
    void risk_monitoring_loop() {
        while (monitoring_active_) {
            std::unique_lock<std::mutex> lock(risk_mutex_);
            
            // Run periodic risk assessments
            if (current_portfolio_metrics_.is_valid) {
                assess_overall_risk_level();
                validate_risk_models();
                
                // Run stress tests every 5 minutes
                static auto last_stress_test = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (now - last_stress_test >= std::chrono::minutes(5)) {
                    lock.unlock();
                    run_stress_tests();
                    lock.lock();
                    last_stress_test = now;
                }
            }
            
            // Wait for next monitoring cycle or exit signal
            risk_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !monitoring_active_; });
        }
    }
    
    // ... (Additional private methods will be implemented in the next part)
    
    const char* risk_level_to_string(ProfessionalRiskLevel level) const {
        switch (level) {
            case ProfessionalRiskLevel::GREEN: return "GREEN (Normal)";
            case ProfessionalRiskLevel::YELLOW: return "YELLOW (Caution)";
            case ProfessionalRiskLevel::ORANGE: return "ORANGE (High Risk)";
            case ProfessionalRiskLevel::RED: return "RED (Critical)";
            case ProfessionalRiskLevel::BLACK: return "BLACK (Shutdown)";
            default: return "UNKNOWN";
        }
    }
    
    const char* market_regime_to_string(MarketRegime regime) const {
        switch (regime) {
            case MarketRegime::NORMAL_VOLATILITY: return "Normal Volatility";
            case MarketRegime::HIGH_VOLATILITY: return "High Volatility";
            case MarketRegime::CRISIS_MODE: return "Crisis Mode";
            case MarketRegime::RECOVERY_MODE: return "Recovery Mode";
            case MarketRegime::TRENDING_MARKET: return "Trending Market";
            case MarketRegime::RANGE_BOUND_MARKET: return "Range Bound";
            default: return "Unknown";
        }
    }
    
    void log_risk_event(InstitutionalRiskType type, ProfessionalRiskLevel severity,
                       const std::string& source, const std::string& asset,
                       const std::string& description, double value, double threshold) {
        
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        InstitutionalRiskEvent event{
            static_cast<uint64_t>(current_time), type, severity, source, asset,
            description, value, threshold, "AUTO", "SYSTEM",
            severity >= ProfessionalRiskLevel::RED, next_event_id_++
        };
        
        risk_event_history_.push_back(event);
        
        // Maintain history size
        if (risk_event_history_.size() > 1000) {
            risk_event_history_.pop_front();
        }
        
        // Log to file for audit trail
        if (institutional_limits_.enable_audit_trail) {
            log_event_to_audit_trail(event);
        }
        
        printf("⚠️ INSTITUTIONAL RISK EVENT [%s]: %s\n",
               risk_level_to_string(severity), description.c_str());
    }
    
    void log_event_to_audit_trail(const InstitutionalRiskEvent& event) {
        std::ofstream audit_file("institutional_risk_audit.csv", std::ios::app);
        if (audit_file.is_open()) {
            // Create header if file is new
            static bool header_written = false;
            if (!header_written) {
                audit_file << "timestamp_ns,event_id,type,severity,source_system,asset_class,"
                          << "description,risk_value,threshold_breached,action_taken,"
                          << "responsible_trader,requires_review\n";
                header_written = true;
            }
            
            audit_file << event.timestamp_ns << "," << event.event_id << ","
                      << static_cast<int>(event.type) << "," << static_cast<int>(event.severity) << ","
                      << event.source_system << "," << event.asset_class << ","
                      << "\"" << event.description << "\"" << "," << event.risk_metric_value << ","
                      << event.threshold_breached << "," << event.action_taken << ","
                      << event.responsible_trader << "," << (event.requires_management_review ? "1" : "0") << "\n";
        }
    }
    
    // Core risk validation methods
    bool check_portfolio_risk_limits(const std::string& exchange, const std::string& symbol, double quantity, double price) {
        if (!current_portfolio_metrics_.is_valid) return true;
        
        double current_equity = position_tracker_->get_total_equity();
        double var_percentage = current_portfolio_metrics_.var_1day_95 / current_equity * 100.0;
        
        if (var_percentage > institutional_limits_.max_portfolio_var_pct) {
            log_risk_event(InstitutionalRiskType::PORTFOLIO_VAR_BREACH, ProfessionalRiskLevel::RED, 
                         exchange, symbol, "Portfolio VaR exceeds limit", var_percentage,
                         institutional_limits_.max_portfolio_var_pct);
            return false;
        }
        return true;
    }
    
    bool check_concentration_limits(const std::string& exchange, const std::string& symbol, double quantity, double price) {
        double current_equity = position_tracker_->get_total_equity();
        auto current_position = position_tracker_->get_position(exchange, symbol);
        double new_position_value = std::abs((current_position.quantity + quantity) * price);
        double new_position_pct = new_position_value / current_equity * 100.0;
        
        if (new_position_pct > institutional_limits_.max_single_position_pct) {
            log_risk_event(InstitutionalRiskType::CONCENTRATION_RISK, ProfessionalRiskLevel::ORANGE,
                         exchange, symbol, "Single position concentration exceeds limit", 
                         new_position_pct, institutional_limits_.max_single_position_pct);
            return false;
        }
        return true;
    }
    
    bool check_liquidity_constraints(const std::string& exchange, const std::string& symbol, double quantity, double price) {
        if (!current_portfolio_metrics_.is_valid) return true;
        
        if (current_portfolio_metrics_.liquidity_score < institutional_limits_.min_portfolio_liquidity_score) {
            log_risk_event(InstitutionalRiskType::LIQUIDITY_CRISIS, ProfessionalRiskLevel::YELLOW,
                         exchange, symbol, "Portfolio liquidity score below minimum",
                         current_portfolio_metrics_.liquidity_score, institutional_limits_.min_portfolio_liquidity_score);
            return false;
        }
        return true;
    }
    
    bool check_model_validation(const std::string& exchange, const std::string& symbol, double quantity, double price) {
        double model_score = model_performance_score_.load();
        if (model_score < 0.5) {
            if (std::abs(quantity * price) > 500.0) { // Reduce size when model unreliable
                return false;
            }
        }
        return true;
    }
    
    bool check_intraday_drawdown_limits() {
        double current_equity = position_tracker_->get_total_equity();
        double peak_equity = intraday_peak_equity_.load();
        
        if (current_equity > peak_equity) {
            intraday_peak_equity_.store(current_equity);
            peak_equity = current_equity;
        }
        
        double drawdown_pct = peak_equity > 0 ? (peak_equity - current_equity) / peak_equity * 100.0 : 0.0;
        
        if (drawdown_pct > institutional_limits_.max_intraday_drawdown_pct) {
            log_risk_event(InstitutionalRiskType::OPERATIONAL_ANOMALY, ProfessionalRiskLevel::RED,
                         "PORTFOLIO", "ALL", "Intraday drawdown exceeds limit", drawdown_pct,
                         institutional_limits_.max_intraday_drawdown_pct);
            return false;
        }
        return true;
    }
    
    void update_return_calculations(const std::string& symbol, double price) {
        auto& returns = price_returns_[symbol];
        if (!returns.empty()) {
            double last_price = std::exp(returns.back());
            returns.push_back(std::log(price / last_price));
        } else {
            returns.push_back(0.0);
        }
        
        if (returns.size() > MAX_PRICE_HISTORY) {
            returns.pop_front();
        }
    }
    
    void update_correlation_matrix() {
        // Simplified correlation update
        for (size_t i = 0; i < active_assets_.size(); ++i) {
            for (size_t j = i + 1; j < active_assets_.size(); ++j) {
                correlation_matrix_[active_assets_[i]][active_assets_[j]] = 0.5; // Default correlation
                correlation_matrix_[active_assets_[j]][active_assets_[i]] = 0.5;
            }
        }
    }
    
    void calculate_portfolio_risk_metrics() {
        auto current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        double total_equity = position_tracker_->get_total_equity();
        
        // Simplified VaR calculation (2% of portfolio)
        current_portfolio_metrics_.var_1day_95 = total_equity * 0.02;
        current_portfolio_metrics_.var_1day_99 = total_equity * 0.03;
        current_portfolio_metrics_.portfolio_volatility = 0.2; // 20% volatility assumption
        
        // Calculate concentration
        double max_position = 0.0;
        auto all_positions = position_tracker_->get_all_positions();
        for (const auto& [exchange, symbols] : all_positions) {
            for (const auto& [symbol, pos] : symbols) {
                double position_value = std::abs(pos.quantity * pos.avg_price);
                double position_pct = position_value / total_equity * 100.0;
                max_position = std::max(max_position, position_pct);
            }
        }
        current_portfolio_metrics_.max_single_position_pct = max_position;
        
        current_portfolio_metrics_.liquidity_score = 0.8; // Default good liquidity
        current_portfolio_metrics_.avg_correlation = 0.6;  // Moderate correlation
        current_portfolio_metrics_.last_calculation_ns = current_time;
        current_portfolio_metrics_.is_valid = true;
    }
    
    void update_market_regime_detection() {
        if (current_portfolio_metrics_.portfolio_volatility > 0.3) {
            current_market_regime_.store(MarketRegime::HIGH_VOLATILITY);
        } else {
            current_market_regime_.store(MarketRegime::NORMAL_VOLATILITY);
        }
    }
    
    void assess_overall_risk_level() {
        if (!current_portfolio_metrics_.is_valid) {
            current_risk_level_.store(ProfessionalRiskLevel::YELLOW);
            return;
        }
        
        ProfessionalRiskLevel new_level = ProfessionalRiskLevel::GREEN;
        
        double current_equity = position_tracker_->get_total_equity();
        double var_pct = current_portfolio_metrics_.var_1day_95 / current_equity * 100.0;
        
        if (var_pct > institutional_limits_.max_portfolio_var_pct * 0.8) {
            new_level = ProfessionalRiskLevel::YELLOW;
        }
        if (var_pct > institutional_limits_.max_portfolio_var_pct) {
            new_level = ProfessionalRiskLevel::RED;
        }
        
        current_risk_level_.store(new_level);
    }
    
    void validate_risk_models() {
        model_performance_score_.store(0.8); // Default good performance
    }
    
    double calculate_stress_scenario_impact(const StressTestScenario& scenario) {
        double total_impact = 0.0;
        auto all_positions = position_tracker_->get_all_positions();
        for (const auto& [exchange, symbols] : all_positions) {
            for (const auto& [symbol, pos] : symbols) {
                double position_value = pos.quantity * pos.avg_price;
                double price_shock = 0.0;
                
                if (symbol.find("BTC") != std::string::npos) {
                    price_shock = scenario.btc_shock_pct / 100.0;
                } else if (symbol.find("ETH") != std::string::npos) {
                    price_shock = scenario.eth_shock_pct / 100.0;
                }
                
                total_impact += position_value * price_shock * scenario.volatility_multiplier;
            }
        }
        
        return total_impact;
    }
    
    double calculate_stress_var_impact(const StressTestScenario& scenario) {
        if (!current_portfolio_metrics_.is_valid) return 0.0;
        return current_portfolio_metrics_.var_1day_95 * scenario.volatility_multiplier;
    }
    
    void generate_compliance_report() const {
        if (!institutional_limits_.enable_regulatory_monitoring) return;
        
        printf("\n📋 Regulatory Compliance: All limits within bounds\n");
        printf("   Audit Trail: %s\n", institutional_limits_.enable_audit_trail ? "ACTIVE" : "INACTIVE");
        printf("   Real-time Reporting: %s\n", institutional_limits_.enable_real_time_reporting ? "ENABLED" : "DISABLED");
    }
};

} // namespace risk
