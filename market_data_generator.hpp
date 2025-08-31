#pragma once

#include <vector>
#include <unordered_map>
#include <random>
#include <functional>
#include <deque>
#include <string>
#include "messages.hpp"

namespace hft::simulator {

enum class AnomalyType : uint8_t {
    FAT_FINGER = 0,
    FLASH_CRASH = 1,
    LIQUIDITY_VOID = 2,
    NEWS_SHOCK = 3,
    ALGORITHM_GONE_WILD = 4
};

enum class EventType : uint8_t {
    TRADE = 0,
    QUOTE = 1,
    IMBALANCE = 2,
    HALT = 3,
    AUCTION = 4
};

class MarketDataGenerator {
public:
    struct MarketRegime {
        enum Type { NORMAL, VOLATILE, TRENDING, RANGING, FLASH_CRASH };
        Type current = NORMAL;
        double volatility_multiplier = 1.0;
        double trend_bias = 0.0;
        uint64_t regime_start_ns;
        uint64_t regime_duration_ns;
        
        // Additional regime parameters
        double jump_probability = 0.001;      // Probability of price jumps per tick
        double mean_reversion_speed = 0.1;    // Ornstein-Uhlenbeck parameter
        double correlation_decay = 0.95;      // Auto-correlation decay
    };
    
    struct Level {
        Price price;
        Quantity quantity;
        uint32_t order_count;
        uint64_t last_update_ns;
        
        Level(Price p = 0, Quantity q = 0, uint32_t count = 0) 
            : price(p), quantity(q), order_count(count), last_update_ns(get_timestamp_ns()) {}
    };
    
    struct OrderBookState {
        std::vector<Level> bids;    // Sorted descending by price
        std::vector<Level> asks;    // Sorted ascending by price
        Price last_trade_price;
        Quantity last_trade_size;
        uint64_t last_update_ns;
        double mid_price;
        double spread_bps;
        
        OrderBookState() : last_trade_price(0), last_trade_size(0), 
                          last_update_ns(0), mid_price(0), spread_bps(0) {
            bids.reserve(20);  // Reserve space for 20 levels
            asks.reserve(20);
        }
        
        void update_mid_price() {
            if (!bids.empty() && !asks.empty()) {
                double bid_price = to_float_price(bids[0].price);
                double ask_price = to_float_price(asks[0].price);
                mid_price = (bid_price + ask_price) / 2.0;
                spread_bps = ((ask_price - bid_price) / mid_price) * 10000.0;
            }
        }
    };
    
    struct MarketMicrostructure {
        double tick_size = 0.01;
        double lot_size = 0.001;
        double spread_mean = 0.0010;        // 10 bps
        double spread_std = 0.0002;         // 2 bps std dev
        double trade_intensity = 100.0;     // trades per second
        double quote_intensity = 1000.0;    // quote updates per second
        double market_impact_factor = 0.1;  // Price impact coefficient
        double inventory_decay = 0.99;      // Market maker inventory decay
        
        // Hawkes process parameters for clustered activity
        double hawkes_baseline = 50.0;      // Base arrival rate
        double hawkes_alpha = 0.5;          // Self-excitation strength
        double hawkes_beta = 2.0;           // Decay rate
    };

private:
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uniform_dist_;
    std::normal_distribution<double> normal_dist_;
    std::exponential_distribution<double> exp_dist_;
    std::poisson_distribution<int> poisson_dist_;
    
    std::unordered_map<SymbolId, OrderBookState> order_books_;
    std::unordered_map<SymbolId, MarketMicrostructure> microstructures_;
    std::unordered_map<SymbolId, MarketRegime> regimes_;
    std::unordered_map<SymbolId, std::deque<double>> price_history_;
    
    // Hawkes process state for modeling clustered volatility
    struct HawkesState {
        double intensity = 50.0;
        uint64_t last_event_time = 0;
        std::deque<uint64_t> recent_events;
    };
    std::unordered_map<SymbolId, HawkesState> hawkes_states_;
    
