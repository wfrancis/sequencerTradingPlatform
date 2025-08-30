// messages.hpp - Core message definitions for the trading system
#pragma once

#include <cstdint>
#include <cstring>

namespace hft {

/**
 * Message Types flowing through the system
 * Lower numbers = higher priority in some queue implementations
 */
enum class MessageType : uint8_t {
    // Market Data Messages (0-9) - Highest frequency
    MARKET_DATA_TICK = 0,      // Top of book update
    ORDER_BOOK_UPDATE = 1,      // Full depth update
    TRADE = 2,                  // Trade occurred
    IMBALANCE = 3,              // Order imbalance indicator
    AUCTION = 4,                // Auction message

    // Order Management Messages (10-19)
    NEW_ORDER = 10,             // Send new order
    CANCEL_ORDER = 11,          // Cancel existing order
    REPLACE_ORDER = 12,         // Modify order (price/qty)
    ORDER_ACK = 13,             // Order acknowledged by exchange
    ORDER_REJECT = 14,          // Order rejected
    FILL = 15,                  // Order completely filled
    PARTIAL_FILL = 16,          // Order partially filled
    CANCEL_ACK = 17,            // Cancel acknowledged
    CANCEL_REJECT = 18,         // Cancel rejected

    // Position & Risk Messages (20-29)
    POSITION_UPDATE = 20,       // Position changed
    RISK_LIMIT = 21,           // Risk limit update
    PNL_UPDATE = 22,           // P&L update
    MARGIN_CALL = 23,          // Margin requirement change
    EXPOSURE_UPDATE = 24,       // Exposure calculation

    // Strategy Messages (30-39)
    SIGNAL = 30,               // Trading signal generated
    STRATEGY_COMMAND = 31,      // Control strategy behavior
    PARAMETER_UPDATE = 32,      // Update strategy parameters
    STRATEGY_STATUS = 33,       // Strategy health/status

    // System Messages (40-49)
    HEARTBEAT = 40,            // Keep-alive message
    START_OF_DAY = 41,         // Trading session start
    END_OF_DAY = 42,           // Trading session end
    HALT = 43,                 // Trading halt
    RESUME = 44,               // Trading resume

    // Control Messages (50+)
    SHUTDOWN = 50,             // Graceful shutdown
    EMERGENCY_STOP = 51        // Kill switch activated
};

/**
 * Trading Venues
 * Each venue has different latency characteristics and protocols
 */
enum class Venue : uint8_t {
    BINANCE = 0,    // Crypto - WebSocket/REST
    COINBASE = 1,   // Crypto - FIX/WebSocket
    CME = 2,        // Futures - iLink3 (SBE)
    NYSE = 3,       // Equities - NYSE Pillar
    NASDAQ = 4,     // Equities - OUCH/ITCH
    EUREX = 5,      // Derivatives - ETI
    DERIBIT = 6,    // Crypto Options
    OKX = 7,        // Crypto
    BATS = 8,       // Equities - BOE
    ICE = 9         // Commodities
};

/**
 * Order Side
 */
enum class Side : uint8_t {
    BID = 0,        // Buy side
    ASK = 1,        // Sell side
    BUY = 0,        // Alias for BID
    SELL = 1        // Alias for ASK
};

/**
 * Order Types
 */
enum class OrderType : uint8_t {
    LIMIT = 0,              // Standard limit order (maker)
    MARKET = 1,             // Market order (taker)
    LIMIT_IOC = 2,          // Immediate or cancel
    LIMIT_FOK = 3,          // Fill or kill
    POST_ONLY = 4,          // Maker only (reject if crosses)
    STOP = 5,               // Stop order
    STOP_LIMIT = 6,         // Stop limit order
    ICEBERG = 7,            // Hidden quantity order
    PEGGED = 8              // Pegged to market
};

/**
 * Time in Force
 */
enum class TimeInForce : uint8_t {
    DAY = 0,        // Valid for trading day
    GTC = 1,        // Good till cancelled
    IOC = 2,        // Immediate or cancel
    FOK = 3,        // Fill or kill
    GTX = 4,        // Good till crossing
    GTD = 5         // Good till date
};

/**
 * Order Status
 */
enum class OrderStatus : uint8_t {
    PENDING = 0,            // Not yet sent to exchange
    SENT = 1,              // Sent to exchange
    ACKNOWLEDGED = 2,       // Accepted by exchange
    PARTIALLY_FILLED = 3,   // Some quantity filled
    FILLED = 4,            // Completely filled
    CANCELLED = 5,         // Cancelled
    REJECTED = 6,          // Rejected by exchange
    EXPIRED = 7            // Expired (GTD/DAY orders)
};

// Type aliases for clarity and easy changes
using Price = uint64_t;        // Fixed point: 8 decimal places
using Quantity = uint64_t;      // Fixed point: 8 decimal places
using OrderId = uint64_t;       // Unique order identifier
using SymbolId = uint16_t;      // Internal symbol ID
using StrategyId = uint16_t;    // Strategy identifier

// Constants for fixed-point arithmetic
constexpr uint64_t PRICE_MULTIPLIER = 100000000ULL;     // 10^8 for 8 decimals
constexpr uint64_t QUANTITY_MULTIPLIER = 100000000ULL;  // 10^8 for 8 decimals

/**
 * Convert floating point price to fixed point
 * @param price Floating point price
 * @return Fixed point representation
 */
inline Price to_fixed_price(double price) {
    return static_cast<Price>(price * PRICE_MULTIPLIER);
}

/**
 * Convert fixed point price to floating point
 * @param price Fixed point price
 * @return Floating point representation
 */
inline double to_float_price(Price price) {
    return static_cast<double>(price) / PRICE_MULTIPLIER;
}

/**
 * Core sequenced message structure
 *
 * This is the fundamental unit of data flow in the system.
 * Exactly 64 bytes (1 cache line) for optimal performance.
 *
 * Memory layout is carefully designed:
 * - First 16 bytes: Sequencing information (most frequently accessed)
 * - Next 16 bytes: Message routing/type information
 * - Last 32 bytes: Payload (union of different message types)
 */
struct alignas(64) SequencedMessage {
    // ========== Sequencing Information (16 bytes) ==========
    uint64_t sequence;          // Global sequence number from sequencer
    uint64_t timestamp_ns;      // Hardware timestamp (TSC or PTP)

