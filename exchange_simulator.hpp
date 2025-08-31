#pragma once

#include <map>
#include <deque>
#include <unordered_map>
#include <random>
#include <functional>
#include <string>
#include <atomic>
#include "messages.hpp"
#include "market_data_generator.hpp"

namespace hft::simulator {

class ExchangeSimulator {
public:
    struct Order {
        OrderId id;
        Side side;
        OrderType type;
        Price price;
        Quantity quantity;
        Quantity filled_quantity = 0;
        uint64_t timestamp;
        uint32_t priority;  // For time priority
        std::string trader_id;
        TimeInForce tif = TimeInForce::DAY;
        OrderStatus status = OrderStatus::PENDING;
        uint64_t sequence = 0;
        
        Order() = default;
        Order(OrderId order_id, Side s, OrderType t, Price p, Quantity q, 
              const std::string& trader, uint64_t ts = get_timestamp_ns())
            : id(order_id), side(s), type(t), price(p), quantity(q), 
              trader_id(trader), timestamp(ts), priority(0) {}
        
        Quantity remaining_quantity() const {
            return quantity - filled_quantity;
        }
        
        bool is_fully_filled() const {
            return filled_quantity >= quantity;
        }
    };
    
private:
    struct OrderBook {
        // Price-time priority: map ensures price ordering, deque ensures time ordering
        std::map<Price, std::deque<Order>, std::greater<>> bids;  // Descending price
        std::map<Price, std::deque<Order>, std::less<>> asks;     // Ascending price
        std::atomic<uint32_t> next_priority{0};
        SymbolId symbol_id;
        
        explicit OrderBook(SymbolId id) : symbol_id(id) {}
        
        void add_order(Order& order) {
            order.priority = next_priority.fetch_add(1);
            if (order.side == Side::BUY) {
                bids[order.price].push_back(std::move(order));
            } else {
                asks[order.price].push_back(std::move(order));
            }
        }
        
        bool remove_order(OrderId id, Side side) {
            auto& levels = (side == Side::BUY) ? bids : asks;
            for (auto& [price, orders] : levels) {
                auto it = std::find_if(orders.begin(), orders.end(),
                    [id](const Order& o) { return o.id == id; });
                if (it != orders.end()) {
                    orders.erase(it);
                    if (orders.empty()) {
                        levels.erase(price);
                    }
                    return true;
                }
            }
            return false;
        }
        
        Order* find_order(OrderId id, Side side) {
            auto& levels = (side == Side::BUY) ? bids : asks;
            for (auto& [price, orders] : levels) {
                auto it = std::find_if(orders.begin(), orders.end(),
                    [id](const Order& o) { return o.id == id; });
                if (it != orders.end()) {
                    return &(*it);
                }
            }
            return nullptr;
        }
        
        std::pair<Price, Price> get_bbo() const {
            Price best_bid = 0, best_ask = 0;
            if (!bids.empty()) best_bid = bids.begin()->first;
            if (!asks.empty()) best_ask = asks.begin()->first;
            return {best_bid, best_ask};
        }
        
        size_t total_orders() const {
            size_t count = 0;
            for (const auto& [price, orders] : bids) count += orders.size();
            for (const auto& [price, orders] : asks) count += orders.size();
            return count;
        }
    };
    
    struct LatencyProfile {
        std::normal_distribution<double> order_ack_latency{100, 20};    // microseconds
        std::normal_distribution<double> cancel_ack_latency{80, 15};
        std::normal_distribution<double> fill_latency{120, 30};
        std::normal_distribution<double> market_data_latency{50, 10};
        double packet_loss_rate = 0.0001;
        double duplicate_rate = 0.00001;
        double out_of_order_rate = 0.0001;
        
        // Risk controls
        double max_order_rate = 1000.0; // orders per second per trader
        double max_cancel_ratio = 100.0; // cancel-to-fill ratio
        double max_message_rate = 5000.0; // total messages per second
        
        LatencyProfile() = default;
    };
    
