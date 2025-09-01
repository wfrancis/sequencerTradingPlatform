#pragma once

#include <random>
#include <map>
#include <atomic>
#include <tuple>
#include "shared_types.hpp"

namespace hft {

// Market regime types for different market conditions
enum class MarketRegime : uint8_t {
    NORMAL = 0,
    VOLATILE = 1,
    TRENDING = 2,
    CRISIS = 3
};

// Base class for market data generation across all simulators
class BaseMarketGenerator {
protected:
    mutable std::mt19937_64 rng_;
    std::map<SymbolId, double> prices_;
    std::map<SymbolId, double> volatilities_;
    std::map<SymbolId, double> trends_;
    std::atomic<uint64_t> updates_generated_{0};
    
    MarketRegime current_regime_ = MarketRegime::NORMAL;
    uint64_t regime_start_time_ = 0;

public:
    explicit BaseMarketGenerator(uint64_t seed = std::random_device{}()) 
        : rng_(seed), regime_start_time_(get_timestamp_ns()) {
        initialize_default_prices();
    }

    virtual ~BaseMarketGenerator() = default;

    // Pure virtual methods that each implementation must provide
    virtual void generate_market_update() = 0;
    virtual double get_price(SymbolId symbol) const = 0;
    
    // Common utility methods
    void initialize_default_prices() {
        prices_[BTC_USD] = 45000.0;  // BTC
        prices_[ETH_USD] = 3200.0;   // ETH
        volatilities_[BTC_USD] = 0.02;  // 2% volatility
        volatilities_[ETH_USD] = 0.03;  // 3% volatility
        trends_[BTC_USD] = 0.0;
        trends_[ETH_USD] = 0.0;
    }

    // Common market event triggers
    virtual void trigger_flash_crash() {
        printf("🚨 TRIGGERING FLASH CRASH EVENT\n");
        current_regime_ = MarketRegime::CRISIS;
        regime_start_time_ = get_timestamp_ns();
        
        // Instant price drop
        for (auto& [symbol, price] : prices_) {
            price *= 0.85;  // 15% drop
        }
        
        // Increase volatility
        for (auto& [symbol, vol] : volatilities_) {
            vol *= 5.0;  // 5x volatility spike
        }
    }

    virtual void trigger_volatility_spike() {
        printf("⚡ TRIGGERING VOLATILITY SPIKE\n");
        current_regime_ = MarketRegime::VOLATILE;
        regime_start_time_ = get_timestamp_ns();
        
        for (auto& [symbol, vol] : volatilities_) {
            vol *= 3.0;  // 3x volatility
        }
    }

    virtual void restore_normal_market() {
        printf("✅ RESTORING NORMAL MARKET CONDITIONS\n");
        current_regime_ = MarketRegime::NORMAL;
        volatilities_[BTC_USD] = 0.02;
        volatilities_[ETH_USD] = 0.03;
        trends_[BTC_USD] = 0.0;
        trends_[ETH_USD] = 0.0;
    }

    // Get market data in common format (bid, ask, volume)
    virtual std::tuple<double, double, double> get_market_data(SymbolId symbol) const {
        double mid = get_price(symbol);
        double spread_bps = get_spread_bps(symbol);
        double spread_abs = mid * spread_bps / 10000.0;
        
        double bid = mid - spread_abs / 2.0;
        double ask = mid + spread_abs / 2.0;
        double volume = std::uniform_real_distribution<>(0.5, 2.0)(rng_);
        
        return {bid, ask, volume};
    }

    virtual double get_spread_bps(SymbolId symbol) const {
        double base_spread_bps = (symbol == BTC_USD) ? 1.0 : 2.0;
        double vol = get_effective_volatility(symbol);
        double vol_multiplier = 1.0 + vol * 10.0;  // Higher vol = wider spreads
        return base_spread_bps * vol_multiplier;
    }

    // Common getters
    uint64_t get_updates_generated() const { 
        return updates_generated_.load(); 
    }

    MarketRegime get_current_regime() const { 
        return current_regime_; 
    }