    // Correlation matrix for multi-asset simulation
    std::vector<std::vector<double>> correlation_matrix_;
    std::vector<double> random_shocks_;
    
public:
    explicit MarketDataGenerator(uint64_t seed = std::random_device{}()) 
        : rng_(seed), uniform_dist_(0.0, 1.0), normal_dist_(0.0, 1.0), 
          exp_dist_(1.0), poisson_dist_(1.0) {}
    
    void configure_market_dynamics() {
        // Initialize correlation matrix (simplified 2x2 for BTC/ETH)
        correlation_matrix_ = {{1.0, 0.7}, {0.7, 1.0}};
        random_shocks_.resize(2);
        
        // Set up default microstructure parameters
        MarketMicrostructure btc_params;
        btc_params.tick_size = 0.01;
        btc_params.spread_mean = 0.0005;  // 5 bps for BTC
        btc_params.trade_intensity = 200.0;
        
        MarketMicrostructure eth_params;
        eth_params.tick_size = 0.001;
        eth_params.spread_mean = 0.0008;  // 8 bps for ETH
        eth_params.trade_intensity = 150.0;
        
        microstructures_[1] = btc_params;  // Assuming BTC symbol ID = 1
        microstructures_[2] = eth_params;  // Assuming ETH symbol ID = 2
    }
    
    void generate_order_book_snapshot(
        SymbolId symbol,
        Venue venue,
        uint64_t timestamp_ns,
        OrderBookState& book
    ) {
        auto& regime = regimes_[symbol];
        auto& microstructure = microstructures_[symbol];
        
        // Update market regime if needed
        update_market_regime(symbol, timestamp_ns, regime);
        
        // Generate correlated price innovations
        double dt = (timestamp_ns - book.last_update_ns) / 1e9; // Convert to seconds
        double price_innovation = generate_price_innovation(book.mid_price, dt, regime);
        
        // Update order book with new mid price
        double new_mid = book.mid_price + price_innovation;
        update_order_book_dynamics(book, price_innovation, microstructure);
        
        // Update Hawkes process intensity
        update_hawkes_intensity(symbol, timestamp_ns);
        
        book.last_update_ns = timestamp_ns;
        book.update_mid_price();
        
        // Store price in history for analysis
        price_history_[symbol].push_back(new_mid);
        if (price_history_[symbol].size() > 1000) {
            price_history_[symbol].pop_front();
        }
    }
    
    void simulate_market_event(
        EventType type,
        SymbolId symbol,
        Venue venue
    ) {
        auto& book = order_books_[symbol];
        auto& microstructure = microstructures_[symbol];
        
        switch (type) {
            case EventType::TRADE: {
                simulate_trade_event(book, microstructure);
                break;
            }
            case EventType::QUOTE: {
                simulate_quote_update(book, microstructure);
                break;
            }
            case EventType::IMBALANCE: {
                simulate_order_imbalance(book, microstructure);
                break;
            }
            case EventType::HALT: {
                // Clear order book on trading halt
                book.bids.clear();
                book.asks.clear();
                break;
            }
            case EventType::AUCTION: {
                simulate_auction_event(book, microstructure);
                break;
            }
        }
    }
    
    void inject_anomaly(AnomalyType type) {
        switch (type) {
            case AnomalyType::FAT_FINGER: {
                inject_fat_finger_trade();
                break;
            }
            case AnomalyType::FLASH_CRASH: {
                inject_flash_crash();
                break;
            }
            case AnomalyType::LIQUIDITY_VOID: {
                inject_liquidity_void();
                break;
            }
            case AnomalyType::NEWS_SHOCK: {
                inject_news_shock();
                break;
            }
            case AnomalyType::ALGORITHM_GONE_WILD: {
                inject_algorithm_malfunction();
                break;
            }
        }
    }
    