    struct FeeStructure {
        double maker_rebate = -0.0002;  // -2 bps (rebate)
        double taker_fee = 0.0003;      // 3 bps
        double cancel_fee = 0.0;         // Per excessive cancels
        uint32_t cancel_ratio_threshold = 100;  // Cancel-to-fill ratio
        double minimum_fee = 0.01;       // Minimum fee in USD
        
        double calculate_fee(Quantity quantity, Price price, bool is_maker) const {
            double notional = to_float_price(price) * to_float_price(quantity);
            double fee_rate = is_maker ? maker_rebate : taker_fee;
            return std::max(notional * fee_rate, is_maker ? maker_rebate : minimum_fee);
        }
    };
    
    struct TraderStats {
        uint64_t orders_sent = 0;
        uint64_t orders_filled = 0;
        uint64_t orders_cancelled = 0;
        uint64_t messages_sent = 0;
        uint64_t last_message_time = 0;
        double total_fees = 0.0;
        
        double get_cancel_ratio() const {
            return orders_filled > 0 ? static_cast<double>(orders_cancelled) / orders_filled : 0.0;
        }
        
        bool is_rate_limited(uint64_t current_time, double max_rate) const {
            uint64_t window_ns = 1000000000ULL; // 1 second window
            if (current_time - last_message_time < window_ns) {
                double elapsed_seconds = (current_time - last_message_time) / 1e9;
                double current_rate = messages_sent / elapsed_seconds;
                return current_rate > max_rate;
            }
            return false;
        }
    };
    
public:
    struct Fill {
        OrderId order_id;
        std::string trader_id;
        Side side;
        Price price;
        Quantity quantity;
        uint64_t timestamp;
        uint64_t trade_id;
        double fee;
        bool is_maker;
        
        Fill(OrderId oid, const std::string& trader, Side s, Price p, 
             Quantity q, bool maker, uint64_t ts = get_timestamp_ns())
            : order_id(oid), trader_id(trader), side(s), price(p), 
              quantity(q), is_maker(maker), timestamp(ts), fee(0.0) {
            trade_id = ts; // Simple trade ID generation
        }
    };

private:
    
    std::mt19937_64 rng_;
    std::unordered_map<SymbolId, OrderBook> order_books_;
    std::unordered_map<std::string, TraderStats> trader_stats_;
    std::unordered_map<OrderId, Order> pending_orders_;
    
    LatencyProfile latency_profile_;
    FeeStructure fee_structure_;
    
    std::atomic<OrderId> next_order_id_{1};
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<uint64_t> total_volume_{0};
    
    // Market impact model parameters
    struct MarketImpactModel {
        double linear_impact = 1e-6;    // Linear market impact coefficient
        double sqrt_impact = 1e-4;      // Square root market impact coefficient
        double temporary_impact = 0.5;  // Temporary vs permanent impact ratio
        double decay_rate = 0.1;        // Impact decay rate (per second)
    } impact_model_;
    
    // Hidden liquidity simulation
    struct HiddenLiquidityPool {
        double iceberg_probability = 0.1;  // Probability of iceberg orders
        double dark_pool_ratio = 0.15;     // Ratio of volume in dark pools
        Quantity min_iceberg_size = static_cast<Quantity>(1000 * QUANTITY_MULTIPLIER);
        Quantity displayed_size = static_cast<Quantity>(100 * QUANTITY_MULTIPLIER);
    } hidden_liquidity_;
    
    // Event callbacks
    std::function<void(const Order&)> order_ack_callback_;
    std::function<void(const Order&, const std::string&)> order_reject_callback_;
    std::function<void(const Fill&)> fill_callback_;
    std::function<void(SymbolId, Price, Price)> market_data_callback_;
    
public:
    explicit ExchangeSimulator(uint64_t seed = std::random_device{}()) 
        : rng_(seed) {
        // Initialize with common crypto symbols
        order_books_.emplace(std::piecewise_construct, 
                            std::forward_as_tuple(1), 
                            std::forward_as_tuple(1)); // BTC
        order_books_.emplace(std::piecewise_construct, 
                            std::forward_as_tuple(2), 
                            std::forward_as_tuple(2)); // ETH
    }
    