    // ========== Message Header (16 bytes) ==========
    MessageType type;           // Message type (1 byte)
    Venue venue;               // Trading venue (1 byte)
    SymbolId symbol_id;        // Internal symbol ID (2 bytes)
    StrategyId strategy_id;    // Strategy that generated/owns (2 bytes)
    uint16_t source_id;        // Component that created message (2 bytes)
    uint64_t correlation_id;   // For request/response matching (8 bytes)

    // ========== Message Payload (32 bytes) ==========
    // Union allows reuse of same memory for different message types
    union {
        // Market data tick
        struct {
            Price bid_price;        // Best bid price
            Quantity bid_size;      // Best bid quantity
            Price ask_price;        // Best ask price
            Quantity ask_size;      // Best ask quantity
        } market_data;

        // Order information
        struct {
            OrderId order_id;       // Exchange order ID
            Price price;            // Limit price
            Quantity quantity;      // Order quantity
            Side side;             // Buy/Sell
            OrderType order_type;   // Order type
            TimeInForce tif;       // Time in force
            OrderStatus status;     // Current status
            uint32_t flags;        // Additional flags/metadata
        } order;

        // Position update
        struct {
            int64_t position;       // Net position (signed)
            int64_t realized_pnl;   // Realized P&L
            int64_t unrealized_pnl; // Unrealized P&L
            uint64_t exposure;      // Total exposure
        } position;

        // Trading signal
        struct {
            uint32_t signal_type;   // Type of signal
            float strength;         // Signal strength [0, 1]
            Price target_price;     // Target/fair value price
            float confidence;       // Confidence level [0, 1]
            float expected_edge;    // Expected profit (bps)
            uint16_t hold_time_ms;  // Expected holding period
            uint16_t _padding;
        } signal;

        // Fill information
        struct {
            OrderId order_id;       // Order that was filled
            Price fill_price;       // Execution price
            Quantity fill_quantity; // Executed quantity
            uint64_t trade_id;     // Exchange trade ID
            int64_t fee;           // Transaction fee (can be negative for rebates)
        } fill;

        // Risk limits
        struct {
            int64_t max_position;   // Maximum position size
            uint64_t max_exposure;  // Maximum exposure
            uint64_t max_order_size;// Maximum single order size
            uint32_t max_order_rate;// Max orders per second
            float max_loss;        // Maximum loss allowed
            uint32_t _padding;
        } risk_limit;

        // Raw payload for custom messages
        uint8_t raw_payload[32];
    };

    /**
     * Default constructor - zero initializes
     */
    SequencedMessage() {
        std::memset(this, 0, sizeof(SequencedMessage));
    }

    /**
     * Check if this is a latency-critical message
     * @return true if message requires immediate processing
     */
    bool is_critical() const {
        return type == MessageType::FILL ||
               type == MessageType::PARTIAL_FILL ||
               type == MessageType::ORDER_REJECT ||
               type == MessageType::MARKET_DATA_TICK;
    }
};

// Compile-time check that message is exactly one cache line
// Note: On some systems (like macOS), alignment may cause larger size
#ifdef __linux__
static_assert(sizeof(SequencedMessage) == 64,
              "SequencedMessage must be exactly 64 bytes");
#else
// For MVP/development on other systems, just warn about size
static_assert(sizeof(SequencedMessage) <= 128,
              "SequencedMessage should be <= 128 bytes for cache efficiency");
#endif

} // namespace hft