    // Initialize order book with realistic levels
    void initialize_order_book(SymbolId symbol, double initial_price, double initial_spread_bps = 10.0) {
        OrderBookState& book = order_books_[symbol];
        book.mid_price = initial_price;
        book.last_update_ns = get_timestamp_ns();
        
        double spread = initial_price * (initial_spread_bps / 10000.0);
        double bid_price = initial_price - spread / 2.0;
        double ask_price = initial_price + spread / 2.0;
        
        auto& microstructure = microstructures_[symbol];
        
        // Generate realistic order book depth
        for (int i = 0; i < 10; ++i) {
            // Bid side
            double level_price = bid_price - i * microstructure.tick_size;
            Quantity level_size = static_cast<Quantity>(
                (1000.0 * std::exp(-i * 0.3)) * QUANTITY_MULTIPLIER
            );
            book.bids.emplace_back(to_fixed_price(level_price), level_size, 1 + i);
            
            // Ask side
            level_price = ask_price + i * microstructure.tick_size;
            book.asks.emplace_back(to_fixed_price(level_price), level_size, 1 + i);
        }
        
        book.update_mid_price();
    }
    
    // Get current order book state
    const OrderBookState& get_order_book(SymbolId symbol) const {
        static OrderBookState empty_book;
        auto it = order_books_.find(symbol);
        return (it != order_books_.end()) ? it->second : empty_book;
    }
    
private:
    double generate_price_innovation(
        double current_price,
        double dt,
        const MarketRegime& regime
    ) {
        // Ornstein-Uhlenbeck mean reversion process
        double mean_reversion = -regime.mean_reversion_speed * regime.trend_bias * dt;
        
        // Diffusion term with regime-dependent volatility
        double volatility = 0.02 * regime.volatility_multiplier; // 2% base volatility
        double diffusion = volatility * std::sqrt(dt) * normal_dist_(rng_);
        
        // Jump component
        double jump = 0.0;
        if (uniform_dist_(rng_) < regime.jump_probability * dt) {
            double jump_size = normal_dist_(rng_) * 0.005 * current_price; // 0.5% average jump
            jump = jump_size * (uniform_dist_(rng_) > 0.5 ? 1 : -1);
        }
        
        return current_price * (mean_reversion + diffusion) + jump;
    }
    
    void update_order_book_dynamics(
        OrderBookState& book,
        double mid_price_change,
        const MarketMicrostructure& params
    ) {
        double new_mid = book.mid_price + mid_price_change;
        double spread = new_mid * (params.spread_mean + params.spread_std * normal_dist_(rng_));
        
        // Update top-of-book
        if (!book.bids.empty() && !book.asks.empty()) {
            book.bids[0].price = to_fixed_price(new_mid - spread / 2.0);
            book.asks[0].price = to_fixed_price(new_mid + spread / 2.0);
            
            // Simulate quantity changes with market maker behavior
            simulate_market_maker_behavior(book, 0.0, spread / new_mid);
        }
    }
    
    void simulate_market_maker_behavior(
        OrderBookState& book,
        double inventory_skew,
        double spread_adjustment
    ) {
        if (book.bids.empty() || book.asks.empty()) return;
        
        // Adjust quantities based on market maker inventory and risk
        double inventory_adjustment = 1.0 + inventory_skew * 0.1; // ±10% adjustment
        double spread_mult = 1.0 + spread_adjustment;
        
        for (auto& level : book.bids) {
            // Market makers reduce size when inventory is long
            level.quantity = static_cast<Quantity>(
                level.quantity * inventory_adjustment * (0.9 + 0.2 * uniform_dist_(rng_))
            );
        }
        
        for (auto& level : book.asks) {
            // Market makers reduce size when inventory is short
            level.quantity = static_cast<Quantity>(
                level.quantity * (2.0 - inventory_adjustment) * (0.9 + 0.2 * uniform_dist_(rng_))
            );
        }
    }
    