    void set_latency_profile(const LatencyProfile& profile) {
        latency_profile_ = profile;
    }
    
    void set_fee_structure(const FeeStructure& fees) {
        fee_structure_ = fees;
    }
    
    void set_callbacks(
        std::function<void(const Order&)> ack_cb,
        std::function<void(const Order&, const std::string&)> reject_cb,
        std::function<void(const Fill&)> fill_cb,
        std::function<void(SymbolId, Price, Price)> md_cb
    ) {
        order_ack_callback_ = std::move(ack_cb);
        order_reject_callback_ = std::move(reject_cb);
        fill_callback_ = std::move(fill_cb);
        market_data_callback_ = std::move(md_cb);
    }
    
    OrderId submit_order(const Order& order) {
        auto& stats = trader_stats_[order.trader_id];
        uint64_t current_time = get_timestamp_ns();
        
        // Rate limiting check
        if (stats.is_rate_limited(current_time, latency_profile_.max_order_rate)) {
            if (order_reject_callback_) {
                order_reject_callback_(order, "Rate limit exceeded");
            }
            return 0; // Invalid order ID
        }
        
        // Order validation
        std::string reject_reason;
        if (!validate_order(order, reject_reason)) {
            if (order_reject_callback_) {
                order_reject_callback_(order, reject_reason);
            }
            return 0;
        }
        
        OrderId order_id = next_order_id_.fetch_add(1);
        Order new_order = order;
        new_order.id = order_id;
        new_order.timestamp = current_time;
        new_order.status = OrderStatus::PENDING;
        
        // Simulate order processing latency
        double ack_latency_us = latency_profile_.order_ack_latency(rng_);
        schedule_order_processing(std::move(new_order), ack_latency_us);
        
        // Update trader stats
        stats.orders_sent++;
        stats.messages_sent++;
        stats.last_message_time = current_time;
        
        return order_id;
    }
    
    bool cancel_order(OrderId id, const std::string& trader_id) {
        auto& stats = trader_stats_[trader_id];
        uint64_t current_time = get_timestamp_ns();
        
        // Rate limiting check
        if (stats.is_rate_limited(current_time, latency_profile_.max_cancel_ratio)) {
            return false;
        }
        
        // Find order across all symbols
        for (auto& [symbol_id, book] : order_books_) {
            for (int side = 0; side < 2; ++side) {
                Side order_side = static_cast<Side>(side);
                Order* order = book.find_order(id, order_side);
                if (order && order->trader_id == trader_id) {
                    // Simulate cancel processing latency
                    double cancel_latency_us = latency_profile_.cancel_ack_latency(rng_);
                    schedule_order_cancellation(id, order_side, symbol_id, cancel_latency_us);
                    
                    stats.orders_cancelled++;
                    stats.messages_sent++;
                    stats.last_message_time = current_time;
                    return true;
                }
            }
        }
        
        return false;
    }
    
    void process_matching() {
        // Process all pending order operations
        process_pending_operations();
        
        // Match orders in all symbols
        for (auto& [symbol_id, book] : order_books_) {
            match_orders(book);
            
            // Simulate hidden liquidity
            if (std::uniform_real_distribution<double>(0, 1)(rng_) < hidden_liquidity_.iceberg_probability) {
                simulate_hidden_liquidity(book, hidden_liquidity_.iceberg_probability);
            }
            
            // Publish market data
            auto [best_bid, best_ask] = book.get_bbo();
            if (market_data_callback_ && (best_bid > 0 || best_ask > 0)) {
                // Simulate market data latency
                double md_latency_us = latency_profile_.market_data_latency(rng_);
                schedule_market_data_publication(symbol_id, best_bid, best_ask, md_latency_us);
            }
        }
    }
    