    void reset_stats() { 
        updates_generated_.store(0); 
    }

protected:
    double get_effective_volatility(SymbolId symbol) const {
        auto it = volatilities_.find(symbol);
        return it != volatilities_.end() ? it->second : 0.02;  // Default 2%
    }

    // Common price movement generation
    double generate_price_change(double current_price, double volatility, double trend = 0.0) {
        updates_generated_.fetch_add(1);
        
        // Base random walk with trend
        std::normal_distribution<double> price_change(trend, volatility * current_price);
        double change = price_change(rng_);
        
        // Add regime-specific effects
        switch (current_regime_) {
            case MarketRegime::CRISIS:
                change += current_price * -0.001;  // Strong downward pressure
                break;
            case MarketRegime::VOLATILE:
                change *= 2.0;  // Amplify movements
                break;
            case MarketRegime::TRENDING:
                change += trend * current_price;  // Stronger trend effect
                break;
            default:
                break;
        }
        
        return change;
    }

    // Auto-transition market regimes
    void update_market_regime_if_needed() {
        uint64_t current_time = get_timestamp_ns();
        uint64_t regime_duration = current_time - regime_start_time_;
        
        // Auto-transition back to normal after 10 seconds
        if (regime_duration > 10000000000ULL && current_regime_ != MarketRegime::NORMAL) {
            restore_normal_market();
        }
    }
};

// Simple implementation for basic simulators
class SimpleMarketGenerator : public BaseMarketGenerator {
public:
    SimpleMarketGenerator(uint64_t seed = std::random_device{}()) 
        : BaseMarketGenerator(seed) {}

    void generate_market_update() override {
        update_market_regime_if_needed();
        
        for (auto& [symbol, price] : prices_) {
            double volatility = get_effective_volatility(symbol);
            double trend = trends_[symbol];
            double change = generate_price_change(price, volatility, trend);
            
            price += change;
            price = std::max(price, 100.0);  // Floor price
        }
    }

    double get_price(SymbolId symbol) const override {
        auto it = prices_.find(symbol);
        return it != prices_.end() ? it->second : 0.0;
    }

    void simulate_news_shock() {
        printf("📰 News shock - random 5-10%% price movement\n");
        for (auto& [symbol, price] : prices_) {
            double shock = (0.05 + 0.05 * std::uniform_real_distribution<>(0, 1)(rng_));
            if (std::uniform_real_distribution<>(0, 1)(rng_) > 0.5) shock = -shock;
            price *= (1.0 + shock);
        }
    }

    void reset_volatility() {
        volatilities_[BTC_USD] = 0.02;
        volatilities_[ETH_USD] = 0.03;
    }
};

// High-frequency implementation for speed tests
class HighSpeedMarketGenerator : public BaseMarketGenerator {
public:
    HighSpeedMarketGenerator(uint64_t seed = std::random_device{}()) 
        : BaseMarketGenerator(seed) {
        // Much smaller moves for HFT simulation
        volatilities_[BTC_USD] = 0.0001;
        volatilities_[ETH_USD] = 0.0002;
    }

    void generate_market_update() override {
        // No regime checking for maximum speed
        for (auto& [symbol, price] : prices_) {
            double volatility = get_effective_volatility(symbol);
            // Micro price movements typical in HFT
            std::normal_distribution<double> micro_change(0.0, volatility * price);
            double change = micro_change(rng_);
            price += change;
            price = std::max(price, 100.0);
        }
        updates_generated_.fetch_add(1);
    }

    double get_price(SymbolId symbol) const override {
        auto it = prices_.find(symbol);
        return it != prices_.end() ? it->second : 0.0;
    }

    void inject_flash_crash() {
        for (auto& [symbol, price] : prices_) {
            price *= 0.95;  // 5% instant drop for HFT
        }
    }

    void reset_volatility() {
        volatilities_[BTC_USD] = 0.0001;
        volatilities_[ETH_USD] = 0.0002;
    }
};

} // namespace hft