    void update_market_regime(SymbolId symbol, uint64_t timestamp_ns, MarketRegime& regime) {
        // Check if regime should change
        if (timestamp_ns > regime.regime_start_ns + regime.regime_duration_ns) {
            // Simple regime switching model
            double regime_prob = uniform_dist_(rng_);
            
            MarketRegime::Type old_regime = regime.current;
            if (regime_prob < 0.7) {
                regime.current = MarketRegime::NORMAL;
                regime.volatility_multiplier = 1.0;
                regime.trend_bias = 0.0;
            } else if (regime_prob < 0.85) {
                regime.current = MarketRegime::VOLATILE;
                regime.volatility_multiplier = 2.0;
                regime.trend_bias = 0.0;
            } else if (regime_prob < 0.93) {
                regime.current = MarketRegime::TRENDING;
                regime.volatility_multiplier = 1.2;
                regime.trend_bias = (uniform_dist_(rng_) > 0.5 ? 0.001 : -0.001);
            } else if (regime_prob < 0.98) {
                regime.current = MarketRegime::RANGING;
                regime.volatility_multiplier = 0.7;
                regime.trend_bias = 0.0;
            } else {
                regime.current = MarketRegime::FLASH_CRASH;
                regime.volatility_multiplier = 5.0;
                regime.trend_bias = -0.01; // Strong downward bias
            }
            
            if (regime.current != old_regime) {
                regime.regime_start_ns = timestamp_ns;
                // Regime durations: normal=5min, volatile=2min, others=1min
                regime.regime_duration_ns = (regime.current == MarketRegime::NORMAL ? 300 :
                                           regime.current == MarketRegime::VOLATILE ? 120 : 60) * 1000000000ULL;
            }
        }
    }
    
    void update_hawkes_intensity(SymbolId symbol, uint64_t timestamp_ns) {
        auto& hawkes = hawkes_states_[symbol];
        auto& params = microstructures_[symbol];
        
        // Decay intensity based on time since last events
        double dt = (timestamp_ns - hawkes.last_event_time) / 1e9;
        hawkes.intensity *= std::exp(-params.hawkes_beta * dt);
        hawkes.intensity = std::max(hawkes.intensity, params.hawkes_baseline);
        
        // Remove old events from recent history (older than 60 seconds)
        while (!hawkes.recent_events.empty() && 
               timestamp_ns - hawkes.recent_events.front() > 60000000000ULL) {
            hawkes.recent_events.pop_front();
        }
        
        hawkes.last_event_time = timestamp_ns;
    }
    
    void simulate_trade_event(OrderBookState& book, const MarketMicrostructure& params) {
        if (book.bids.empty() || book.asks.empty()) return;
        
        // Simulate aggressive order hitting the book
        bool buy_trade = uniform_dist_(rng_) > 0.5;
        
        if (buy_trade && !book.asks.empty()) {
            auto& ask = book.asks[0];
            Quantity trade_size = std::min(ask.quantity, 
                static_cast<Quantity>(exp_dist_(rng_) * 100 * QUANTITY_MULTIPLIER));
            
            book.last_trade_price = ask.price;
            book.last_trade_size = trade_size;
            ask.quantity -= trade_size;
            
            if (ask.quantity == 0) {
                book.asks.erase(book.asks.begin());
            }
        } else if (!book.bids.empty()) {
            auto& bid = book.bids[0];
            Quantity trade_size = std::min(bid.quantity,
                static_cast<Quantity>(exp_dist_(rng_) * 100 * QUANTITY_MULTIPLIER));
            
            book.last_trade_price = bid.price;
            book.last_trade_size = trade_size;
            bid.quantity -= trade_size;
            
            if (bid.quantity == 0) {
                book.bids.erase(book.bids.begin());
            }
        }
    }
    
    void simulate_quote_update(OrderBookState& book, const MarketMicrostructure& params) {
        // Simulate passive order placement/cancellation
        bool update_bids = uniform_dist_(rng_) > 0.5;
        
        if (update_bids && !book.bids.empty()) {
            auto& bid = book.bids[0];
            if (uniform_dist_(rng_) < 0.3) { // 30% chance to change quantity
                bid.quantity = static_cast<Quantity>(
                    bid.quantity * (0.8 + 0.4 * uniform_dist_(rng_))
                );
            }
        } else if (!book.asks.empty()) {
            auto& ask = book.asks[0];
            if (uniform_dist_(rng_) < 0.3) {
                ask.quantity = static_cast<Quantity>(
                    ask.quantity * (0.8 + 0.4 * uniform_dist_(rng_))
                );
            }
        }
    }
    
    void simulate_order_imbalance(OrderBookState& book, const MarketMicrostructure& params) {
        // Create temporary imbalance by reducing one side
        double imbalance_factor = 0.3 + 0.4 * uniform_dist_(rng_); // 30-70% reduction
        
        if (uniform_dist_(rng_) > 0.5) {
            // Reduce bid side liquidity
            for (auto& level : book.bids) {
                level.quantity = static_cast<Quantity>(level.quantity * imbalance_factor);
            }
        } else {
            // Reduce ask side liquidity
            for (auto& level : book.asks) {
                level.quantity = static_cast<Quantity>(level.quantity * imbalance_factor);
            }
        }
    }
    