    // Statistics and monitoring
    struct ExchangeStats {
        uint64_t total_orders_processed = 0;
        uint64_t total_trades = 0;
        uint64_t total_volume = 0;
        double total_fees_collected = 0.0;
        uint64_t active_orders = 0;
        double average_spread_bps = 0.0;
        
        struct SymbolStats {
            uint64_t trades = 0;
            uint64_t volume = 0;
            Price last_price = 0;
            double spread_bps = 0.0;
            uint32_t book_depth = 0;
        };
        std::unordered_map<SymbolId, SymbolStats> symbol_stats;
    };
    
    ExchangeStats get_statistics() const {
        ExchangeStats stats;
        stats.total_trades = total_trades_.load();
        stats.total_volume = total_volume_.load();
        
        for (const auto& [symbol_id, book] : order_books_) {
            ExchangeStats::SymbolStats symbol_stat;
            symbol_stat.book_depth = static_cast<uint32_t>(book.total_orders());
            
            auto [best_bid, best_ask] = book.get_bbo();
            if (best_bid > 0 && best_ask > 0) {
                double mid = (to_float_price(best_bid) + to_float_price(best_ask)) / 2.0;
                symbol_stat.spread_bps = ((to_float_price(best_ask) - to_float_price(best_bid)) / mid) * 10000.0;
            }
            
            stats.symbol_stats[symbol_id] = symbol_stat;
            stats.active_orders += symbol_stat.book_depth;
        }
        
        return stats;
    }
    
    void print_status() const {
        auto stats = get_statistics();
        printf("📊 Exchange Status:\n");
        printf("   Total trades: %lu\n", stats.total_trades);
        printf("   Total volume: %lu\n", stats.total_volume);
        printf("   Active orders: %lu\n", stats.active_orders);
        printf("   Symbols: %zu\n", stats.symbol_stats.size());
        
        for (const auto& [symbol_id, symbol_stat] : stats.symbol_stats) {
            printf("   Symbol %u: depth=%u, spread=%.1f bps\n", 
                   symbol_id, symbol_stat.book_depth, symbol_stat.spread_bps);
        }
    }
    
private:
    struct PendingOperation {
        enum Type { ORDER_ACK, ORDER_CANCEL, MARKET_DATA } type;
        uint64_t execute_time;
        Order order;
        OrderId cancel_order_id;
        Side cancel_side;
        SymbolId symbol_id;
        Price bid_price, ask_price;
        
        PendingOperation(Type t, uint64_t time) : type(t), execute_time(time) {}
    };
    
    std::vector<PendingOperation> pending_operations_;
    
    bool validate_order(const Order& order, std::string& reject_reason) const {
        if (order.quantity == 0) {
            reject_reason = "Invalid quantity";
            return false;
        }
        
        if (order.type == OrderType::LIMIT && order.price == 0) {
            reject_reason = "Invalid price for limit order";
            return false;
        }
        
        if (order_books_.find(1) == order_books_.end()) { // Assuming symbol validation
            reject_reason = "Unknown symbol";
            return false;
        }
        
        // Additional validations...
        return true;
    }
    
    void schedule_order_processing(Order order, double delay_us) {
        PendingOperation op(PendingOperation::ORDER_ACK, get_timestamp_ns() + static_cast<uint64_t>(delay_us * 1000));
        op.order = std::move(order);
        pending_operations_.push_back(op);
    }
    
    void schedule_order_cancellation(OrderId order_id, Side side, SymbolId symbol_id, double delay_us) {
        PendingOperation op(PendingOperation::ORDER_CANCEL, get_timestamp_ns() + static_cast<uint64_t>(delay_us * 1000));
        op.cancel_order_id = order_id;
        op.cancel_side = side;
        op.symbol_id = symbol_id;
        pending_operations_.push_back(op);
    }
    
    void schedule_market_data_publication(SymbolId symbol_id, Price bid, Price ask, double delay_us) {
        PendingOperation op(PendingOperation::MARKET_DATA, get_timestamp_ns() + static_cast<uint64_t>(delay_us * 1000));
        op.symbol_id = symbol_id;
        op.bid_price = bid;
        op.ask_price = ask;
        pending_operations_.push_back(op);
    }
    