    void simulate_auction_event(OrderBookState& book, const MarketMicrostructure& params) {
        // Simulate opening/closing auction with crossed book
        if (book.bids.empty() || book.asks.empty()) return;
        
        Price auction_price = (book.bids[0].price + book.asks[0].price) / 2;
        Quantity matched_volume = std::min(book.bids[0].quantity, book.asks[0].quantity);
        
        book.last_trade_price = auction_price;
        book.last_trade_size = matched_volume;
        
        // Remove matched volume
        book.bids[0].quantity -= matched_volume;
        book.asks[0].quantity -= matched_volume;
        
        if (book.bids[0].quantity == 0) book.bids.erase(book.bids.begin());
        if (book.asks[0].quantity == 0) book.asks.erase(book.asks.begin());
    }
    
    void inject_fat_finger_trade() {
        // Simulate erroneous large order at wrong price
        for (auto& [symbol, book] : order_books_) {
            if (book.bids.empty() || book.asks.empty()) continue;
            
            double error_factor = 1.05 + 0.1 * uniform_dist_(rng_); // 5-15% price error
            Price error_price = static_cast<Price>(book.asks[0].price * error_factor);
            Quantity large_size = static_cast<Quantity>(10000 * QUANTITY_MULTIPLIER); // Large order
            
            // Add erroneous bid at too high price
            book.bids.insert(book.bids.begin(), Level(error_price, large_size, 1));
        }
    }
    
    void inject_flash_crash() {
        // Temporarily switch all symbols to flash crash regime
        uint64_t now = get_timestamp_ns();
        for (auto& [symbol, regime] : regimes_) {
            regime.current = MarketRegime::FLASH_CRASH;
            regime.volatility_multiplier = 5.0;
            regime.trend_bias = -0.02;
            regime.regime_start_ns = now;
            regime.regime_duration_ns = 30000000000ULL; // 30 seconds
        }
    }
    
    void inject_liquidity_void() {
        // Remove significant amount of liquidity from all books
        for (auto& [symbol, book] : order_books_) {
            for (auto& level : book.bids) {
                level.quantity = static_cast<Quantity>(level.quantity * 0.1); // 90% reduction
            }
            for (auto& level : book.asks) {
                level.quantity = static_cast<Quantity>(level.quantity * 0.1);
            }
        }
    }
    
    void inject_news_shock() {
        // Sudden regime change with high volatility and trend
        uint64_t now = get_timestamp_ns();
        for (auto& [symbol, regime] : regimes_) {
            regime.current = MarketRegime::VOLATILE;
            regime.volatility_multiplier = 3.0;
            regime.trend_bias = (uniform_dist_(rng_) > 0.5 ? 0.005 : -0.005);
            regime.regime_start_ns = now;
            regime.regime_duration_ns = 120000000000ULL; // 2 minutes
        }
    }
    
    void inject_algorithm_malfunction() {
        // Simulate algorithm sending many small orders rapidly
        for (auto& [symbol, book] : order_books_) {
            if (book.bids.empty() || book.asks.empty()) continue;
            
            // Add many small orders at various levels
            for (int i = 0; i < 50; ++i) {
                Quantity small_size = static_cast<Quantity>(10 * QUANTITY_MULTIPLIER);
                Price offset = static_cast<Price>(i * microstructures_[symbol].tick_size * PRICE_MULTIPLIER);
                
                if (uniform_dist_(rng_) > 0.5) {
                    // Add bid orders
                    book.bids.emplace_back(book.bids.empty() ? 
                        to_fixed_price(book.mid_price - 0.01) : book.bids[0].price - offset, 
                        small_size, 1);
                } else {
                    // Add ask orders
                    book.asks.emplace_back(book.asks.empty() ? 
                        to_fixed_price(book.mid_price + 0.01) : book.asks[0].price + offset, 
                        small_size, 1);
                }
            }
        }
    }
};

} // namespace hft::simulator