    void process_pending_operations() {
        uint64_t current_time = get_timestamp_ns();
        
        auto it = std::remove_if(pending_operations_.begin(), pending_operations_.end(),
            [this, current_time](const PendingOperation& op) {
                if (op.execute_time <= current_time) {
                    execute_pending_operation(op);
                    return true;
                }
                return false;
            });
        
        pending_operations_.erase(it, pending_operations_.end());
    }
    
    void execute_pending_operation(const PendingOperation& op) {
        switch (op.type) {
            case PendingOperation::ORDER_ACK: {
                // Add order to book and send acknowledgment
                Order order = op.order;
                order.status = OrderStatus::ACKNOWLEDGED;
                
                auto book_it = order_books_.find(1); // Simplified symbol lookup
                if (book_it != order_books_.end()) {
                    book_it->second.add_order(order);
                    pending_orders_[order.id] = order;
                    
                    if (order_ack_callback_) {
                        order_ack_callback_(order);
                    }
                }
                break;
            }
            case PendingOperation::ORDER_CANCEL: {
                auto book_it = order_books_.find(op.symbol_id);
                if (book_it != order_books_.end()) {
                    if (book_it->second.remove_order(op.cancel_order_id, op.cancel_side)) {
                        pending_orders_.erase(op.cancel_order_id);
                        // Send cancel acknowledgment via callback if needed
                    }
                }
                break;
            }
            case PendingOperation::MARKET_DATA: {
                if (market_data_callback_) {
                    market_data_callback_(op.symbol_id, op.bid_price, op.ask_price);
                }
                break;
            }
        }
    }
    
    void match_orders(OrderBook& book) {
        while (!book.bids.empty() && !book.asks.empty()) {
            auto& best_bid_level = book.bids.begin()->second;
            auto& best_ask_level = book.asks.begin()->second;
            
            if (best_bid_level.empty() || best_ask_level.empty()) {
                // Clean up empty levels
                if (best_bid_level.empty()) book.bids.erase(book.bids.begin());
                if (best_ask_level.empty()) book.asks.erase(book.asks.begin());
                continue;
            }
            
            Order& best_bid = best_bid_level.front();
            Order& best_ask = best_ask_level.front();
            
            // Check if orders can match
            if (best_bid.price < best_ask.price) {
                break; // No crossing, done matching
            }
            
            // Determine match price (price improvement to taker)
            Price match_price = (best_bid.timestamp < best_ask.timestamp) ? 
                                best_ask.price : best_bid.price;
            
            // Determine match quantity
            Quantity match_quantity = std::min(best_bid.remaining_quantity(), 
                                             best_ask.remaining_quantity());
            
            // Create fills
            bool bid_is_maker = best_bid.timestamp < best_ask.timestamp;
            Fill bid_fill(best_bid.id, best_bid.trader_id, best_bid.side, 
                         match_price, match_quantity, bid_is_maker);
            Fill ask_fill(best_ask.id, best_ask.trader_id, best_ask.side, 
                         match_price, match_quantity, !bid_is_maker);
            
            // Calculate fees
            bid_fill.fee = fee_structure_.calculate_fee(match_quantity, match_price, bid_is_maker);
            ask_fill.fee = fee_structure_.calculate_fee(match_quantity, match_price, !bid_is_maker);
            
            // Update order filled quantities
            best_bid.filled_quantity += match_quantity;
            best_ask.filled_quantity += match_quantity;
            
            // Apply market impact
            apply_market_impact(best_bid.remaining_quantity() > 0 ? best_bid : best_ask, 
                              book, 0.1); // 10% participation rate
            
            // Send fill confirmations
            if (fill_callback_) {
                double fill_latency_us = latency_profile_.fill_latency(rng_);
                // Schedule fill confirmations with latency
                // For simplicity, calling immediately here
                fill_callback_(bid_fill);
                fill_callback_(ask_fill);
            }
            
            // Update statistics
            total_trades_.fetch_add(1);
            total_volume_.fetch_add(match_quantity);
            trader_stats_[best_bid.trader_id].orders_filled++;
            trader_stats_[best_ask.trader_id].orders_filled++;
            
            // Remove fully filled orders
            if (best_bid.is_fully_filled()) {
                best_bid_level.pop_front();
                pending_orders_.erase(best_bid.id);
            }
            if (best_ask.is_fully_filled()) {
                best_ask_level.pop_front();
                pending_orders_.erase(best_ask.id);
            }
            
            // Clean up empty price levels
            if (best_bid_level.empty()) {
                book.bids.erase(book.bids.begin());
            }
            if (best_ask_level.empty()) {
                book.asks.erase(book.asks.begin());
            }
        }
    }
    
    void apply_market_impact(
        const Order& order,
        OrderBook& book,
        double participation_rate
    ) {
        // Simple market impact model: larger orders have more impact
        double impact_factor = impact_model_.linear_impact * to_float_price(order.remaining_quantity()) +
                              impact_model_.sqrt_impact * std::sqrt(to_float_price(order.remaining_quantity()));
        
        impact_factor *= participation_rate; // Scale by participation rate
        
        // Apply impact by adjusting order book levels (simplified)
        if (order.side == Side::BUY) {
            // Buy order pushes ask prices up
            for (auto& [price, orders] : book.asks) {
                // Reduce quantity to simulate impact
                for (auto& o : orders) {
                    o.quantity = static_cast<Quantity>(o.quantity * (1.0 - impact_factor * 0.1));
                }
            }
        } else {
            // Sell order pushes bid prices down
            for (auto& [price, orders] : book.bids) {
                for (auto& o : orders) {
                    o.quantity = static_cast<Quantity>(o.quantity * (1.0 - impact_factor * 0.1));
                }
            }
        }
    }
    
    void simulate_hidden_liquidity(
        OrderBook& book,
        double iceberg_probability
    ) {
        // Add hidden liquidity at random levels
        std::uniform_real_distribution<double> prob_dist(0, 1);
        
        if (prob_dist(rng_) < iceberg_probability) {
            // Add hidden bid liquidity
            if (!book.bids.empty()) {
                Price level = book.bids.begin()->first - static_cast<Price>(0.01 * PRICE_MULTIPLIER);
                Quantity hidden_size = hidden_liquidity_.min_iceberg_size;
                
                Order hidden_order(next_order_id_.fetch_add(1), Side::BUY, OrderType::LIMIT,
                                 level, hidden_size, "hidden_mm_" + std::to_string(book.symbol_id));
                hidden_order.priority = 0; // Highest priority for hidden orders
                book.bids[level].push_front(hidden_order);
            }
        }
        
        if (prob_dist(rng_) < iceberg_probability) {
            // Add hidden ask liquidity
            if (!book.asks.empty()) {
                Price level = book.asks.begin()->first + static_cast<Price>(0.01 * PRICE_MULTIPLIER);
                Quantity hidden_size = hidden_liquidity_.min_iceberg_size;
                
                Order hidden_order(next_order_id_.fetch_add(1), Side::SELL, OrderType::LIMIT,
                                 level, hidden_size, "hidden_mm_" + std::to_string(book.symbol_id));
                hidden_order.priority = 0;
                book.asks[level].push_front(hidden_order);
            }
        }
    }
    
    void simulate_adverse_selection(
        const std::string& trader_id,
        double toxicity_score
    ) {
        // Adjust latency and fill probability based on trader's toxicity
        // Higher toxicity = more adverse selection = worse fills
        auto& stats = trader_stats_[trader_id];
        
        if (toxicity_score > 0.7) {
            // High toxicity trader gets worse latency and partial fills
            latency_profile_.order_ack_latency = std::normal_distribution<double>(150, 30);
            latency_profile_.fill_latency = std::normal_distribution<double>(200, 50);
        }
    }
};

} // namespace hft::simulator
